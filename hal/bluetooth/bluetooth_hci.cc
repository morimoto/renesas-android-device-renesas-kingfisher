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

#include <bluetooth_hci.h>
#include <utils/Log.h>
#include <android-base/logging.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utils/Log.h>
#include <poll.h>

#define BTPROTO_HCI             1
#define HCI_CHANNEL_USER        1
#define HCI_CHANNEL_CONTROL     3
#define HCI_DEV_NONE            0xffff

#define MGMT_OP_INDEX_LIST      0x0003
#define MGMT_EV_INDEX_ADDED     0x0004
#define MGMT_EV_COMMAND_COMP    0x0001
#define MGMT_EV_SIZE_MAX        1024
#define MGMT_EV_POLL_TIMEOUT    3000    /* 3000ms */

struct sockaddr_hci {
    sa_family_t     hci_family;
    unsigned short  hci_dev;
    unsigned short  hci_channel;
};

struct mgmt_pkt {
    unsigned short  opcode;
    unsigned short  index;
    unsigned short  len;
    unsigned char   data[MGMT_EV_SIZE_MAX];
} __attribute__((packed));

struct mgmt_event_read_index {
  unsigned short    cc_opcode;
  unsigned char     status;
  unsigned short    num_intf;
  unsigned short    index[0];
} __attribute__((packed));

#define OSI_NO_INTR(fn)  do {} while ((fn) == -1 && errno == EINTR)

