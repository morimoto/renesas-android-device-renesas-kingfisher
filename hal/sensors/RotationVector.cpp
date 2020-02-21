#include <iterator>
#include <algorithm>
#include "RotationVector.h"

namespace android {
namespace hardware {
namespace sensors {
namespace V2_0 {
namespace kingfisher {

using ::android::hardware::sensors::V1_0::SensorStatus;

RotationVector::RotationVector(const SensorDescriptor &sensorDescriptor, FusionSensor &fusionSensor) :
        VirtualSensor(sensorDescriptor, fusionSensor)
{
}

void RotationVector::preActivateActions()
{
    mCurrAngleX = 0;
    mCurrAngleY = 0;
    mCurrAngleZ = 0;
    mIsNeedInitPosition = true;
}

void RotationVector::boundValues(float &value)
{
    if (value > M_PI) {
        value -= M_PI * 2.0;
    } else if (value < -M_PI) {
        value += M_PI * 2.0;
    }

    value /= M_PI;
}

int RotationVector::process(const std::vector<FusionData> &fusionData, std::vector<Event> &events)
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

        newEvent = fusionData[i].gyroEvent;
        newEvent.sensorType = mSensorDescriptor.sensorInfo.type;
        newEvent.u.vec3.status = SensorStatus::ACCURACY_HIGH;
        newEvent.sensorHandle = mSensorDescriptor.sensorInfo.sensorHandle;

        float xAccel = fusionData[i].accelEvent.u.vec3.x;
        float yAccel = fusionData[i].accelEvent.u.vec3.y;
        float zAccel = fusionData[i].accelEvent.u.vec3.z;

        float xMagn = fusionData[i].magEvent.u.vec3.x;
        float yMagn = fusionData[i].magEvent.u.vec3.y;

        float xGyro = fusionData[i].gyroEvent.u.vec3.x;
        float yGyro = fusionData[i].gyroEvent.u.vec3.y;
        float zGyro = fusionData[i].gyroEvent.u.vec3.z;

        float xAccelAngle = std::atan(xAccel / std::sqrt((yAccel * yAccel + zAccel * zAccel)));
        float yAccelAngle = std::atan(yAccel / std::sqrt((xAccel * xAccel + zAccel * zAccel)));
        float zAccelAngle = std::atan(zAccel / std::sqrt((xAccel * xAccel + yAccel * yAccel)));

        float azimuth = std::atan2(yMagn, xMagn) - M_PI / 2.0;

        if (mIsNeedInitPosition) {
            mCurrAngleX = xAccelAngle;
            mCurrAngleY = yAccelAngle;
            mCurrAngleZ = azimuth;
            mIsNeedInitPosition = false;
        }

        newEvent.timestamp = mTimestamp;

        /* Accumulate gyro value should be multiplied by time difference between the current and the last events. */
        if (std::fabs(xGyro) > THRESHOLD) {
            mCurrAngleX += xGyro * (dt / NSEC);
        }

        if (std::fabs(yGyro) > THRESHOLD) {
            mCurrAngleY += yGyro * (dt / NSEC);
        }

        if (std::fabs(zGyro) > THRESHOLD) {
            mCurrAngleZ += zGyro * (dt / NSEC);
        }

        /* Simple complementary filter */
        newEvent.u.vec3.x = ALPHA * (mCurrAngleX) + (1 - ALPHA) * xAccelAngle;
        newEvent.u.vec3.y = ALPHA * (mCurrAngleY) + (1 - ALPHA) * yAccelAngle;
        newEvent.u.vec3.z = ALPHA * (mCurrAngleZ) + (1 - ALPHA) * zAccelAngle;

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
