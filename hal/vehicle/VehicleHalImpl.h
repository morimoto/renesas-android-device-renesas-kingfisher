/*
 * Copyright (C) 2017 GlobalLogic
 */

#ifndef _VehicleHal_H_
#define _VehicleHal_H_

#include <vector>
#include <thread>
#include <unordered_set>

#include <inttypes.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <net/if.h>
#include <memory.h>
#include <poll.h>

#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/input.h>
#include <linux/input-event-codes.h>

#include <vhal_v2_0/RecurrentTimer.h>
#include <vhal_v2_0/VehicleHal.h>
#include <vhal_v2_0/VehiclePropertyStore.h>

namespace android {
namespace hardware {
namespace automotive {
namespace vehicle {
namespace V2_0 {
namespace kingfisher {

class VehicleHalImpl : public VehicleHal {
private:
    VehiclePropertyStore *          mPropStore;
    std::unordered_set<int32_t>     mHvacPowerProps;
    RecurrentTimer                  mRecurrentTimer;
    int                             mSocket;
    struct sockaddr_can             mSockAddr;
    std::thread                     mCanThread;
    std::atomic<bool>               mCanThreadExit { false };
    std::thread                     mGpioThread;
    std::atomic<bool>               mGpioThreadExit { false };

public:
    VehicleHalImpl(VehiclePropertyStore* propStore);
    virtual ~VehicleHalImpl(void);

    virtual std::vector<VehiclePropConfig> listProperties() override;
    virtual VehicleHal::VehiclePropValuePtr get(const VehiclePropValue& requestedPropValue,
                                        StatusCode* outStatus) override;
    virtual StatusCode set(const VehiclePropValue& propValue) override;
    virtual StatusCode subscribe(int32_t property, float sampleRate) override;
    virtual StatusCode unsubscribe(int32_t property) override;
    virtual void onCreate() override;

    void GpioHandleThread(void);
    void CanRxHandleThread(void);
    void CanTxBytes(void *bytesPtr, size_t bytesCount);

private:
    constexpr std::chrono::nanoseconds hertzToNanoseconds(float hz) const {
        return std::chrono::nanoseconds(static_cast<int64_t>(1000000000L / hz));
    }

    void onGpioStateChanged(int fd, unsigned char* const key_bitmask, size_t array_len);
    void onContinuousPropertyTimer(const std::vector<int32_t>& properties);
    bool isContinuousProperty(int32_t propId) const;
};

}  // namespace kingfisher
}  // namespace V2_0
}  // namespace vehicle
}  // namespace automotive
}  // namespace hardware
}  // namespace android

#endif // _VehicleHal_H_
