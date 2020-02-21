#include <iterator>
#include <algorithm>
#include "GravitySensor.h"

namespace android {
namespace hardware {
namespace sensors {
namespace V2_0 {
namespace kingfisher {

using ::android::hardware::sensors::V1_0::SensorStatus;

GravitySensor::GravitySensor(const SensorDescriptor &sensorDescriptor, FusionSensor &fusionSensor) :
        VirtualSensor(sensorDescriptor, fusionSensor)
{
}

int GravitySensor::process(const std::vector<FusionData> &fusionData, std::vector<Event> &events)
{
    Event newEvent;

    for (size_t i = 0; i < fusionData.size(); i++) {
        float dt = fusionData[i].gyroEvent.timestamp - mTimestamp;

        if (mTimestamp >= fusionData[i].gyroEvent.timestamp) {
            continue;
        }

        if (dt > NSEC) {
            mTimestamp = fusionData[i].gyroEvent.timestamp;
            continue;
        }
        mTimestamp = fusionData[i].gyroEvent.timestamp;

        newEvent = fusionData[i].accelEvent;
        newEvent.sensorType = mSensorDescriptor.sensorInfo.type;
        newEvent.u.vec3.status = SensorStatus::ACCURACY_HIGH;
        newEvent.sensorHandle = mSensorDescriptor.sensorInfo.sensorHandle;
        newEvent.timestamp = mTimestamp;

        events.push_back(newEvent);
    }

    return events.size();
}

}  // namespace kingfisher
}  // namespace V2_0
}  // namespace sensors
}  // namespace hardware
}  // namespace android
