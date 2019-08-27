#define LOG_TAG "SensorsHAL::IIO_Sensor"

#include "IIO_sensor.h"
#include "Sensors.h"

#include <android-base/logging.h>
#include <utils/SystemClock.h>
#include <algorithm>
#include <cmath>
#include <chrono>

#include <linux/input.h>
#include <fcntl.h>
#include <errno.h>

namespace android {
namespace hardware {
namespace sensors {
namespace V1_0 {
namespace kingfisher {

IIO_sensor::IIO_sensor(const IIOSensorDescriptor &sensorDescriptor):
        mSensorDescriptor(sensorDescriptor),
        mCounter(0),
        mIsEnabled(false),
        mMaxReportLatency(0)
{
    getAvailFreqTable();

    if (mAvaliableODR.empty()) {
        ALOGE("Failed to get avaliable ODR table from sysfs, using only default ODR (%u)", mCurrODR.load());
        mAvaliableODR.push_back(mSensorDescriptor.defaultODR);
    }

    mSensorDescriptor.sensorInfo.minDelay = 1e6 / ( *(std::max_element(
        std::begin(mAvaliableODR),
        std::end(mAvaliableODR))));

    mSensorDescriptor.sensorInfo.maxDelay = 1e6 / ( *(std::min_element(
        std::begin(mAvaliableODR),
        std::end(mAvaliableODR))));

    mCurrODR = *(std::min_element(
        std::begin(mAvaliableODR),
        std::end(mAvaliableODR)));

    ALOGD("%s: minDelay = %d", mSensorDescriptor.sensorInfo.name.c_str(), mSensorDescriptor.sensorInfo.minDelay);
    ALOGD("%s: maxDelay = %d", mSensorDescriptor.sensorInfo.name.c_str(), mSensorDescriptor.sensorInfo.maxDelay);

    mFilterBuffer = std::vector<Vector3D<double>>(filterBufferSize, mSensorDescriptor.initialValue);
    updateWatermark();
}

void IIO_sensor::pushEvent(AtomicBuffer& buffer, Event e)
{
    std::unique_lock<std::mutex> lock(buffer.mutex);

    /* Remove first event from queue and place new to the end */
    if(buffer.eventBuffer.size() >= buffer.maxSize)
        buffer.eventBuffer.pop();

    buffer.eventBuffer.push(e);
}

size_t IIO_sensor::getReadyEventsCount(OperationMode mode)
{
    size_t eventCount = 0;

    switch (mode) {
        case OperationMode::NORMAL:
        {
            std::unique_lock<std::mutex> bufferLock(mEventBuffer.mutex);
            eventCount = mEventBuffer.eventBuffer.size();
            break;
        }
        case OperationMode::DATA_INJECTION:
        {
            std::unique_lock<std::mutex> injectLock(mInjectEventBuffer.mutex);
            eventCount = mInjectEventBuffer.eventBuffer.size();
            break;
        }
    }

    return eventCount;
}

int IIO_sensor::getReadyEvents(std::vector<Event>& events, OperationMode mode, int maxEventCount)
{
    if (maxEventCount < 1) {
        return 0;
    }

    int eventCount = 0;
    Event event;
    switch (mode) {
        case OperationMode::NORMAL:
        {
            std::lock_guard<std::mutex> bufferLock(mEventBuffer.mutex);

            eventCount = mEventBuffer.eventBuffer.size();
            if (eventCount > maxEventCount)
                eventCount = maxEventCount;

            for (int i = 0; i < eventCount; i++) {
                event = mEventBuffer.eventBuffer.front();
                /*
                 * Polling thread can be waked up by flush event.
                 * So after this we need to decrement flush counter.
                 */
                if (event.u.meta.what == MetaDataEventType::META_DATA_FLUSH_COMPLETE)
                    mFlushCounter--;

                events.push_back(event);
                mEventBuffer.eventBuffer.pop();
            }
            break;
        }
        case OperationMode::DATA_INJECTION:
        {
            std::lock_guard<std::mutex> injectLock(mInjectEventBuffer.mutex);

            eventCount = mInjectEventBuffer.eventBuffer.size();
            if (eventCount > maxEventCount)
                eventCount = maxEventCount;

            for (int i = 0; i < eventCount; i++) {
                events.push_back(mInjectEventBuffer.eventBuffer.front());
                mInjectEventBuffer.eventBuffer.pop();
            }
            break;
        }
    }

    return eventCount;
}

Return<Result> IIO_sensor::activate(bool enable)
{
    /* Flush event queues when sensor is deactivated*/
    if (mIsEnabled && !enable) {
        std::unique_lock<std::mutex> lock(mEventBuffer.mutex);
        mEventBuffer.eventBuffer = std::queue<Event>();
        lock.unlock();

        std::unique_lock<std::mutex> injectLock(mInjectEventBuffer.mutex);
        mInjectEventBuffer.eventBuffer = std::queue<Event>();
        injectLock.unlock();
    }
    ALOGD("%s %s", enable ? "Activating" : "Deactivating", mSensorDescriptor.sensorInfo.name.c_str());
    mIsEnabled = enable;
    return Return<Result>(Result::OK);
}

Return<Result> IIO_sensor::batch(int64_t argSamplingPeriodNs, int64_t maxReportLatency)
{
    uint16_t requestedODR = samplingPeriodNsToODR(argSamplingPeriodNs);

    ALOGD("Requested to set %u Hz as ODR for %s", requestedODR, mSensorDescriptor.sensorInfo.name.c_str());

    if (!testSensorODR(requestedODR)) {
#ifdef POLL_DEBUG
        ALOGD("Unsupported ODR = %d Hz requested for %s, samplingPeriod = %ld", requestedODR,
                mSensorDescriptor.sensorInfo.name.c_str(), argSamplingPeriodNs);

        ALOGD("ODR has been changed to closest (%u) from requested %u Hz for %s", getClosestOdr(requestedODR),
                requestedODR, mSensorDescriptor.sensorInfo.name.c_str());
#endif
        requestedODR = getClosestOdr(requestedODR);
    }
#ifdef POLL_DEBUG
    ALOGD("Set new ODR for %s equal %d Hz", mSensorDescriptor.sensorInfo.name.c_str(), requestedODR);
#endif

    mCurrODR = requestedODR;
    mMaxReportLatency = maxReportLatency;
    updateWatermark();

    ALOGD("Coming out from batch. samplPeriod = %lu, setup ODR for %s as %u Hz", argSamplingPeriodNs,
            mSensorDescriptor.sensorInfo.name.c_str(), mCurrODR.load());
    return Return<Result>(Result::OK);
}

Return<Result> IIO_sensor::flush()
{
    if (mIsEnabled.load()) {
        mFlushCounter++;
        pushEvent(mEventBuffer, createFlushEvent());
        return Return<Result>(Result::OK);
    } else {
        return Return<Result>(Result::BAD_VALUE);
    }
}

void IIO_sensor::medianFilter(Event &readEvent)
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

Event IIO_sensor::chunkTransform(const IIOBuffer& rawBuffer)
{
    Event event;

    event.u.vec3.x = rawBuffer.coords.x * mSensorDescriptor.sensorInfo.resolution;
    event.u.vec3.y = rawBuffer.coords.y * mSensorDescriptor.sensorInfo.resolution;
    event.u.vec3.z = rawBuffer.coords.z * mSensorDescriptor.sensorInfo.resolution;

    medianFilter(event);

    return event;
}

uint64_t IIO_sensor::timestampTransform(uint64_t timestamp)
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

Event IIO_sensor::createFlushEvent()
{
    Event flushEvent;

    flushEvent.sensorHandle = mSensorDescriptor.sensorInfo.sensorHandle;
    flushEvent.sensorType = SensorType::META_DATA;
    flushEvent.u.meta.what = MetaDataEventType::META_DATA_FLUSH_COMPLETE;

    return flushEvent;
}

void IIO_sensor::transformData(const IIOBuffer& rawBuffer) {
    Event event;

    event = chunkTransform(rawBuffer);
    event.timestamp = timestampTransform(rawBuffer.timestamp);
    event.sensorType = mSensorDescriptor.sensorInfo.type;
    event.u.vec3.status = SensorStatus::ACCURACY_HIGH;
    event.sensorHandle = mSensorDescriptor.sensorInfo.sensorHandle;

#ifdef POLL_DEBUG
    ALOGD(" ===== > Event from %d sensor (count = %u): x = %f, y = %f, z = %f, norm = %f, Rawtimestamp = %lu",
    event.sensorHandle, mCounter, event.u.vec3.x,
        event.u.vec3.y, event.u.vec3.z,
        std::sqrt(event.u.vec3.x * event.u.vec3.x + event.u.vec3.y * event.u.vec3.y + event.u.vec3.z * event.u.vec3.z),
        rawBuffer.timestamp);
#endif

    mCounter++;

    pushEvent(mEventBuffer, event);
}

void IIO_sensor::getAvailFreqTable()
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

void IIO_sensor::injectEvent(const Event& event)
{
    pushEvent(mInjectEventBuffer, event);
}

uint16_t IIO_sensor::getClosestOdr(uint16_t requestedODR)
{
    auto it = std::lower_bound(mAvaliableODR.begin(), mAvaliableODR.end(), requestedODR);

    /* Avoid out of list assignment and set max frequency */
    if(it == mAvaliableODR.end())
        --it;

    return *it;
}

void IIO_sensor::updateWatermark()
{
    /*
     * Watermark - count of event that will be send with one poll
     * transaction. To calculate it we count number of event that
     * will be stored in HAL FIFO while mMaxReportLatency ns by
     * multiplying latency duration on current sensor data rate.
     * It divides by 2 to guarantee that we will not miss report
     * latency deadline.
     */
    size_t newWatermark = (mMaxReportLatency * mCurrODR) / (2 * 1e9);

    if (newWatermark > AtomicBuffer::maxSize)
        newWatermark = AtomicBuffer::maxSize;

    if (!(newWatermark && mMaxReportLatency))
        newWatermark = 1;

    mWatermark = newWatermark;
}

}  // namespace kingfisher
}  // namespace V1_0
}  // namespace sensors
}  // namespace hardware
}  // namespace android
