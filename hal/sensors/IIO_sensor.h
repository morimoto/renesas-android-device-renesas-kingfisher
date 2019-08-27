#ifndef ANDROID_HARDWARE_IIO_sensor_V1_0_KINGFISHER_H
#define ANDROID_HARDWARE_IIO_sensor_V1_0_KINGFISHER_H

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
namespace V1_0 {
namespace kingfisher {

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

        bool isActive() const { return mIsEnabled.load(); }
        uint16_t getODR() const { return mCurrODR.load(); }
        uint32_t getTicks() const { return mTicks; }
        SensorInfo getSensorInfo() const { return mSensorDescriptor.sensorInfo; }
        void addTicks(uint32_t ticks) { mTicks += ticks; }
        void resetTicks() { mTicks = 0; }

        bool isBufferReady(OperationMode mode)
        {
            /*
             * This predicate is used for wakeup main poll function.
             * We have two reasons for thread wakeup:
             *      1) Flush is called
             *      2) We have enough events in buffer for reporting
             */
            return (mFlushCounter.load() || (getReadyEventsCount(mode) >= mWatermark));
        }

    private:
        void getAvailFreqTable();
        void updateWatermark();
        void pushEvent(AtomicBuffer&, Event);

        Event createFlushEvent();
        Event chunkTransform(const IIOBuffer&);
        uint64_t timestampTransform(uint64_t);
        uint16_t getClosestOdr(uint16_t);

        void medianFilter(Event&);
        size_t getReadyEventsCount(OperationMode);

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
        size_t mWatermark;
        std::atomic<int> mFlushCounter;
        uint32_t mTicks;

        /* Sensors events buffers */
        AtomicBuffer mEventBuffer;
        AtomicBuffer mInjectEventBuffer;

        /* Buffer for filters */
        std::vector<Vector3D<double>> mFilterBuffer;
        static constexpr size_t filterBufferSize = 9;
};

}  // namespace kingfisher
}  // namespace V1_0
}  // namespace sensors
}  // namespace hardware
}  // namespace android

#endif//ANDROID_HARDWARE_IIO_sensor_V1_0_KINGFISHER_H
