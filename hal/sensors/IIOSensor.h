#ifndef ANDROID_HARDWARE_IIO_SENSOR_V2_0_KINGFISHER_H
#define ANDROID_HARDWARE_IIO_SENSOR_V2_0_KINGFISHER_H

#include <android/hardware/sensors/1.0/ISensors.h>
#include <queue>
#include <iostream>
#include <fstream>
#include <string>

#include "SensorDescriptors.h"
#include "BaseSensor.h"
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

class IIOSensor : public BaseSensor
{
    public:
        explicit IIOSensor(const SensorDescriptor &);
        virtual ~IIOSensor() { };

        Return<Result> activate(bool) override;
        Return<Result> batch(int64_t, int64_t) override;
        Return<Result> flush() override;
        void transformData(const IIOBuffer&) override;

        void injectEvent(const Event&) override;
        int getReadyEvents(std::vector<Event>&, OperationMode) override;

        void sendAdditionalData();
        std::pair<float, float> findBestResolution() const;
        void setResolution(std::pair<float, float> resolution);

        void addVirtualListener(uint32_t) override;
        void removeVirtualListener(uint32_t) override;
        bool hasActiveListeners() const override;

    protected:
        void getAvailFreqTable();
        void pushEvent(AtomicBuffer&, Event);

        Event chunkTransform(const IIOBuffer&);
        uint64_t timestampTransform(uint64_t);
        uint16_t getClosestOdr(uint16_t);

        void medianFilter(Event&);

        bool testSensorODR(uint16_t requestedODR)
        {
            return (std::find(mAvaliableODR.begin(), mAvaliableODR.end(),
                            requestedODR) != mAvaliableODR.end());
        }

        std::vector<uint16_t> mAvaliableODR;

        uint32_t mCounter;

        /* Sensors events buffers */
        AtomicBuffer mEventBuffer;

        /* Buffer for filters */
        std::vector<Vector3D<double>> mFilterBuffer;
        static constexpr size_t filterBufferSize = 9;
        std::vector<uint32_t> mListenerHandlers;
};

}  // namespace kingfisher
}  // namespace V2_0
}  // namespace sensors
}  // namespace hardware
}  // namespace android

#endif//ANDROID_HARDWARE_IIO_SENSOR_V2_0_KINGFISHER_H
