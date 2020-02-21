#include "GameRotationSensor.h"

namespace android {
namespace hardware {
namespace sensors {
namespace V2_0 {
namespace kingfisher {

using ::android::hardware::sensors::V1_0::SensorStatus;

GameRotationSensor::GameRotationSensor(const SensorDescriptor &sensorDescriptor, FusionSensor &fusionSensor) :
        VirtualSensor(sensorDescriptor, fusionSensor),
        mAngleX(0.0), mAngleY(0.0), mAngleZ(0.0), mIsNeedInitPosition(false)
{
}

void GameRotationSensor::preActivateActions()
{
    mAngleX = 0;
    mAngleX = 0;
    mAngleZ = 0;
    mIsNeedInitPosition = true;
}

void GameRotationSensor::boundValues(float &value)
{
    if (value > M_PI) {
        value -= M_PI * 2.0;
    } else if (value < -M_PI) {
        value += M_PI * 2.0;
    }

    value /= M_PI;
}

int GameRotationSensor::process(const std::vector<FusionData> &fusionData, std::vector<Event> &events)
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

        float xGyro = fusionData[i].gyroEvent.u.vec3.x;
        float yGyro = fusionData[i].gyroEvent.u.vec3.y;
        float zGyro = fusionData[i].gyroEvent.u.vec3.z;

        float xAccelAngle = std::atan(xAccel / std::sqrt((yAccel * yAccel + zAccel * zAccel)));
        float yAccelAngle = std::atan(yAccel / std::sqrt((xAccel * xAccel + zAccel * zAccel)));
        float zAccelAngle = std::atan(zAccel / std::sqrt((xAccel * xAccel + yAccel * yAccel)));

        if (mIsNeedInitPosition) {
            mAngleX = xAccelAngle;
            mAngleY = yAccelAngle;
            mAngleZ = 0.0;
            mIsNeedInitPosition = false;
        }

        newEvent.timestamp = mTimestamp;

        /* Accumulate gyro value should be multiplied by time difference between the current and the last events. */
        if (std::fabs(xGyro) > THRESHOLD) {
            mAngleX += xGyro * (dt / NSEC);
        }

        if (std::fabs(yGyro) > THRESHOLD) {
            mAngleY += yGyro * (dt / NSEC);
        }

        if (std::fabs(zGyro) > THRESHOLD) {
            mAngleZ += zGyro * (dt / NSEC);
        }

        /* Simple complementary filter */
        newEvent.u.vec3.x = ALPHA * (mAngleX) + (1 - ALPHA) * xAccelAngle;
        newEvent.u.vec3.y = ALPHA * (mAngleY) + (1 - ALPHA) * yAccelAngle;
        newEvent.u.vec3.z = ALPHA * (mAngleZ) + (1 - ALPHA) * zAccelAngle;

        boundValues(newEvent.u.vec3.x);
        boundValues(newEvent.u.vec3.y);
        boundValues(newEvent.u.vec3.z);

        events.push_back(newEvent);
    }

    return events.size();
}

}  // namespace kingfisher
}  // namespace V2_0
}  // namespace sensors
}  // namespace hardware
}  // namespace android
