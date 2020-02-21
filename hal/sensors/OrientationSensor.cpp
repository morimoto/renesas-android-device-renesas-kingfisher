#include <iterator>
#include <algorithm>
#include "OrientationSensor.h"

namespace android {
namespace hardware {
namespace sensors {
namespace V2_0 {
namespace kingfisher {

using ::android::hardware::sensors::V1_0::SensorStatus;

OrientationSensor::OrientationSensor(const SensorDescriptor &sensorDescriptor, FusionSensor &fusionSensor) :
        VirtualSensor(sensorDescriptor, fusionSensor),
        mAngleX(0.0), mAngleY(0.0), mAngleZ(0.0)
{
}

void OrientationSensor::preActivateActions()
{
    mAngleX = 0;
    mAngleX = 0;
    mAngleZ = 0;
}

int OrientationSensor::process(const std::vector<FusionData> &fusionData, std::vector<Event> &events)
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

        float xAccel = fusionData[i].accelEvent.u.vec3.x;
        float yAccel = fusionData[i].accelEvent.u.vec3.y;
        float zAccel = fusionData[i].accelEvent.u.vec3.z;

        float xMagn = fusionData[i].magEvent.u.vec3.x;
        float yMagn = fusionData[i].magEvent.u.vec3.y;
        float zMagn = fusionData[i].magEvent.u.vec3.z;

        mAngleX = std::atan2(yAccel, zAccel); //phi
        mAngleY = std::atan2(-xAccel, yAccel * sin(mAngleX) + zAccel * cos(mAngleX)); //theta
        mAngleZ = atan2(zMagn * sin(mAngleX) - yMagn * cos(mAngleX), xMagn * cos(mAngleY) +
            yMagn * sin(mAngleX) * sin(mAngleY) + zMagn * sin(mAngleY) * cos(mAngleX)); //psi

        newEvent.u.vec3.x = mAngleX * 180 / M_PI;
        newEvent.u.vec3.y = mAngleY * 180 / M_PI;
        newEvent.u.vec3.z = mAngleZ * 180 / M_PI;
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
