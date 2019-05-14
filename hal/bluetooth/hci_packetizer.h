//
// Copyright 2017 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#pragma once

#include <functional>

#include <hidl/HidlSupport.h>

#include "hci_internals.h"

namespace android {
namespace hardware {
namespace bluetooth {
namespace hci {

using ::android::hardware::hidl_vec;
using HciPacketReadyCallback = std::function<void(void)>;

class HciPacketizer
{
protected:
    enum State {
        HCI_PREAMBLE,
        HCI_PAYLOAD
    };

    hidl_vec<uint8_t>       mPacket;
    HciPacketReadyCallback  mPacketReadyCb;
    State                   mState;
    uint8_t                 mPreambleBuf[HCI_PREAMBLE_SIZE_MAX];
    size_t                  mBytesRemaining;
    size_t                  mBytesRead;

public:
    HciPacketizer(HciPacketReadyCallback packet_cb) :
        mPacketReadyCb(packet_cb),
        mState(HCI_PREAMBLE),
        mBytesRemaining(0),
        mBytesRead(0)
    { };

    void OnDataReady(int fd, HciPacketType packet_type);

    const hidl_vec<uint8_t>& GetPacket(void) const {
        return mPacket;
    }
};

}  // namespace hci
}  // namespace bluetooth
}  // namespace hardware
}  // namespace android
