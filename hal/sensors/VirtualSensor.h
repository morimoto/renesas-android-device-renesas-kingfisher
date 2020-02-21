#ifndef ANDROID_HARDWARE_VIRTUAL_SENSOR_V2_0_KINGFISHER_H
#define ANDROID_HARDWARE_VIRTUAL_SENSOR_V2_0_KINGFISHER_H

#include <android/hardware/sensors/1.0/ISensors.h>
#include <queue>
#include <iostream>
#include <fstream>
#include <string>

#include "SensorDescriptors.h"
#include "common.h"
#include "BaseSensor.h"
#include "FusionSensor.h"

namespace android {
namespace hardware {
namespace sensors {
namespace V2_0 {
namespace kingfisher {

using ::android::hardware::sensors::V1_0::Event;
using ::android::hardware::sensors::V1_0::OperationMode;
using ::android::hardware::sensors::V1_0::Result;
using ::android::hardware::sensors::V1_0::SensorInfo;

class VirtualSensor : public BaseSensor
{
    public:
        explicit VirtualSensor(const SensorDescriptor&, FusionSensor&);

        Return<Result> activate(bool) override;
        Return<Result> batch(int64_t, int64_t) override;
        Return<Result> flush() override;
        void transformData(const IIOBuffer&) override { };
        uint16_t getODR() const override { return 0; };
        int getReadyEvents(std::vector<Event>&, OperationMode) override;
        void injectEvent(const Event&) override;

        void addVirtualListener(uint32_t) override { };
        void removeVirtualListener(uint32_t) override { };
        bool hasActiveListeners() const override { return false; };

    protected:
        virtual int process(const std::vector<FusionData>&, std::vector<Event>&) = 0;
        virtual void preActivateActions() = 0;
        std::vector<FusionData> LERP(std::vector<FusionData>&, int mul);
        Event lerpEvent(Event& e1, Event& e2, float factor);

        FusionSensor &mFusionSensor;
        FUSION_MODE mFusionMode;
        std::mutex mBufferLock;
        std::atomic<bool> mNeedFlush = false;
#ifdef POLL_DEBUG
        uint32_t mCounter;
#endif
};

}  // namespace kingfisher
}  // namespace V2_0
}  // namespace sensors
}  // namespace hardware
}  // namespace android

#endif//ANDROID_HARDWARE_VIRTUAL_SENSOR_V2_0_KINGFISHER_H
