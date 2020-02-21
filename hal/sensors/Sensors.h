#ifndef ANDROID_HARDWARE_SENSORS_V2_0_KINGFISHER_H
#define ANDROID_HARDWARE_SENSORS_V2_0_KINGFISHER_H

#include <android/hardware/sensors/2.0/ISensors.h>
#include <hidl/Status.h>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <fmq/MessageQueue.h>
#include <hidl/MQDescriptor.h>

#include "IIOSensor.h"
#include "SensorDescriptors.h"
#include "common.h"
#include "FusionSensor.h"

namespace android {
namespace hardware {
namespace sensors {
namespace V2_0 {
namespace kingfisher {

using ::android::hardware::Return;
using ::android::hardware::MessageQueue;
using ::android::hardware::MQDescriptor;
using ::android::hardware::EventFlag;

class Sensors : public ISensors
{
    using Event = ::android::hardware::sensors::V1_0::Event;
    using OperationMode = ::android::hardware::sensors::V1_0::OperationMode;
    using RateLevel = ::android::hardware::sensors::V1_0::RateLevel;
    using Result = ::android::hardware::sensors::V1_0::Result;
    using SharedMemInfo = ::android::hardware::sensors::V1_0::SharedMemInfo;

    public:
        Sensors();
        ~Sensors();
        /*
         * Methods from ::android::hardware::sensors::V1_0::ISensors follow.
         */
        Return<void> getSensorsList (getSensorsList_cb) override;
        Return<Result> setOperationMode(OperationMode) override;
        Return<Result> activate(int32_t, bool) override;
        Return<Result> batch(int32_t, int64_t, int64_t) override;
        Return<Result> flush(int32_t) override;
        Return<Result> injectSensorData(const Event&) override;
        Return<void> registerDirectChannel(const SharedMemInfo&, registerDirectChannel_cb) override;
        Return<Result> unregisterDirectChannel(int32_t) override;
        Return<void> configDirectReport(int32_t, int32_t, RateLevel, configDirectReport_cb) override;
        /*
         * v2.0 required interface.
         */
        Return<Result> initialize(const ::android::hardware::MQDescriptorSync<Event>& eventQueueDescriptor,
                                  const ::android::hardware::MQDescriptorSync<uint32_t>& wakeLockDescriptor,
                                  const sp<ISensorsCallback>& sensorsCallback) override;

    private:
        Return<Result> HWBatch(int32_t, int64_t, int64_t);
        Return<Result> virtualBatch(int32_t, int64_t, int64_t);
        void pollIIODeviceGroupBuffer(uint32_t groupIndex);
        void pollVirtualDeviceGroupBuffer(uint32_t groupIndex);

        void startPollThreads();
        void stopPollThreads();

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
         * v2.0 private methods taken from the default way.
         */
        void deleteEventFlag();
        void postEvents(const std::vector<Event>& events);
        /*
         * This method is needed due to Android handles numeration - it starts
         * from 1. Keep sync with SensorIndex and HandleIndex enums.
         */
        inline int handleToIndex(int handle) const { return --handle; }
        inline bool testHandle(int handle) const {
            return (handle > 0 && handle < HANDLE_COUNT);
        };

        std::vector<std::shared_ptr<BaseSensor>> mSensors;
        FusionSensor mFusionSensor;

        OperationMode mMode;

        std::atomic<bool> mTerminatePollThreads;
        static constexpr int maxReadRetries = 5;

        std::vector<SensorDescriptor> mSensorDescriptors;
        std::vector<SensorsGroupDescriptor> mSensorGroups;

        /* v2.0 specific attributes taken from default way */
        using EventMessageQueue = MessageQueue<Event, kSynchronizedReadWrite>;
        using WakeLockMessageQueue = MessageQueue<uint32_t, kSynchronizedReadWrite>;

        std::unique_ptr<EventMessageQueue> mEventQueue;
        std::unique_ptr<WakeLockMessageQueue> mWakeLockQueue;

        EventFlag* mEventQueueFlag;
        std::mutex mWriteLock;

        std::atomic<bool> mPollThreadsStarted;

        std::atomic<bool> mAccelEventReady = false;
        std::atomic<bool> mGyroEventReady = false;
        std::atomic<bool> mMagnEventReady = false;
        void setReadyFlag(SensorType);
        void resetReadyFlag(FUSION_MODE);
        void notifyListeners();
};

}  // namespace kingfisher
}  // namespace V2_0
}  // namespace sensors
}  // namespace hardware
}  // namespace android

#endif//ANDROID_HARDWARE_SENSORS_V2_0_KINGFISHER_H
