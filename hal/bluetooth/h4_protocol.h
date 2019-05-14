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

#include <hidl/HidlSupport.h>

#include "async_fd_watcher.h"
#include "hci_internals.h"
#include "hci_protocol.h"

namespace android {
namespace hardware {
namespace bluetooth {
namespace hci {

class H4Protocol : public HciProtocol {
private:
    int                 mSocketFd;
    uint8_t             mTxBuf[2048];

    PacketReadCallback  mEventCb;
    PacketReadCallback  mAclCb;
    PacketReadCallback  mScoCb;

    HciPacketType       mHciPacketType;
    hci::HciPacketizer  mHciPacketizer;

public:
    H4Protocol(int fd, PacketReadCallback event_cb, PacketReadCallback acl_cb, PacketReadCallback sco_cb) :
        mSocketFd(fd),
        mEventCb(event_cb),
        mAclCb(acl_cb),
        mScoCb(sco_cb),
        mHciPacketType(HCI_PACKET_TYPE_UNKNOWN),
        mHciPacketizer([this]() { OnPacketReady(); }) {}

    size_t Send(uint8_t type, const uint8_t* data, size_t length);

    void OnPacketReady(void);
    void OnDataReady(int fd);
};

}  // namespace hci
}  // namespace bluetooth
}  // namespace hardware
}  // namespace android
