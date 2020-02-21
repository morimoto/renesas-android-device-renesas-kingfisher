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
#include <android/hardware/sensors/2.0/types.h>
#include "SensorDescriptors.h"
#include "VirtualSensor.h"
#include "GravitySensor.h"
#include "RotationVector.h"
#include "GeoMagRotationVector.h"
#include "LinearAccelerationSensor.h"
#include "GameRotationSensor.h"
#include "OrientationSensor.h"

using namespace std::chrono_literals;

namespace android {
namespace hardware {
namespace sensors {
namespace V2_0 {
namespace kingfisher {

using ::android::hardware::sensors::V1_0::Event;
using ::android::hardware::sensors::V1_0::OperationMode;
using ::android::hardware::sensors::V1_0::RateLevel;
using ::android::hardware::sensors::V1_0::Result;
using ::android::hardware::sensors::V1_0::SharedMemInfo;
using ::android::hardware::sensors::V2_0::SensorTimeout;
using ::android::hardware::sensors::V2_0::WakeLockQueueFlagBits;

Sensors::Sensors()
    : mEventQueueFlag(nullptr),
      mPollThreadsStarted(false)
{
    mMode = OperationMode::NORMAL;
    mSensorDescriptors =
        std::vector<SensorDescriptor>(sensors_descriptors,
        sensors_descriptors + (sizeof(sensors_descriptors) / sizeof(sensors_descriptors[0])));
    mSensorGroups = sensorGroupDescriptors;

    fillGroups();

    /* Do not change the sensors push sequence! */
    /* Firstly, initialize hardware sensors, then virtual */
    mSensors.push_back(std::make_shared<IIOSensor>(mSensorDescriptors[SensorIndex::ACC]));
    mSensors.push_back(std::make_shared<IIOSensor>(mSensorDescriptors[SensorIndex::GYR]));
    mSensors.push_back(std::make_shared<IIOSensor>(mSensorDescriptors[SensorIndex::MAG]));
    mFusionSensor.addHwSensors(mSensors);

    mSensors.push_back(std::make_shared<GravitySensor>
        (mSensorDescriptors[SensorIndex::GRAV], mFusionSensor));

    mSensors.push_back(std::make_shared<RotationVector>
        (mSensorDescriptors[SensorIndex::ROTV], mFusionSensor));

    mSensors.push_back(std::make_shared<GeoMagRotationVector>
        (mSensorDescriptors[SensorIndex::GEOMAG], mFusionSensor));

    mSensors.push_back(std::make_shared<LinearAccelerationSensor>
        (mSensorDescriptors[SensorIndex::LINACC], mFusionSensor));
    mSensors.push_back(std::make_shared<GameRotationSensor>
        (mSensorDescriptors[SensorIndex::GAME], mFusionSensor));

    mSensors.push_back(std::make_shared<OrientationSensor>
        (mSensorDescriptors[SensorIndex::ORIENT], mFusionSensor));

    activateAllGroups();
    /* File descriptors must be opened before starting threads */
    openFileDescriptors();
}

Sensors::~Sensors()
{
    stopPollThreads();
    closeFileDescriptors();
}

void Sensors::deleteEventFlag()
{
    status_t status = EventFlag::deleteEventFlag(&mEventQueueFlag);
    if (status != OK) {
        ALOGI("Failed to delete event flag: %d", status);
    }
}

/*
 * Accelerometer and magnetometer need special preparation because they are
 * placed on the same address, other sensors should work correctly with
 * generic buffer
 */
void Sensors::parseBuffer(const IIOCombinedBuffer& from, IIOBuffer& to, uint32_t sensorHandle)
{
    switch (sensorHandle) {
        case HandleIndex::ACC_HANDLE: {
            to.coords = from.accelMagnBuf.accel;
            to.timestamp = from.accelMagnBuf.timestamp;
            break;
        }
        case HandleIndex::MAGN_HANDLE: {
            to.coords = from.accelMagnBuf.magn;
            to.timestamp = from.accelMagnBuf.timestamp;
            break;
        }
        default: {
            to.coords = from.genericBuf.coords;
            to.timestamp = from.genericBuf.timestamp;
            break;
        }
    }
    return;
}

/*
 * Generalized method to handle all sensors. Handles special case with LSM9DS0 -
 * accelerometer and magnetometer. They are placed at the same I2C address and
 * have same chrdev in /dev.
*/
void Sensors::pollIIODeviceGroupBuffer(uint32_t groupIndex)
{
    int retryCount = 0;
    std::vector<Event> outEvents;

    while(!mTerminatePollThreads.load()) {
        IIOCombinedBuffer rawBuffer;
        int readBytes = ::read(mSensorGroups[groupIndex].fd,
            &rawBuffer, mSensorGroups[groupIndex].bufSize);
        if (readBytes == mSensorGroups[groupIndex].bufSize) {
            IIOBuffer readBuffer;

            uint16_t maxODR = getMaxODRFromGroup(groupIndex);

            for (size_t i = 0; i < mSensorGroups[groupIndex].sensorHandles.size(); i++) {
                uint32_t sensorHandle = mSensorGroups[groupIndex].sensorHandles[i];
                uint32_t sensorIndex = handleToIndex(sensorHandle);
                uint16_t sensorODR = mSensors[sensorIndex]->getODR();

                mSensors[sensorIndex]->addTicks(sensorODR);
                if (mSensors[sensorIndex]->getTicks() >= maxODR) {
                    mSensors[sensorIndex]->resetTicks();
                    if (mSensors[sensorIndex]->isActive() ||
                        mSensors[sensorIndex]->hasActiveListeners()) {
                        parseBuffer(rawBuffer, readBuffer, sensorHandle);
                        mSensors[sensorIndex]->transformData(readBuffer);
                        mSensors[sensorIndex]->getReadyEvents(outEvents, mMode);

                        mFusionSensor.pushEvents(outEvents);
                        setReadyFlag(mSensors[sensorIndex]->getSensorType());
                        notifyListeners();
                    }
                }
                if (mSensors[sensorIndex]->isActive()) {
                    postEvents(outEvents);
                    outEvents.clear();
                }
            }
        } else {
            ALOGE("Failed to read data from %s buffer file",
                mSensorGroups[groupIndex].name.c_str());
            ALOGE("Expected %d bytes, actual %d",
                mSensorGroups[groupIndex].bufSize, readBytes);
            if(++retryCount > maxReadRetries)
                mTerminatePollThreads = true;
        }
    }
}

void Sensors::setReadyFlag(SensorType sensorType)
{
    if (sensorType == SensorType::ACCELEROMETER) {
        mAccelEventReady = true;
    } else if (sensorType == SensorType::MAGNETIC_FIELD) {
        mMagnEventReady = true;
    } else if (sensorType == SensorType::GYROSCOPE) {
        mGyroEventReady = true;
    }
}

void Sensors::resetReadyFlag(FUSION_MODE mode)
{
    mAccelEventReady = false;
    if (mode == FUSION_9AXIS) {
        mMagnEventReady = false;
        mGyroEventReady = false;
    } else if (mode == FUSION_NOMAG) {
        mGyroEventReady = false;
    } else if (mode == FUSION_NOGYRO) {
        mMagnEventReady = false;
    }
}

void Sensors::notifyListeners()
{
    bool isEventsProcessed = false;
    if (!mAccelEventReady.load())
        return;

    for (size_t i = 0; i < mSensorGroups.size(); i++) {
        if (!mSensorGroups[i].isVirtual) {
            continue;
        }

        uint32_t sensorHandle = mSensorGroups[i].sensorHandles[0];
        uint32_t sensorIndex = handleToIndex(sensorHandle);

        if (mSensorGroups[i].mode == FUSION_9AXIS &&
            mMagnEventReady.load() && mGyroEventReady.load()) {
            mSensors[sensorIndex]->notifyEventsReady();
            isEventsProcessed = true;
        } else if (mSensorGroups[i].mode == FUSION_NOMAG &&
            mGyroEventReady.load()) {
            mSensors[sensorIndex]->notifyEventsReady();
            isEventsProcessed = true;
        } else if (mSensorGroups[i].mode == FUSION_NOGYRO &&
            mMagnEventReady.load()) {
            mSensors[sensorIndex]->notifyEventsReady();
            isEventsProcessed = true;
        }
    }

    if (isEventsProcessed) {
        for (size_t i = 0; i < mSensorGroups.size(); i++) {
            resetReadyFlag(mSensorGroups[i].mode);
        }
    }
}

void Sensors::pollVirtualDeviceGroupBuffer(uint32_t groupIndex)
{
    std::vector<Event> outEvents;

    while(!mTerminatePollThreads.load()) {
        for (size_t i = 0; i < mSensorGroups[groupIndex].sensorHandles.size(); i++) {

            uint32_t sensorHandle = mSensorGroups[groupIndex].sensorHandles[i];
            uint32_t sensorIndex = handleToIndex(sensorHandle);
            mSensors[sensorIndex]->getReadyEvents(outEvents, mMode);
        }

        postEvents(outEvents);
        outEvents.clear();
    }
}

void Sensors::startPollThreads()
{
    for (size_t i = 0; i < mSensorGroups.size(); i++) {
        if (mSensorGroups[i].isVirtual) {
            mSensorGroups[i].thread = std::make_shared<std::thread>(&Sensors::pollVirtualDeviceGroupBuffer, this, i);
        } else {
            if (mSensorGroups[i].fd >= 0) {
                mSensorGroups[i].thread = std::make_shared<std::thread>(&Sensors::pollIIODeviceGroupBuffer, this, i);
            } else {
                ALOGE("Failed to start poll thread for %s", mSensorGroups[i].name.c_str());
            }
        }
    }
}

void Sensors::stopPollThreads()
{
    mTerminatePollThreads = true;

    for (size_t i = 0; i < mSensorGroups.size(); i++) {
        if (mSensorGroups[i].thread->joinable())
            mSensorGroups[i].thread->join();
    }
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

/*
 *  v2.0 required interface based on default implementation.
 */
Return<Result> Sensors::initialize(const ::android::hardware::MQDescriptorSync<Event>& eventQueueDescriptor,
                                   const ::android::hardware::MQDescriptorSync<uint32_t>& wakeLockDescriptor,
                                   const sp<ISensorsCallback>&)
{
    Result result = Result::OK;

    for(const auto& sensor: mSensors) {
        sensor->activate(false);
    }

    mEventQueue = std::make_unique<EventMessageQueue>(eventQueueDescriptor, true /* resetPointers */);
    deleteEventFlag();

    if (EventFlag::createEventFlag(mEventQueue->getEventFlagWord(), &mEventQueueFlag) != OK) {
        result = Result::BAD_VALUE;
        return result;
    }
    /*
     * None of our sensors are of WakeUp type so this queue will be empty all
     * the time and no methods to work with it are required.
     */
    mWakeLockQueue = std::make_unique<WakeLockMessageQueue>(wakeLockDescriptor, true);

    if (!mEventQueue || !mWakeLockQueue || !mEventQueueFlag) {
        result = Result::BAD_VALUE;
        return result;
    }

    for(const auto& sensor: mSensors) {
        sensor->activate(true);
    }

    if (!mPollThreadsStarted.load()) {
        startPollThreads();
        mPollThreadsStarted = true;
    }

    return result;
}

void Sensors::postEvents(const std::vector<Event>& events)
{
    std::lock_guard<std::mutex> lock(mWriteLock);
    if (mEventQueue->write(events.data(), events.size())) {
        mEventQueueFlag->wake(static_cast<uint32_t>(EventQueueFlagBits::READ_AND_PROCESS));
    }
}


Return<Result> Sensors::HWBatch(int32_t sensorHandle, int64_t samplingPeriodNs, int64_t argMaxReportLatencyNs)
{
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
Return<Result> Sensors::virtualBatch(int32_t sensorHandle, int64_t samplingPeriodNs, int64_t argMaxReportLatencyNs)
{
    int index = getGroupIndexByHandle(sensorHandle);
    Return<Result> res(Result::OK);
    /* Enable or disable hardware sensors */
    res = HWBatch(HandleIndex::ACC_HANDLE, samplingPeriodNs, argMaxReportLatencyNs);
    if (res != Result::OK)
        return res;
    if (mSensorGroups[index].mode != FUSION_NOMAG) {
        res = HWBatch(HandleIndex::MAGN_HANDLE, samplingPeriodNs, argMaxReportLatencyNs);
        if (res != Result::OK)
            return res;
    }
    if (mSensorGroups[index].mode != FUSION_NOGYRO) {
        res = HWBatch(HandleIndex::GYRO_HANDLE, samplingPeriodNs, argMaxReportLatencyNs);
        if (res != Result::OK)
            return res;
    }
    return res;
}

Return<Result> Sensors::batch(int32_t sensorHandle, int64_t samplingPeriodNs, int64_t argMaxReportLatencyNs)
{
    ALOGD("batch() - sampling period = %lu, ODR = %u, sensorHandle = %d, maxReportLatency = %ld",
        samplingPeriodNs, samplingPeriodNsToODR(samplingPeriodNs), sensorHandle, argMaxReportLatencyNs);

    if (!testHandle(sensorHandle))
        return Return<Result>(Result::BAD_VALUE);

    int index = getGroupIndexByHandle(sensorHandle);
    if (mSensorGroups[index].isVirtual) {
        return virtualBatch(sensorHandle, samplingPeriodNs, argMaxReportLatencyNs);
    } else {
        return HWBatch(sensorHandle, samplingPeriodNs, argMaxReportLatencyNs);
    }

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

void Sensors::fillGroups()
{
    for (size_t i = 0; i < mSensorDescriptors.size(); ++i) {
        for (size_t j = 0; j < mSensorGroups.size(); j++) {
            if (mSensorDescriptors[i].sensorGroupName == mSensorGroups[j].name) {
                mSensorGroups[j].sensorHandles.push_back(mSensorDescriptors[i].sensorInfo.sensorHandle);
                mSensorDescriptors[i].sensorGroup = std::make_shared<SensorsGroupDescriptor>(mSensorGroups[j]);
            }
        }
    }
}

size_t Sensors::getGroupIndexByHandle(uint32_t handleIndex)
{
    size_t idx;

    for (idx = 0; idx < mSensorGroups.size(); ++idx) {
        auto it = std::find(mSensorGroups[idx].sensorHandles.begin(),
            mSensorGroups[idx].sensorHandles.end(), handleIndex);
        if (it != mSensorGroups[idx].sensorHandles.end())
            return idx;
    }

    return idx;
}

void Sensors::activateGroup(bool enable, uint32_t index)
{
    const int value = enable ? 1 : 0;

    if (mSensorGroups[index].isVirtual) {
        /* Enable or disable hardware sensors */
        activate(HandleIndex::ACC_HANDLE, enable);
        if (mSensorGroups[index].mode != FUSION_NOMAG) {
            activate(HandleIndex::MAGN_HANDLE, enable);
        }

        if (mSensorGroups[index].mode != FUSION_NOGYRO) {
            activate(HandleIndex::GYRO_HANDLE, enable);
        }
    } else if (!fileWriteInt(mSensorGroups[index].bufferSwitchFileName, value)) {
        ALOGE("Failed to write to the %s", mSensorGroups[index].bufferSwitchFileName.c_str());
    }
}

bool Sensors::isActiveGroup(uint32_t index)
{
    for (size_t i = 0; i < mSensorGroups[index].sensorHandles.size(); ++i) {
        if (mSensors[handleToIndex(mSensorGroups[index].sensorHandles[i])]->isActive() ||
            mSensors[handleToIndex(mSensorGroups[index].sensorHandles[i])]->hasActiveListeners()) {
            return true;
        }
    }

    return false;
}

bool Sensors::setTriggerFreq(uint16_t requestedODR, uint32_t index)
{
    if (mSensorGroups[index].isVirtual)
        return true;

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
    for (size_t i = 0; i < mSensorGroups.size(); i++) {
        close(mSensorGroups[i].fd);
    }
}

}  // namespace kingfisher
}  // namespace V2_0
}  // namespace sensors
}  // namespace hardware
}  // namespace android
