#ifndef ANDROID_HARDWARE_IIO_sensor_V2_0_KINGFISHER_H
#define ANDROID_HARDWARE_IIO_sensor_V2_0_KINGFISHER_H

#include <android/hardware/sensors/1.0/ISensors.h>
#include <queue>
#include <iostream>
#include <fstream>
#include <string>

#include "IIO_sensor_descriptors.h"
#include "common.h"

namespace android {
namespace hardware {
namespace sensors {
namespace V2_0 {
namespace kingfisher {

using ::android::hardware::sensors::V1_0::Event;
using ::android::hardware::sensors::V1_0::OperationMode;
using ::android::hardware::sensors::V1_0::Result;
using ::android::hardware::sensors::V1_0::SensorInfo;

class IIO_sensor
{
    public:
        IIO_sensor(const IIOSensorDescriptor &);
        virtual ~IIO_sensor() { };

        Return<Result> activate(bool);
        Return<Result> batch(int64_t, int64_t);
        Return<Result> flush();
        void transformData(const IIOBuffer&);
        void injectEvent(const Event&);
        int getReadyEvents(std::vector<Event>&, OperationMode, int);
        size_t getReadyEventsCount(OperationMode);
        bool isActive() const { return mIsEnabled.load(); }
        uint16_t getODR() const { return mCurrODR.load(); }
        uint32_t getTicks() const { return mTicks; }
        SensorInfo getSensorInfo() const { return mSensorDescriptor.sensorInfo; }
        void addTicks(uint32_t ticks) { mTicks += ticks; }
        void resetTicks() { mTicks = 0; }
        void sendAdditionalData();
        std::pair<float, float> findBestResolution() const;
        void setResolution(std::pair<float, float> resolution);

    private:
        void getAvailFreqTable();
        void pushEvent(AtomicBuffer&, Event);

        Event createFlushEvent();
        Event chunkTransform(const IIOBuffer&);
        uint64_t timestampTransform(uint64_t);
        uint16_t getClosestOdr(uint16_t);

        void medianFilter(Event&);

        bool testSensorODR(uint16_t requestedODR)
        {
            return (std::find(mAvaliableODR.begin(), mAvaliableODR.end(),
                            requestedODR) != mAvaliableODR.end());
        }

        IIOSensorDescriptor mSensorDescriptor;
        std::vector<uint16_t> mAvaliableODR;

        uint32_t mCounter;
        /*This can be used from multiple threads*/
        std::atomic<uint16_t> mCurrODR;
        std::atomic<bool> mIsEnabled;

        uint64_t mMaxReportLatency;
        uint32_t mTicks;

        /* Sensors events buffers */
        AtomicBuffer mEventBuffer;
        AtomicBuffer mInjectEventBuffer;

        /* Buffer for filters */
        std::vector<Vector3D<double>> mFilterBuffer;
        static constexpr size_t filterBufferSize = 9;
};

}  // namespace kingfisher
}  // namespace V2_0
}  // namespace sensors
}  // namespace hardware
}  // namespace android

#endif//ANDROID_HARDWARE_IIO_sensor_V2_0_KINGFISHER_H