namespace android {
namespace hardware {
namespace bluetooth {
namespace V1_0 {
namespace kingfisher {

using android::hardware::hidl_vec;

BluetoothHci::BluetoothHci(void)
    : mDeathRecipient(new BluetoothDeathRecipient(this))
{
}

Return<void> BluetoothHci::initialize(const ::android::sp<IBluetoothHciCallbacks>& cb)
{
    const int hci_dev = 0; /* hci0 */

    ALOGD("BluetoothHci::initialize(hci%d)", hci_dev);

    mEventCb = cb;
    mEventCb->linkToDeath(mDeathRecipient, 0);

    // Create BT HCI socket
    mSocketFd = socket(PF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI);
    if (mSocketFd < 0) {
        ALOGE("%s: Can't create BT-RAW-HCI socket", __func__);
        mEventCb->initializationComplete(Status::INITIALIZATION_ERROR);
        return Void();
    }

    ALOGD("%s: HCI socket fd=%d", __func__, mSocketFd);

    if (!WaitForHciDevice(hci_dev)) {
        mEventCb->initializationComplete(Status::INITIALIZATION_ERROR);
        return Void();
    }

    mH4 = new hci::H4Protocol(mSocketFd,
        [cb](const hidl_vec<uint8_t>& packet) { cb->hciEventReceived(packet); },
        [cb](const hidl_vec<uint8_t>& packet) { cb->aclDataReceived(packet);  },
        [cb](const hidl_vec<uint8_t>& packet) { cb->scoDataReceived(packet);  });

    // Use a socket pair to enforce the TI FIONREAD requirement.
    int sockfd[2];
    socketpair(AF_LOCAL, SOCK_STREAM, 0, sockfd);
    int shim_fd = sockfd[0];
    int for_hci = sockfd[1];

    mFdWatcher.WatchFdForNonBlockingReads(mSocketFd, [this, shim_fd](int fd) {
        const size_t socket_bytes = 2048;
        uint8_t* socket_buffer = new uint8_t[socket_bytes];

        size_t bytes_read = TEMP_FAILURE_RETRY(read(fd, socket_buffer, socket_bytes));
        size_t bytes_written = TEMP_FAILURE_RETRY(write(shim_fd, socket_buffer, bytes_read));

        CHECK(bytes_written == bytes_read);

        delete[] socket_buffer;
    });

    mFdWatcher.WatchFdForNonBlockingReads(
      for_hci, [this](int fd) { mH4->OnDataReady(fd); });

    struct sockaddr_hci addr;
    memset(&addr, 0, sizeof(addr));
    addr.hci_family = AF_BLUETOOTH;
    addr.hci_dev = hci_dev;
    addr.hci_channel = HCI_CHANNEL_USER;

    ALOGI("%s: HCI bind fd=%d", __func__, mSocketFd);

    if (bind(mSocketFd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        ALOGE("%s: HCI socket bind error %s (%d)", __func__, strerror(errno), errno);
        mEventCb->initializationComplete(Status::INITIALIZATION_ERROR);
    } else {
        ALOGI("%s: HCI device ready", __func__);
        mEventCb->initializationComplete(Status::SUCCESS);
    }

    return Void();
}

Return<void> BluetoothHci::close(void)
{
    ALOGI("BluetoothHci::close()");

    if (mSocketFd >= 0) {
        mFdWatcher.StopWatchingFileDescriptors();

        ::close(mSocketFd);
        mSocketFd = -1;
    }

    if (mH4 != nullptr) {
        delete mH4;
        mH4 = nullptr;
    }

    mEventCb->unlinkToDeath(mDeathRecipient);
    return Void();
}

Return<void> BluetoothHci::sendHciCommand(const hidl_vec<uint8_t>& packet)
{
    mH4->Send(HCI_PACKET_TYPE_COMMAND, packet.data(), packet.size());
    return Void();
}

Return<void> BluetoothHci::sendAclData(const hidl_vec<uint8_t>& packet)
{
    mH4->Send(HCI_PACKET_TYPE_ACL_DATA, packet.data(), packet.size());
    return Void();
}

Return<void> BluetoothHci::sendScoData(const hidl_vec<uint8_t>& packet)
{
    mH4->Send(HCI_PACKET_TYPE_SCO_DATA, packet.data(), packet.size());
    return Void();
}

bool BluetoothHci::WaitForHciDevice(int hci_dev)
{
    struct sockaddr_hci addr;
    struct pollfd fds[1];
    struct mgmt_pkt ev;
    int fd;

    ALOGD("%s", __func__);

    fd = socket(PF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI);
    if (fd < 0) {
        ALOGE("Bluetooth socket error: %s", strerror(errno));
        return false;
    }

    ALOGD("%s: BT-HCI-RAW socket fd=%d", __func__, fd);

    memset(&addr, 0, sizeof(addr));
    addr.hci_family     = AF_BLUETOOTH;
    addr.hci_dev        = HCI_DEV_NONE;
    addr.hci_channel    = HCI_CHANNEL_CONTROL;

    if (bind(fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        ALOGE("HCI Channel Control: %s", strerror(errno));
        ::close(fd);
        return false;
    }

    fds[0].fd           = fd;
    fds[0].events       = POLLIN;

    /* Read Controller Index List Command */
    ev.opcode           = MGMT_OP_INDEX_LIST;
    ev.index            = HCI_DEV_NONE;
    ev.len              = 0;

    ssize_t wrote;
    OSI_NO_INTR(wrote = write(fd, &ev, 6));

    if (wrote != 6)
    {
        ALOGE("Unable to write mgmt command: %s", strerror(errno));
        ::close(fd);
        return false;
    }

    for (;;)
    {
        int n;
        OSI_NO_INTR(n = poll(fds, 1, MGMT_EV_POLL_TIMEOUT));
        if (n == -1) {
            ALOGE("Poll error: %s", strerror(errno));
            break;
        } else if (n == 0) {
            ALOGE("Timeout, no HCI device detected (%s)", strerror(errno));
            break;
        }

        if (fds[0].revents & POLLIN)
        {
            n = read(fd, &ev, sizeof(struct mgmt_pkt));
            if (n < 0) {
                ALOGE("Error reading control channel");
                break;
            }

            if (ev.opcode == MGMT_EV_INDEX_ADDED && ev.index == hci_dev) {
                /* Found proper HCI device */
                ALOGD("%s: HCI%d: success, close fd=%d", __func__, hci_dev, fd);
                ::close(fd);
                return true;
            }
            else if (ev.opcode == MGMT_EV_COMMAND_COMP) {
                mgmt_event_read_index *cc = reinterpret_cast<mgmt_event_read_index *>(ev.data);

                if (cc->cc_opcode != MGMT_OP_INDEX_LIST || cc->status != 0)
                    continue;

                for (int i = 0; i < cc->num_intf; i++) {
                    if (cc->index[i] == hci_dev) {
                        ALOGI("%s: HCI%d: sucess, close fd=%d", __func__, hci_dev, fd);
                        ::close(fd);
                        return true;
                    }
                }
            }
        }
    } /* while (1) */

    ALOGI("%s: fail, close fd=%d", __func__, fd);

    ::close(fd);
    return false;
}

}  // namespace kingfisher
}  // namespace V1_0
}  // namespace bluetooth
}  // namespace hardware
}  // namespace android
