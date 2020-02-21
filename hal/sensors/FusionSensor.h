#ifndef ANDROID_HARDWARE_FUSION_SENSOR_V2_0_KINGFISHER_H
#define ANDROID_HARDWARE_FUSION_SENSOR_V2_0_KINGFISHER_H

#include <android/hardware/sensors/1.0/ISensors.h>
#include <string>
#include <list>

#include "SensorDescriptors.h"
#include "common.h"
#include "IIOSensor.h"

namespace android {
namespace hardware {
namespace sensors {
namespace V2_0 {
namespace kingfisher {

using ::android::hardware::sensors::V1_0::Event;
using ::android::hardware::sensors::V1_0::OperationMode;
using ::android::hardware::sensors::V1_0::Result;
using ::android::hardware::sensors::V1_0::SensorInfo;

struct FusionData
{
    Event accelEvent;
    Event gyroEvent;
    Event magEvent;
};

class FusionSensor
{
    public:
        void addHwSensors(std::vector<std::shared_ptr<BaseSensor>>&);
        void activate(FUSION_MODE, uint32_t, bool);
        void batch(FUSION_MODE, uint64_t);
        void pushEvents(const std::vector<Event>&);
        int getMinODR(FUSION_MODE);
        std::vector<FusionData> getFusionEvents(FUSION_MODE);

    private:
        std::shared_ptr<BaseSensor> mAcc;
        std::shared_ptr<BaseSensor> mMag;
        std::shared_ptr<BaseSensor> mGyro;
        std::mutex mBufferLock;
        static const size_t mMaxFusionData = 20;
        std::list<Event> mCurrentAccelEvents;
        std::list<Event> mCurrentGyroEvents;
        std::list<Event> mCurrentMagnEvents;
};

}  // namespace kingfisher
}  // namespace V2_0
}  // namespace sensors
}  // namespace hardware
}  // namespace android

#endif//ANDROID_HARDWARE_FUSION_SENSOR_V2_0_KINGFISHER_H
