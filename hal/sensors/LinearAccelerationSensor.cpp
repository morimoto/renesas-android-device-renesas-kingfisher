#include "LinearAccelerationSensor.h"

namespace android {
namespace hardware {
namespace sensors {
namespace V2_0 {
namespace kingfisher {

using ::android::hardware::sensors::V1_0::SensorStatus;

LinearAccelerationSensor::LinearAccelerationSensor(const SensorDescriptor &sensorDescriptor, FusionSensor &fusionSensor) :
        VirtualSensor(sensorDescriptor, fusionSensor),
        mGravityX(0.0), mGravityY(0.0), mGravityZ(0.0)
{
}

int LinearAccelerationSensor::process(const std::vector<FusionData> &fusionData, std::vector<Event> &events)
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

        // Isolate the force of gravity with the low-pass filter.
        mGravityX = ALPHA * mGravityX + (1 - ALPHA) * newEvent.u.vec3.x;
        mGravityY = ALPHA * mGravityY + (1 - ALPHA) * newEvent.u.vec3.y;
        mGravityZ = ALPHA * mGravityZ + (1 - ALPHA) * newEvent.u.vec3.z;

        // Remove the gravity contribution.
        newEvent.u.vec3.x = newEvent.u.vec3.x - mGravityX;
        newEvent.u.vec3.y = newEvent.u.vec3.y - mGravityY;
        newEvent.u.vec3.z = newEvent.u.vec3.z - mGravityZ;
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
