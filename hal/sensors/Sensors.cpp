#define LOG_TAG "SensorsHAL::Sensors"

#include "Sensors.h"
#include <android-base/logging.h>
#include <utils/SystemClock.h>
#include <thread>
#include <chrono>
#include <linux/input.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include "IIO_sensor_descriptors.h"

using namespace std::chrono_literals;

namespace android {
namespace hardware {
namespace sensors {
namespace V1_0 {
namespace kingfisher {

Sensors::Sensors()
{
    mMode = OperationMode::NORMAL;
    mSensorDescriptors =
        std::vector<IIOSensorDescriptor>(sensors_descriptors,
        sensors_descriptors + (sizeof(sensors_descriptors) / sizeof(sensors_descriptors[0])));
    mSensorGroups = sensor_group_descriptors;

    fillGroups();

    mSensors.resize(SensorIndex::COUNT);
    mSensors[SensorIndex::ACC] = std::make_unique<IIO_sensor>(mSensorDescriptors[SensorIndex::ACC]);
    mSensors[SensorIndex::GYR] = std::make_unique<IIO_sensor>(mSensorDescriptors[SensorIndex::GYR]);
    mSensors[SensorIndex::MAG] = std::make_unique<IIO_sensor>(mSensorDescriptors[SensorIndex::MAG]);

    activateAllGroups();

    mSensors[ACC]->activate(true);
    mSensors[GYR]->activate(true);
    mSensors[MAG]->activate(true);

    mEventsReady = false;

    /* File descriptors must be opened before starting threads */
    openFileDescriptors();
    startPollThreads();
}

Sensors::~Sensors()
{
    stopPollThreads();
    closeFileDescriptors();
}

void Sensors::pollIIODeviceBuffer(IIO_sensor *sensor)
{
    int retryCount = 0;

    while(!mTerminatePollThreads.load()) {
        IIOBuffer readBuffer;
        int readBytes = ::read(mSensorGroups[1].fd, &readBuffer, sizeof(readBuffer));
        if (readBytes == sizeof(readBuffer)) {
            if(sensor->isActive()) {
                sensor->transformData(readBuffer);
                mCondVarBufferReady.notify_one();
            }
        } else {
            ALOGE("Failed to read data from gyro buffer file");
            ALOGE("Expected %lu bytes, actual %d", sizeof(IIOBuffer), readBytes);
            if(++retryCount > maxReadRetries)
                mTerminatePollThreads = true;
        }
    }
}

/*
 * This method is needed to separate data from 2 sensors inside LSM9DS0 - accelerometer
 * and magnetometer. They are placed at the same I2C address and have same chrdev in /dev.
 * In case of another sensor can be easily removed with PollIIODeviceBuffer()
 */
void Sensors::pollIIOAccelMagnBuffer()
{
    int retryCount = 0;

    while(!mTerminatePollThreads.load()) {
        /* Add convertation to generic buffer */
        IIOBufferAccelMagn readBuffer;
        int readBytes = ::read(mSensorGroups[0].fd, &readBuffer, sizeof(readBuffer));
        if (readBytes == sizeof(readBuffer)) {
            IIOBuffer accelBuffer, magnBuffer;

            accelBuffer.coords = readBuffer.accel;
            accelBuffer.timestamp = readBuffer.timestamp;
            magnBuffer.coords = readBuffer.magn;
            magnBuffer.timestamp = readBuffer.timestamp;
            uint32_t groupIndex = getGroupIndexByHandle(mSensorGroups[0].sensorHandles[0]);
            uint16_t maxODR = getMaxODRFromGroup(groupIndex);

            for (size_t i = 0; i < mSensorGroups[groupIndex].sensorHandles.size(); i++) {
                uint32_t sensorHandle = mSensorGroups[groupIndex].sensorHandles[i];
                uint32_t sensorIndex = handleToIndex(sensorHandle);
                uint16_t sensorODR = mSensors[sensorIndex]->getODR();

                mSensors[sensorIndex]->addTicks(sensorODR);
                if (mSensors[sensorIndex]->getTicks() >= maxODR) {
                    mSensors[sensorIndex]->resetTicks();
                    if (mSensors[sensorIndex]->isActive()) {
                        /*This is just for test*/
                        if (sensorHandle == HandleIndex::ACC_HANDLE) {
                            mSensors[sensorIndex]->transformData(accelBuffer);
                        } else if (sensorHandle == HandleIndex::MAGN_HANDLE) {
                            mSensors[sensorIndex]->transformData(magnBuffer);
                        }
                        mCondVarBufferReady.notify_one();
                    }
                }
            }
        } else {
            ALOGE("Failed to read data from Accel&Magn buffer file");
            ALOGE("Expected %lu bytes, actual %d", sizeof(IIOBufferAccelMagn), readBytes);
            if(++retryCount > maxReadRetries)
                mTerminatePollThreads = true;
        }
    }
}


void Sensors::startPollThreads()
{
    /* TODO: unify this and remove hardcode */
    if (mSensorGroups[0].fd >= 0) {
        mSensorGroups[0].thread = std::make_shared<std::thread>(&Sensors::pollIIODeviceBuffer, this,
                                                              mSensors[GYR].get());
    } else {
        ALOGE("Failed to start poll thread for Gyro");
    }

    if (mSensorGroups[1].fd >= 0) {
        mSensorGroups[1].thread = std::make_shared<std::thread>(&Sensors::pollIIOAccelMagnBuffer, this);
    } else {
        ALOGE("Failed to start poll thread for Accel&&Magn");
    }
}

void Sensors::stopPollThreads()
{
    mTerminatePollThreads = true;

    if (mSensorGroups[0].thread->joinable())
        mSensorGroups[0].thread->join();

    if (mSensorGroups[1].thread->joinable())
        mSensorGroups[1].thread->join();
}

Return<void> Sensors::getSensorsList(getSensorsList_cb cb)
{
    hidl_vec<SensorInfo> sensorsList;
    sensorsList.resize(SensorIndex::COUNT);
    for (size_t i = 0; i < SensorIndex::COUNT; ++i)
        sensorsList[i] = mSensors[i]->getSensorInfo();

    cb(sensorsList);

    return Void();
}

Return<Result> Sensors::setOperationMode(OperationMode actualMode)
{
    ALOGD("Set %s mode for Sensors HAL", (actualMode == OperationMode::NORMAL) ? "normal" : "data injection");
    if ((actualMode == OperationMode::NORMAL) ||
        (actualMode == OperationMode::DATA_INJECTION) ) {
        mMode = actualMode;
        return Result::OK;
    } else {
        return Result::BAD_VALUE;
    }
}

Return<Result> Sensors::activate(int32_t sensorHandle, bool enabled)
{
    if (!testHandle(sensorHandle))
        return Result::BAD_VALUE;

    mSensors[handleToIndex(sensorHandle)]->activate(enabled);

    int groupIndex = getGroupIndexByHandle(sensorHandle);
    /* If any sensor in group is active, we can't disable whole group */
    activateGroup(isActiveGroup(groupIndex), groupIndex);

    return Result::OK;
}

Return<void> Sensors::poll(int32_t maxCount, poll_cb cb) {
    std::vector<Event> outBuffer;

    /*
     * This approach is borrowed from default implementation.
     * It's main intention to prevent undefined behaviour or dead-locks
     * when more then one client call this function.
     */
    std::unique_lock<std::mutex> lock(mPollLock, std::try_to_lock);
    if (!lock.owns_lock()) {
        ALOGE("Poll re-entry, killing myself!");
        ::exit(-1);
    }

#ifdef POLL_DEBUG
    ALOGD("Requested to get at most %d events for Android", maxCount);
#endif

    /* Check input values */
    if (maxCount < 1) {
        cb(Result::BAD_VALUE, hidl_vec<Event>(), hidl_vec<SensorInfo>());
        return Void();
    }

    /*
     * do-while loop is needed to prevent state, when after event processing there
     * is no valid event left in event buffers. It will cause the empty buffer returning.
     */
    do {
        /* Check the data availability in the buffer, if not - wait for it */
        std::unique_lock <std::mutex> condLock(mConditionMutex);
        mCondVarBufferReady.wait(condLock, [this] {return isEventsReady();});

        int eventsLeft = maxCount;
        OperationMode workMode = mMode;
        for (int i = 0; i < SensorIndex::COUNT; i++) {
#ifdef POLL_DEBUG
            ALOGD("Left to poll %d events", eventsLeft);
#endif
            int polledEvents = mSensors[i]->getReadyEvents(outBuffer, workMode,
                    eventsLeft);

            eventsLeft -= polledEvents;
            if (eventsLeft < 1)
                break;
        }

    } while (outBuffer.empty());
    lock.unlock();

    hidl_vec < Event > returnBuffer;
    returnBuffer.resize(outBuffer.size());
    for (unsigned long i = 0; i < outBuffer.size(); i++)
        returnBuffer[i] = outBuffer[i];

#ifdef POLL_DEBUG
    ALOGD("Reports %lu polled events", outBuffer.size());
#endif

    cb(Result::OK, returnBuffer, hidl_vec<SensorInfo>());

    return Void();
}

Return<Result> Sensors::batch(int32_t sensorHandle, int64_t samplingPeriodNs, int64_t argMaxReportLatencyNs)
{
    ALOGD("batch() - sampling period = %lu, ODR = %u, sensorHandle = %d, maxReportLatency = %ld",
        samplingPeriodNs, samplingPeriodNsToODR(samplingPeriodNs), sensorHandle, argMaxReportLatencyNs);

    if (!testHandle(sensorHandle))
        return Return<Result>(Result::BAD_VALUE);

    Return<Result> res(Result::OK);

    uint16_t currMaxODR = getMaxODRFromGroup(getGroupIndexByHandle(sensorHandle));

    res = mSensors[handleToIndex(sensorHandle)]->batch(samplingPeriodNs, argMaxReportLatencyNs);
    if (res != Result::OK)
        return res;

    uint16_t newMaxODR = getMaxODRFromGroup(getGroupIndexByHandle(sensorHandle));

    if (newMaxODR != currMaxODR) {
        ALOGD("Setting new trigger freq for %s = %u Hz",
            mSensorGroups[getGroupIndexByHandle(sensorHandle)].name.c_str(), newMaxODR);
        setTriggerFreq(newMaxODR, getGroupIndexByHandle(sensorHandle));
    }

    return res;
}

Return<Result> Sensors::flush(int32_t sensorHandle)
{
    if (testHandle(sensorHandle))
        return mSensors[handleToIndex(sensorHandle)]->flush();
    else
        return Return<Result>(Result::BAD_VALUE);
}


Return<Result> Sensors::injectSensorData(const Event& event)
{
    if ((mMode != OperationMode::DATA_INJECTION) ||
        (SensorType::ADDITIONAL_INFO == event.sensorType)) {
        ALOGE("Try to inject data ADDITIONAL_INFO or event not in inject mode");
        return Return<Result>(Result::INVALID_OPERATION);
    }

    if (!testHandle(event.sensorHandle)) {
        ALOGE("Invalid handler passed to %s", __func__);
        return Return<Result>(Result::BAD_VALUE);
    }

#ifdef POLL_DEBUG
    ALOGD("Injecting event to buffer (x = %f y = %f z = %f) type = %d, ",
        event.u.vec3.x, event.u.vec3.y, event.u.vec3.z, event.sensorType);
#endif

    mSensors[handleToIndex(event.sensorHandle)]->injectEvent(event);

    return Return<Result>(Result::OK);
}

/*
 * Current implementation doesn't support DirectChannel mode.
 */
Return<void> Sensors::registerDirectChannel(const SharedMemInfo&, registerDirectChannel_cb cb)
{
    for (size_t i = 0; i < SensorIndex::COUNT; ++i) {
        cb (Result::INVALID_OPERATION, mSensors[i]->getSensorInfo().sensorHandle);
    }

    return Void();
}

/*
 * Current implementation doesn't support DirectChannel mode.
 */
Return<Result> Sensors::unregisterDirectChannel(int32_t)
{
    return Return<Result>(Result::OK);
}

/*
 * Current implementation doesn't support DirectChannel mode.
 */
Return<void> Sensors::configDirectReport(int32_t, int32_t, RateLevel, configDirectReport_cb cb)
{
    cb(Result::INVALID_OPERATION, 0);

    return Void();
}

bool Sensors::isEventsReady()
{
    bool result = false;

    for (int i = 0; (i < SensorIndex::COUNT) && !result; i++) {
        result = mSensors[i]->isBufferReady(mMode);
    }

    return result;
}

void Sensors::fillGroups()
{
    for (size_t i = 0; i < mSensorDescriptors.size(); ++i) {
        for (size_t j = 0; j < mSensorGroups.size(); j++) {
            if (mSensorDescriptors[i].sensorGroupName == mSensorGroups[j].name) {
                mSensorGroups[j].sensorHandles.push_back(mSensorDescriptors[i].sensorInfo.sensorHandle);
            }
        }
    }
}

size_t  Sensors::getGroupIndexByHandle(uint32_t handeleIndex)
{
    size_t idx;

    for (idx = 0; idx < mSensorGroups.size(); ++idx) {
        auto it = std::find(mSensorGroups[idx].sensorHandles.begin(),
            mSensorGroups[idx].sensorHandles.end(), handeleIndex);
        if (it != mSensorGroups[idx].sensorHandles.end())
            return idx;
    }

    return idx;
}

void Sensors::activateGroup(bool enable, uint32_t index)
{
    const int value = enable ? 1 : 0;

    if(!fileWriteInt(mSensorGroups[index].bufferSwitchFileName, value)) {
        ALOGE("Failed to write to the %s", mSensorGroups[index].bufferSwitchFileName.c_str());
    }
}

bool Sensors::isActiveGroup(uint32_t index)
{
    for (size_t i = 0; i < mSensorGroups[index].sensorHandles.size(); ++i) {
        if (mSensors[handleToIndex(mSensorGroups[index].sensorHandles[i])]->isActive()) {;
            return true;
        }
    }

    return false;
}

bool Sensors::setTriggerFreq(uint16_t requestedODR, uint32_t index)
{
    if(!fileWriteInt(mSensorGroups[index].triggerFileName, requestedODR)) {
        ALOGE("Failed to set ODR = %d Hz", requestedODR);
        return false;
    }

    mSensorGroups[index].currODR = requestedODR;

    return true;
}

void Sensors::activateAllGroups()
{
    for (size_t i = 0; i < mSensorGroups.size(); i++) {
        activateGroup(true, i);
        setTriggerFreq(getMaxODRFromGroup(i), i);
    }
}

uint16_t Sensors::getMaxODRFromGroup(uint32_t index)
{
    uint32_t sensor_handle = 0;
    uint16_t sensor_ODR = 0;
    uint16_t max_ODR = 0;

    for (size_t i = 0; i < mSensorGroups[index].sensorHandles.size(); i++) {
        sensor_handle = mSensorGroups[index].sensorHandles[i];
        sensor_ODR = mSensors[handleToIndex(sensor_handle)]->getODR();
        if (sensor_ODR > max_ODR)
            max_ODR = sensor_ODR;
    }

    return max_ODR;
}

void Sensors::openFileDescriptors()
{
    for (size_t i = 0; i < mSensorGroups.size(); i++) {
        mSensorGroups[i].fd = open(mSensorGroups[i].deviceFileName.c_str(), O_RDONLY);
        if(mSensorGroups[i].fd == -1) {
            ALOGE("Failed to open %s buffer file", mSensorGroups[i].deviceFileName.c_str());
        }
    }
}

void Sensors::closeFileDescriptors()
{
    for (size_t i = 0; i < mSensorGroups.size(); i++)
        close(mSensorGroups[i].fd);
}

}  // namespace kingfisher
}  // namespace V1_0
}  // namespace sensors
}  // namespace hardware
}  // namespace android
