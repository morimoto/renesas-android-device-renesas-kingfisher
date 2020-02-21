#include "GeoMagRotationVector.h"

namespace android {
namespace hardware {
namespace sensors {
namespace V2_0 {
namespace kingfisher {

using ::android::hardware::sensors::V1_0::SensorStatus;

GeoMagRotationVector::GeoMagRotationVector(const SensorDescriptor &sensorDescriptor, FusionSensor &fusionSensor) :
        VirtualSensor(sensorDescriptor, fusionSensor)
{
}

int GeoMagRotationVector::process(const std::vector<FusionData> &fusionData, std::vector<Event> &events)
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

        float xAccel = fusionData[i].accelEvent.u.vec3.x;
        float yAccel = fusionData[i].accelEvent.u.vec3.y;
        float zAccel = fusionData[i].accelEvent.u.vec3.z;

        float xMagn = fusionData[i].magEvent.u.vec3.x;
        float yMagn = fusionData[i].magEvent.u.vec3.y;
        float zMagn = fusionData[i].magEvent.u.vec3.z;

        float roll =  std::atan2(yAccel, zAccel); //phi
        float pitch = std::atan2(-xAccel, yAccel*sin(roll) + zAccel * cos(roll)); //theta
        float yaw = atan2(zMagn*sin(roll) - yMagn*cos(roll), xMagn*cos(pitch) + yMagn*sin(roll)*sin(pitch) + zMagn*sin(pitch)*cos(roll)); //psi

        newEvent.u.data[0] = roll / M_PI;
        newEvent.u.data[1] = pitch / M_PI;
        newEvent.u.data[2] = yaw / M_PI;

        newEvent.u.data[4] = 0;

        events.push_back(newEvent);
    }

    return events.size();
}

}  // namespace kingfisher
}  // namespace V2_0
}  // namespace sensors
}  // namespace hardware
}  // namespace android
