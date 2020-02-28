#ifndef ANDROID_HARDWARE_GRAVITY_SENSOR_V2_0_KINGFISHER_H
#define ANDROID_HARDWARE_GRAVITY_SENSOR_V2_0_KINGFISHER_H

#include <android/hardware/sensors/1.0/ISensors.h>
#include <string>

#include "SensorDescriptors.h"
#include "common.h"
#include "BaseSensor.h"
#include "FusionSensor.h"
#include "VirtualSensor.h"

namespace android {
namespace hardware {
namespace sensors {
namespace V2_0 {
namespace kingfisher {

using ::android::hardware::sensors::V1_0::Event;
using ::android::hardware::sensors::V1_0::OperationMode;
using ::android::hardware::sensors::V1_0::Result;
using ::android::hardware::sensors::V1_0::SensorInfo;

class GravitySensor : public VirtualSensor
{
    public:
        explicit GravitySensor(const SensorDescriptor&, FusionSensor&);

    protected:
        int process(const std::vector<FusionData>&, std::vector<Event>&) override;
        void preActivateActions() override { };
};

}  // namespace kingfisher
}  // namespace V2_0
}  // namespace sensors
}  // namespace hardware
}  // namespace android

#endif//ANDROID_HARDWARE_GRAVITY_SENSOR_V2_0_KINGFISHER_H