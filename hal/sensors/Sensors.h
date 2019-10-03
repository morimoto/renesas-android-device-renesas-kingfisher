#ifndef ANDROID_HARDWARE_SENSORS_V1_0_KINGFISHER_H
#define ANDROID_HARDWARE_SENSORS_V1_0_KINGFISHER_H

#include <android/hardware/sensors/1.0/ISensors.h>
#include <hidl/Status.h>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>

#include "IIO_sensor.h"
#include "IIO_sensor_descriptors.h"
#include "common.h"

namespace android {
namespace hardware {
namespace sensors {
namespace V1_0 {
namespace kingfisher {

using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::hardware::hidl_vec;

class Sensors : public ISensors
{
    public:
        Sensors();
        ~Sensors();
        /*
         * Methods from ::android::hardware::sensors::V1_0::ISensors follow.
         */
        Return<void> getSensorsList (getSensorsList_cb) override;
        Return<Result> setOperationMode(OperationMode) override;
        Return<Result> activate(int32_t, bool) override;
        Return<void> poll(int32_t, poll_cb) override;
        Return<Result> batch(int32_t, int64_t, int64_t) override;
        Return<Result> flush(int32_t) override;
        Return<Result> injectSensorData(const Event&) override;
        Return<void> registerDirectChannel(const SharedMemInfo&, registerDirectChannel_cb) override;
        Return<Result> unregisterDirectChannel(int32_t) override;
        Return<void> configDirectReport(int32_t, int32_t, RateLevel, configDirectReport_cb) override;

    private:
        void pollIIODeviceGroupBuffer(uint32_t groupIndex);

        void startPollThreads();
        void stopPollThreads();

        bool isEventsReady();
        int getEventsCount(OperationMode mode);

        /*Groups*/
        void fillGroups();
        size_t getGroupIndexByHandle(uint32_t);
        void activateGroup(bool, uint32_t);
        bool isActiveGroup(uint32_t index);
        bool setTriggerFreq(uint16_t, uint32_t);
        void activateAllGroups();
        uint16_t getMaxODRFromGroup(uint32_t);
        void openFileDescriptors();
        void closeFileDescriptors();
        void parseBuffer(const IIOCombinedBuffer& from, IIOBuffer& to, uint32_t sensorHandle);

        /*
         * This method is needed due to Android handles numeration - it starts
         * from 1. Keep sync with SensorIndex and HandleIndex enums.
         */
        inline int handleToIndex(int handle) const { return --handle; }
        inline bool testHandle(int handle) const {
            return (handle > 0 && handle < HANDLE_COUNT);
        };

        std::vector<std::unique_ptr<IIO_sensor>> mSensors;

        /* TODO: Investigate if synchronization is needed for it */
        OperationMode mMode;

        std::atomic<bool> mTerminatePollThreads;
        std::mutex mPollLock;

        /* Buffer condition variables */
        std::mutex mConditionMutex;
        std::condition_variable mCondVarBufferReady;
        std::atomic<bool> mEventsReady;

        static constexpr int maxReadRetries = 5;

        std::vector<IIOSensorDescriptor> mSensorDescriptors;
        std::vector<SensorsGroupDescriptor> mSensorGroups;
};

}  // namespace kingfisher
}  // namespace V1_0
}  // namespace sensors
}  // namespace hardware
}  // namespace android

#endif//ANDROID_HARDWARE_SENSORS_V1_0_KINGFISHER_H
