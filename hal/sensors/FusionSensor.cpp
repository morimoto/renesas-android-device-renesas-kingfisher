#include <android/hardware/sensors/1.0/ISensors.h>
#include <queue>
#include <iostream>
#include <fstream>
#include <string>

#include "SensorDescriptors.h"
#include "common.h"
#include "FusionSensor.h"

namespace android {
namespace hardware {
namespace sensors {
namespace V2_0 {
namespace kingfisher {

void FusionSensor::addHwSensors(std::vector<std::shared_ptr<BaseSensor>> &sensors)
{
    for (size_t i  = 0; i < sensors.size(); i++) {
        if (sensors[i]->getSensorType() == SensorType::ACCELEROMETER) {
            mAcc = sensors[i];
            ALOGD("Registered accelerometer\n");
        }
        if (sensors[i]->getSensorType() == SensorType::MAGNETIC_FIELD) {
            mMag = sensors[i];
            ALOGD("Registered magnetometer\n");
        }
        if (sensors[i]->getSensorType() == SensorType::GYROSCOPE) {
            mGyro = sensors[i];
            ALOGD("Registered gyroscope\n");
        }
    }
}

void FusionSensor::FusionSensor::activate(FUSION_MODE mode, uint32_t sensorHandle, bool activate)
{
    if (activate) {
        if (!mAcc->hasActiveListeners()) {
            mCurrentAccelEvents.clear();
        }
        mAcc->activate(activate);
        mAcc->addVirtualListener(sensorHandle);
    } else {
        mAcc->removeVirtualListener(sensorHandle);
    }

    if (mode != FUSION_NOMAG) {
        if (activate) {
            if (!mMag->hasActiveListeners()) {
                mCurrentMagnEvents.clear();
            }
            mMag->activate(activate);
            mMag->addVirtualListener(sensorHandle);
        } else {
            mMag->removeVirtualListener(sensorHandle);
        }
    }

    if (mode != FUSION_NOGYRO) {
        if (activate) {
            if (!mGyro->hasActiveListeners()) {
                mCurrentGyroEvents.clear();
            }
            mGyro->activate(activate);
            mGyro->addVirtualListener(sensorHandle);
        } else {
            mGyro->removeVirtualListener(sensorHandle);
        }
    }
}

void FusionSensor::batch(FUSION_MODE mode, uint64_t ns)
{
    // Comment from framework:
    // Call batch with timeout zero instead of setDelay().
    mAcc->batch(ns, 0);
    if (mode != FUSION_NOMAG) {
        mMag->batch(ns, 0);
    }
    if (mode != FUSION_NOGYRO) {
        mGyro->batch(ns, 0);
    }
}

void FusionSensor::pushEvents(const std::vector<Event> &events)
{
    if (events.empty())
        return;

    const std::lock_guard<std::mutex> lock(mBufferLock);

    for (size_t i = 0; i < events.size(); i++) {
        if (events[i].sensorType == SensorType::ACCELEROMETER) {
            if (mCurrentAccelEvents.size() >= FusionSensor::mMaxFusionData) {
                mCurrentAccelEvents.pop_front();
            }
            mCurrentAccelEvents.push_back(events[i]);
        } else if (events[i].sensorType == SensorType::MAGNETIC_FIELD) {
            if (mCurrentMagnEvents.size() >= FusionSensor::mMaxFusionData) {
                mCurrentMagnEvents.pop_front();
            }
            mCurrentMagnEvents.push_back(events[i]);
        } else if (events[i].sensorType == SensorType::GYROSCOPE) {
            if (mCurrentGyroEvents.size() >= FusionSensor::mMaxFusionData) {
                mCurrentGyroEvents.pop_front();
            }
            mCurrentGyroEvents.push_back(events[i]);
        }
    }
}

std::vector<FusionData> FusionSensor::getFusionEvents(FUSION_MODE mode)
{
    const std::lock_guard<std::mutex> lock(mBufferLock);
    size_t readyEvents = mCurrentAccelEvents.size();
    std::vector<FusionData> retData;

    if (mode == FUSION_9AXIS || mode == FUSION_NOGYRO) {
        readyEvents = std::min(readyEvents, mCurrentMagnEvents.size());
    }
    if (mode == FUSION_9AXIS || mode == FUSION_NOMAG) {
        readyEvents = std::min(readyEvents, mCurrentGyroEvents.size());
    }

    std::list<Event>::iterator accelIter = mCurrentAccelEvents.begin();
    std::list<Event>::iterator gyroIter = mCurrentGyroEvents.begin();
    std::list<Event>::iterator magnIter = mCurrentMagnEvents.begin();

    for (size_t i = 0; i < readyEvents; i++) {
        FusionData fusionData = {
            .accelEvent = *accelIter,
        };
        accelIter++;

        if (mode == FUSION_9AXIS || mode == FUSION_NOGYRO) {
            fusionData.magEvent = *magnIter;
            magnIter++;
        }
        if (mode == FUSION_9AXIS || mode == FUSION_NOMAG) {
            fusionData.gyroEvent = *gyroIter;
            gyroIter++;
        }

        retData.push_back(fusionData);
    }

    return retData;
}

int FusionSensor::getMinODR(FUSION_MODE mode){
    unsigned short minODR = mAcc->getODR();
    if (mode != FUSION_NOMAG) {
        minODR = std::min(minODR, mMag->getODR());
    }
    if (mode != FUSION_NOGYRO) {
        minODR = std::min(minODR, mGyro->getODR());
    }
    return minODR;
}

}  // namespace kingfisher
}  // namespace V2_0
}  // namespace sensors
}  // namespace hardware
}  // namespace android
