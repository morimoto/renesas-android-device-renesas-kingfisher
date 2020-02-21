#define LOG_TAG "SensorsHAL::IIO_Sensor"

#include "IIOSensor.h"
#include "SensorDescriptors.h"
#include "Sensors.h"

#include <android-base/logging.h>
#include <utils/SystemClock.h>
#include <algorithm>
#include <cmath>
#include <chrono>

#include <linux/input.h>
#include <fcntl.h>
#include <errno.h>
#include <hidl/HidlSupport.h>
#include <cstdlib>


namespace android {
namespace hardware {
namespace sensors {
namespace V2_0 {
namespace kingfisher {

using ::android::hardware::Return;
using ::android::hardware::sensors::V1_0::SensorStatus;
using ::android::hardware::sensors::V1_0::MetaDataEventType;
using ::android::hardware::sensors::V1_0::AdditionalInfoType;
using ::android::hardware::sensors::V1_0::AdditionalInfo;

IIOSensor::IIOSensor(const SensorDescriptor &sensorDescriptor) :
        BaseSensor(sensorDescriptor),
        mCounter(0)
{
    getAvailFreqTable();

    if (mAvaliableODR.empty()) {
        ALOGE("Failed to get avaliable ODR table from sysfs, using only default ODR (%u)",
            mCurrODR.load());
        mAvaliableODR.push_back(mSensorDescriptor.defaultODR);
    }

    const std::pair<float, float> resolution = findBestResolution();
    if (resolution.first != 0) {
        setResolution(resolution);
    }

    mSensorDescriptor.sensorInfo.minDelay = USEC / ( *(std::max_element(
        std::begin(mAvaliableODR),
        std::end(mAvaliableODR))));

    mSensorDescriptor.sensorInfo.maxDelay = USEC / ( *(std::min_element(
        std::begin(mAvaliableODR),
        std::end(mAvaliableODR))));

    mCurrODR = *(std::min_element(
        std::begin(mAvaliableODR),
        std::end(mAvaliableODR)));

    ALOGD("%s: minDelay = %d", mSensorDescriptor.sensorInfo.name.c_str(),
        mSensorDescriptor.sensorInfo.minDelay);
    ALOGD("%s: maxDelay = %d", mSensorDescriptor.sensorInfo.name.c_str(),
        mSensorDescriptor.sensorInfo.maxDelay);

    mFilterBuffer =
        std::vector<Vector3D<double>>(filterBufferSize, mSensorDescriptor.initialValue);
}

std::pair<float, float> IIOSensor::findBestResolution() const
{
    if(mSensorDescriptor.resolutions.find(mSensorDescriptor.sensorInfo.maxRange) ==
            mSensorDescriptor.resolutions.end()){
        ALOGE("Can't find correct resolution for %s",
            mSensorDescriptor.sensorInfo.name.c_str());
        return std::pair<float, float>(0, 0);
    }
    return mSensorDescriptor.resolutions.at(mSensorDescriptor.sensorInfo.maxRange);
}

void IIOSensor::setResolution(std::pair<float, float> resolution)
{
    ALOGI("Setting %s resolution to %f",
        mSensorDescriptor.sensorInfo.name.c_str(), resolution.first);
    std::ofstream file(mSensorDescriptor.scaleFileName);
    if(!file.is_open()){
        ALOGW("Failed to write %s",mSensorDescriptor.scaleFileName.c_str());
        return;
    }
    file << resolution.first;
    file.close();
    mSensorDescriptor.sensorInfo.resolution = resolution.second;
}

void IIOSensor::pushEvent(AtomicBuffer& buffer, Event e)
{
    if (!mIsEnabled.load())
        return;

    std::unique_lock<std::mutex> lock(buffer.mutex);

    /* Remove first event from queue and place new to the end */
    if(buffer.eventBuffer.size() >= buffer.maxSize)
        buffer.eventBuffer.pop();

    buffer.eventBuffer.push(e);
}

int IIOSensor::getReadyEvents(std::vector<Event>& events, OperationMode mode)
{
    int eventCount = 0;
    Event event;
    switch (mode) {
        case OperationMode::NORMAL:
        {
            std::lock_guard<std::mutex> bufferLock(mEventBuffer.mutex);

            eventCount = mEventBuffer.eventBuffer.size();

            for (int i = 0; i < eventCount; i++) {
                event = mEventBuffer.eventBuffer.front();
                events.push_back(event);
                mEventBuffer.eventBuffer.pop();
            }
            break;
        }
        case OperationMode::DATA_INJECTION:
        {
            std::lock_guard<std::mutex> injectLock(mInjectEventBuffer.mutex);

            eventCount = mInjectEventBuffer.eventBuffer.size();

            for (int i = 0; i < eventCount; i++) {
                events.push_back(mInjectEventBuffer.eventBuffer.front());
                mInjectEventBuffer.eventBuffer.pop();
            }
            break;
        }
    }

    return eventCount;
}

void IIOSensor::addVirtualListener(uint32_t sensorHandle)
{
    if (std::find(mListenerHandlers.begin(), mListenerHandlers.end(),
        sensorHandle) == mListenerHandlers.end()) {
        mListenerHandlers.push_back(sensorHandle);
    }
}

void IIOSensor::removeVirtualListener(uint32_t sensorHandle)
{
    std::vector<uint32_t>::iterator iter =
        std::find(mListenerHandlers.begin(), mListenerHandlers.end(),
        sensorHandle);

    if (iter != mListenerHandlers.end()) {
        mListenerHandlers.erase(iter);
    }
}

bool IIOSensor::hasActiveListeners() const
{
    return !mListenerHandlers.empty();
}


Return<Result> IIOSensor::activate(bool enable)
{
    mIsEnabled = enable;

    /* Flush event queues when sensor is deactivated*/
    if (mIsEnabled.load() && !enable) {
        std::unique_lock<std::mutex> lock(mEventBuffer.mutex);
        mEventBuffer.eventBuffer = std::queue<Event>();
        lock.unlock();

        std::unique_lock<std::mutex> injectLock(mInjectEventBuffer.mutex);
        mInjectEventBuffer.eventBuffer = std::queue<Event>();
        injectLock.unlock();
    } else {
        mTimestamp = ::android::elapsedRealtimeNano();
        sendAdditionalData();
    }

    ALOGD("%s %s", enable ? "Activating" : "Deactivating",
        mSensorDescriptor.sensorInfo.name.c_str());
    return Return<Result>(Result::OK);
}

Return<Result> IIOSensor::batch(int64_t argSamplingPeriodNs, int64_t)
{
    uint16_t requestedODR = samplingPeriodNsToODR(argSamplingPeriodNs);

    ALOGD("Requested to set %u Hz as ODR for %s",
        requestedODR, mSensorDescriptor.sensorInfo.name.c_str());

    if (!testSensorODR(requestedODR)) {
#ifdef POLL_DEBUG
        ALOGD("Unsupported ODR = %d Hz requested for %s, samplingPeriod = %ld",
            requestedODR, mSensorDescriptor.sensorInfo.name.c_str(),
            argSamplingPeriodNs);

        ALOGD("ODR has been changed to closest (%u) from requested %u Hz for %s",
            getClosestOdr(requestedODR), requestedODR,
            mSensorDescriptor.sensorInfo.name.c_str());
#endif
        requestedODR = getClosestOdr(requestedODR);
    }
#ifdef POLL_DEBUG
    ALOGD("Set new ODR for %s equal %d Hz",
        mSensorDescriptor.sensorInfo.name.c_str(), requestedODR);
#endif

    mCurrODR = requestedODR;
    ALOGD("Coming out from batch. samplPeriod = %lu, setup ODR for %s as %u Hz",
        argSamplingPeriodNs, mSensorDescriptor.sensorInfo.name.c_str(), mCurrODR.load());
    return Return<Result>(Result::OK);
}

Return<Result> IIOSensor::flush()
{
    if (mIsEnabled.load()) {
        pushEvent(mEventBuffer, createFlushEvent());
        sendAdditionalData();
        return Return<Result>(Result::OK);
    } else {
        return Return<Result>(Result::BAD_VALUE);
    }
}

void IIOSensor::medianFilter(Event &readEvent)
{
    Vector3D<double> newVector = {0.0, 0.0, 0.0};

    newVector.x = readEvent.u.vec3.x;
    newVector.y = readEvent.u.vec3.y;
    newVector.z = readEvent.u.vec3.z;

    mFilterBuffer[mCounter % filterBufferSize] = newVector;

    std::vector<Vector3D<double>> sortedFilterBuffer = mFilterBuffer;

    std::nth_element(sortedFilterBuffer.begin(),
            sortedFilterBuffer.begin() + filterBufferSize / 2,
            sortedFilterBuffer.end(),
        [](const Vector3D<double> &v1,const Vector3D<double> &v2)->bool {
            return (v1.x < v2.x);
        });
    readEvent.u.vec3.x = sortedFilterBuffer[sortedFilterBuffer.size() / 2].x;

    std::nth_element(sortedFilterBuffer.begin(),
                sortedFilterBuffer.begin() + filterBufferSize / 2,
                sortedFilterBuffer.end(),
        [](const Vector3D<double> &v1,const Vector3D<double> &v2)->bool {
            return (v1.y < v2.y);
        });
    readEvent.u.vec3.y = sortedFilterBuffer[sortedFilterBuffer.size() / 2].y;

    std::nth_element(sortedFilterBuffer.begin(),
                sortedFilterBuffer.begin() + filterBufferSize / 2,
                sortedFilterBuffer.end(),
        [](const Vector3D<double> &v1,const Vector3D<double> &v2)->bool {
            return (v1.z < v2.z);
        });
    readEvent.u.vec3.z = sortedFilterBuffer[sortedFilterBuffer.size() / 2].z;
}

Event IIOSensor::chunkTransform(const IIOBuffer& rawBuffer)
{
    Event event;

    event.u.vec3.x = rawBuffer.coords.x * mSensorDescriptor.sensorInfo.resolution;
    event.u.vec3.y = rawBuffer.coords.y * mSensorDescriptor.sensorInfo.resolution;
    event.u.vec3.z = rawBuffer.coords.z * mSensorDescriptor.sensorInfo.resolution;

    medianFilter(event);

    return event;
}

uint64_t IIOSensor::timestampTransform(uint64_t timestamp)
{
    /*
    * Those calculations are required because Android expects us to send timestamp in nanoseconds from boot
    * but timestamp in event is in nanoseconds since UNIX epoch
    * so to convert UNIX epoch time to nanoseconds from boot it's required to make a few calculations:
    * get current time from epoch using std::chrono
    * substract 'current time from boot' from 'current time from epoch'
    * now we have an exact time of the boot
    * the last step is to substract calculated boot time from timestamp
    */
    uint64_t fromEpoch = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() - android::elapsedRealtimeNano();

    return (timestamp - fromEpoch);

}

void IIOSensor::sendAdditionalData()
{
    std::vector<Event> additionalEvents = generateAdditionalEvent();

    for (size_t i = 0; i < additionalEvents.size(); ++i) {
        pushEvent(mEventBuffer, additionalEvents[i]);
    }
}



void IIOSensor::transformData(const IIOBuffer& rawBuffer) {
    Event event;

    event = chunkTransform(rawBuffer);
    event.timestamp = timestampTransform(rawBuffer.timestamp);

    if (event.timestamp < mTimestamp) {
            return;
    }
    mTimestamp = event.timestamp;

    event.sensorType = mSensorDescriptor.sensorInfo.type;
    event.u.vec3.status = SensorStatus::ACCURACY_HIGH;
    event.sensorHandle = mSensorDescriptor.sensorInfo.sensorHandle;

#ifdef POLL_DEBUG
    ALOGD(" ===== > Event from %d sensor (count = %u): x = %f, y = %f, z = %f, \
        norm = %f, event.timestamp = %lu",
    event.sensorHandle, mCounter, event.u.vec3.x,
        event.u.vec3.y, event.u.vec3.z,
        std::sqrt(event.u.vec3.x * event.u.vec3.x + event.u.vec3.y * event.u.vec3.y
        + event.u.vec3.z * event.u.vec3.z), event.timestamp);
#endif

    mCounter++;

    pushEvent(mEventBuffer, event);
}

void IIOSensor::getAvailFreqTable()
{
    std::ifstream file(mSensorDescriptor.availFreqFileName);
    if (!file.is_open()) {
        ALOGW("Failed to read %s ", mSensorDescriptor.availFreqFileName.c_str());
    }
    std::string line;
    std::getline(file, line);
    file.close();

    std::istringstream iss(line);
    std::vector<std::string> arrAvailFreq((std::istream_iterator<std::string>(iss)),
                            std::istream_iterator<std::string>());

    for(size_t i = 0; i < arrAvailFreq.size(); i++) {
        uint16_t ODR = std::stoul(arrAvailFreq[i]);
        if (ODR >= mSensorDescriptor.minODR && ODR <= mSensorDescriptor.maxODR)
            mAvaliableODR.push_back(ODR);
    }

    std::sort(mAvaliableODR.begin(), mAvaliableODR.end());
}

void IIOSensor::injectEvent(const Event& event)
{
    pushEvent(mInjectEventBuffer, event);
}

uint16_t IIOSensor::getClosestOdr(uint16_t requestedODR)
{
    auto it = std::lower_bound(mAvaliableODR.begin(), mAvaliableODR.end(), requestedODR);

    /* Avoid out of list assignment and set max frequency */
    if(it == mAvaliableODR.end())
        --it;

    return *it;
}

}  // namespace kingfisher
}  // namespace V2_0
}  // namespace sensors
}  // namespace hardware
}  // namespace android
