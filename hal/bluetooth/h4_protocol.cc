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

#define LOG_TAG "KingfisherBluetoothHAL"

#include "h4_protocol.h"

#include <android-base/logging.h>
#include <assert.h>
#include <fcntl.h>

namespace android {
namespace hardware {
namespace bluetooth {
namespace hci {

size_t H4Protocol::Send(uint8_t type, const uint8_t* data, size_t length)
{
    CHECK(length < sizeof(mTxBuf));

    mTxBuf[0] = type;
    memcpy(&mTxBuf[1], data, length);

    return WriteSafely(mSocketFd, mTxBuf, length + 1/*type*/);
}

void H4Protocol::OnPacketReady(void)
{
    switch (mHciPacketType)
    {
        case HCI_PACKET_TYPE_EVENT:
            mEventCb(mHciPacketizer.GetPacket());
            break;
        case HCI_PACKET_TYPE_ACL_DATA:
            mAclCb(mHciPacketizer.GetPacket());
            break;
        case HCI_PACKET_TYPE_SCO_DATA:
            mScoCb(mHciPacketizer.GetPacket());
            break;
        default: {
            bool bad_packet_type = true;
            CHECK(!bad_packet_type);
        }
    }

    mHciPacketType = HCI_PACKET_TYPE_UNKNOWN;
}

void H4Protocol::OnDataReady(int fd)
{
    if (mHciPacketType == HCI_PACKET_TYPE_UNKNOWN)
    {
        uint8_t buffer[1] = {0};
        size_t bytes_read = TEMP_FAILURE_RETRY(read(fd, buffer, 1));
        CHECK(bytes_read == 1);

        mHciPacketType = static_cast<HciPacketType>(buffer[0]);

    } else {
        mHciPacketizer.OnDataReady(fd, mHciPacketType);
    }
}

}  // namespace hci
}  // namespace bluetooth
}  // namespace hardware
}  // namespace android
