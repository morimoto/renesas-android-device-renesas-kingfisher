#ifndef ANDROID_HARDWARE_BASE_SENSOR_V2_0_KINGFISHER_H
#define ANDROID_HARDWARE_BASE_SENSOR_V2_0_KINGFISHER_H

#include <android/hardware/sensors/1.0/ISensors.h>
#include <queue>
#include <iostream>
#include <fstream>
#include <string>
#include <utils/SystemClock.h>

#include "SensorDescriptors.h"
#include "common.h"

namespace android {
namespace hardware {
namespace sensors {
namespace V2_0 {
namespace kingfisher {

using ::android::hardware::sensors::V1_0::Event;
using ::android::hardware::sensors::V1_0::OperationMode;
using ::android::hardware::sensors::V1_0::Result;
using ::android::hardware::sensors::V1_0::SensorInfo;
using ::android::hardware::sensors::V1_0::AdditionalInfoType;
using ::android::hardware::sensors::V1_0::AdditionalInfo;
using ::android::hardware::sensors::V1_0::MetaDataEventType;

class BaseSensor
{
    public:
        explicit BaseSensor(const SensorDescriptor& sensorDescriptor) :
            mSensorDescriptor(sensorDescriptor),
            mIsEnabled(false) {};
        virtual ~BaseSensor() { };

        virtual Return<Result> activate(bool) = 0;
        virtual Return<Result> batch(int64_t, int64_t) = 0;
        virtual Return<Result> flush() = 0;
        virtual void transformData(const IIOBuffer&) = 0;
        virtual int getReadyEvents(std::vector<Event>&, OperationMode) = 0;
        virtual void injectEvent(const Event&) = 0;
        virtual void addVirtualListener(uint32_t sensorHandle) = 0;
        virtual void removeVirtualListener(uint32_t sensorHandle) = 0;
        virtual bool hasActiveListeners() const = 0;

        SensorType getSensorType() const { return mSensorDescriptor.sensorInfo.type; }
        bool isActive() const { return mIsEnabled.load(); }
        SensorInfo getSensorInfo() const { return mSensorDescriptor.sensorInfo; }
        uint32_t getTicks() const { return mTicks; }
        void addTicks(uint32_t ticks) { mTicks += ticks; }
        void resetTicks() { mTicks = 0; }
        void notifyEventsReady() { if (mIsEnabled.load()) { mNotifier.notify_one(); } };
        virtual uint16_t getODR() const { return mCurrODR.load(); };

    protected:
        std::vector<Event> generateAdditionalEvent();
        Event createFlushEvent();

        SensorDescriptor mSensorDescriptor;
        AtomicBuffer mInjectEventBuffer;
        std::atomic<bool> mIsEnabled = false;
        std::atomic<bool> mAdditionalInfoNeeded = false;
        uint32_t mTicks;
        /*This can be used from multiple threads*/
        std::condition_variable mNotifier;
        std::atomic<uint16_t> mCurrODR;
        float mTimestamp = 0;
};

}  // namespace kingfisher
}  // namespace V2_0
}  // namespace sensors
}  // namespace hardware
}  // namespace android

#endif//ANDROID_HARDWARE_BASE_SENSOR_V2_0_KINGFISHER_H
