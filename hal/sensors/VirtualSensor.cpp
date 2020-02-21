#include <android/hardware/sensors/1.0/ISensors.h>
#include <queue>
#include <iostream>
#include <fstream>
#include <string>

#include "SensorDescriptors.h"
#include "common.h"
#include "VirtualSensor.h"

namespace android {
namespace hardware {
namespace sensors {
namespace V2_0 {
namespace kingfisher {

using ::android::hardware::Return;
using ::android::hardware::sensors::V1_0::Result;
using ::android::hardware::sensors::V1_0::SensorStatus;
using ::android::hardware::sensors::V1_0::MetaDataEventType;
using ::android::hardware::sensors::V1_0::AdditionalInfoType;
using ::android::hardware::sensors::V1_0::AdditionalInfo;

VirtualSensor::VirtualSensor(const SensorDescriptor &sensorDescriptor, FusionSensor &fusionSensor) :
        BaseSensor(sensorDescriptor),
        mFusionSensor(fusionSensor)
{
    if (sensorDescriptor.maxODR > 0 ) {
        mSensorDescriptor.sensorInfo.minDelay = USEC / sensorDescriptor.maxODR;
    } else {
        //Fallback
        mSensorDescriptor.sensorInfo.minDelay = USEC / mSensorDescriptor.defaultODR;
    }

    if (mSensorDescriptor.minODR > 0) {
        mSensorDescriptor.sensorInfo.maxDelay = USEC / sensorDescriptor.minODR;
    } else {
        //Fallback
        mSensorDescriptor.sensorInfo.maxDelay = USEC / mSensorDescriptor.defaultODR;
    }
    mFusionMode = sensorDescriptor.sensorGroup->mode;
#ifdef POLL_DEBUG
    mCounter = 0;
#endif
}

Return<Result> VirtualSensor::flush()
{
    if (mIsEnabled.load()) {
        mNeedFlush = true;
        mAdditionalInfoNeeded = true;
        mNotifier.notify_one();

        return Return<Result>(Result::OK);
    } else {
        return Return<Result>(Result::BAD_VALUE);
    }
}

Return<Result> VirtualSensor::batch(int64_t delay_ns, int64_t)
{
    mFusionSensor.batch(mFusionMode, delay_ns);
    mCurrODR = samplingPeriodNsToODR(delay_ns);
    return Return<Result>(Result::OK);
}

Return<Result> VirtualSensor::activate(bool enable)
{
    preActivateActions();
    if (enable && !mIsEnabled) {
        mAdditionalInfoNeeded = true;
        mNotifier.notify_one();
    }

    mIsEnabled = enable;
    mTimestamp = ::android::elapsedRealtimeNano();
    mFusionSensor.activate(mFusionMode, mSensorDescriptor.sensorInfo.sensorHandle, enable);
    ALOGD("%s %s", enable ? "Activating" : "Deactivating",
        mSensorDescriptor.sensorInfo.name.c_str());
    return Return<Result>(Result::OK);
}

int VirtualSensor::getReadyEvents(std::vector<Event> &events, OperationMode mode)
{
    int eventCount = 0;
    std::mutex mutex;
    std::unique_lock<std::mutex> lock(mutex);
    int minRealODR;

    mNotifier.wait(lock);

    if(mAdditionalInfoNeeded.load()) {
        std::lock_guard<std::mutex> bufferLock(mBufferLock);
        std::vector<Event> additionalInfo = generateAdditionalEvent();

        if (mNeedFlush.load()) {
            mNeedFlush = false;
            events.push_back(createFlushEvent());
        }

        for (size_t i = 0; i < additionalInfo.size(); ++i) {
            events.push_back(additionalInfo[i]);
        }

        mAdditionalInfoNeeded = false;
    }

    switch (mode) {
        case OperationMode::NORMAL:
        {

            std::lock_guard<std::mutex> bufferLock(mBufferLock);
            minRealODR = mFusionSensor.getMinODR(mFusionMode);
            std::vector<FusionData> fusionData = mFusionSensor.getFusionEvents(mFusionMode);
            if (mCurrODR > minRealODR) {
            // +1 Here to ensure that float value will be rounded upwards when casted to int
                fusionData = LERP(fusionData, (mCurrODR/minRealODR) + 1);
            }

            eventCount = process(fusionData, events);

#ifdef POLL_DEBUG
            for (size_t i = 0; i < events.size(); ++i) {
                ALOGD(" ===== > Event from %d sensor (count = %u): \
                x = %f, y = %f, z = %f, norm = %f, Rawtimestamp = %lu",
                    events[i].sensorHandle, mCounter, events[i].u.vec3.x,
                    events[i].u.vec3.y, events[i].u.vec3.z,
                    std::sqrt(events[i].u.vec3.x * events[i].u.vec3.x + 
                    events[i].u.vec3.y * events[i].u.vec3.y +
                    events[i].u.vec3.z * events[i].u.vec3.z),
                    events[i].timestamp);
                mCounter++;
            }
#endif

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

void VirtualSensor::injectEvent(const Event& event)
{
    std::lock_guard<std::mutex> injectLock(mInjectEventBuffer.mutex);

    /* Remove first event from queue and place new to the end */
    if(mInjectEventBuffer.eventBuffer.size() >= mInjectEventBuffer.maxSize)
        mInjectEventBuffer.eventBuffer.pop();

    mInjectEventBuffer.eventBuffer.push(event);
}

std::vector<FusionData> VirtualSensor::LERP(std::vector<FusionData>& events, int multiplier)
{
    // Increment multiplier to prevent last interpolated event being the same as
    // next HW event and distribute events more evenly
    // in other words shift interpolated values to the left
    // do         |----x----x----|
    //			 ^  ^    ^ 
    //			 |  |    └ HW Event
    //			 |  └ Interpolated event
    //			 └ Timeline
    // instead of |-------x-----x|
    multiplier++;
    std::vector<FusionData> newEvents;
    if (events.size() == 0) {
        return newEvents;
    }
    for (unsigned long i = 0; i < events.size() - 1; i++) {
        newEvents.push_back(events[i]);
        for(int j = 1; j < multiplier; j++) {
            FusionData temp;
            temp.accelEvent = lerpEvent(events[i].accelEvent,
                    events[i+1].accelEvent, (float) j / multiplier);

            if (mFusionMode != FUSION_NOMAG) {
                temp.magEvent = lerpEvent(events[i].magEvent,
                        events[i+1].magEvent, (float) j / multiplier);
            }

            if (mFusionMode != FUSION_NOGYRO) {
                temp.gyroEvent = lerpEvent(events[i].gyroEvent,
                        events[i+1].gyroEvent, (float) j / multiplier);
            }
            newEvents.push_back(temp);
        }
    }
    return newEvents;
}

Event VirtualSensor::lerpEvent(Event& e1, Event& e2, float factor){
    Event newEvent;
    newEvent.timestamp = e1.timestamp + ((e2.timestamp - e1.timestamp) * factor);
    newEvent.u.vec3.x = e1.u.vec3.x + (e2.u.vec3.x - e1.u.vec3.x) * factor;
    newEvent.u.vec3.y = e1.u.vec3.y + (e2.u.vec3.y - e1.u.vec3.y) * factor;
    newEvent.u.vec3.z = e1.u.vec3.z + (e2.u.vec3.z - e1.u.vec3.z) * factor;
    return newEvent;
}

}  // namespace kingfisher
}  // namespace V2_0
}  // namespace sensors
}  // namespace hardware
}  // namespace android
