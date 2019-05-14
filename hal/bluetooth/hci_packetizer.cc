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

#include "hci_packetizer.h"

#include <android-base/logging.h>
#include <utils/Log.h>

#include <dlfcn.h>
#include <fcntl.h>

namespace {

const size_t preamble_size_for_type[] = {
    0,
    HCI_COMMAND_PREAMBLE_SIZE,
    HCI_ACL_PREAMBLE_SIZE,
    HCI_SCO_PREAMBLE_SIZE,
    HCI_EVENT_PREAMBLE_SIZE
};

const size_t packet_length_offset_for_type[] = {
    0,
    HCI_LENGTH_OFFSET_CMD,
    HCI_LENGTH_OFFSET_ACL,
    HCI_LENGTH_OFFSET_SCO,
    HCI_LENGTH_OFFSET_EVT
};

size_t HciGetPacketLengthForType(HciPacketType type, const uint8_t* preamble)
{
    size_t offset = packet_length_offset_for_type[type];

    if (type != HCI_PACKET_TYPE_ACL_DATA)
        return preamble[offset];

    return (((preamble[offset + 1]) << 8) | preamble[offset]);
}

}  // namespace

namespace android {
namespace hardware {
namespace bluetooth {
namespace hci {

void HciPacketizer::OnDataReady(int fd, HciPacketType packet_type)
{
    switch (mState)
    {
        case HCI_PREAMBLE:
        {
            size_t bytes_read = TEMP_FAILURE_RETRY(read(fd, mPreambleBuf + mBytesRead,
                preamble_size_for_type[packet_type] - mBytesRead));

            CHECK(bytes_read > 0);

            mBytesRead += bytes_read;

            if (mBytesRead == preamble_size_for_type[packet_type])
            {
                size_t packet_length = HciGetPacketLengthForType(packet_type, mPreambleBuf);

                mPacket.resize(preamble_size_for_type[packet_type] + packet_length);

                memcpy(mPacket.data(), mPreambleBuf, preamble_size_for_type[packet_type]);

                mBytesRemaining = packet_length;
                mState = HCI_PAYLOAD;
                mBytesRead = 0;
            }
            break;
        }

        case HCI_PAYLOAD:
        {
            size_t bytes_read = TEMP_FAILURE_RETRY(read(fd,
                    mPacket.data() + preamble_size_for_type[packet_type] + mBytesRead, mBytesRemaining));

            CHECK(bytes_read > 0);

            mBytesRemaining -= bytes_read;
            mBytesRead += bytes_read;

            if (mBytesRemaining == 0) {
                mPacketReadyCb();
                mState = HCI_PREAMBLE;
                mBytesRead = 0;
            }
            break;
        }
    }
}

}  // namespace hci
}  // namespace bluetooth
}  // namespace hardware
}  // namespace android
