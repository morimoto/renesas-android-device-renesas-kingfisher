//
// Copyright 2019 The Android Open Source Project
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

#define LOG_TAG "BluetoothHAL"

#include <android-base/logging.h>
#include <hidl/HidlTransportSupport.h>
#include <hidl/HidlSupport.h>

#include <android/hardware/bluetooth/1.0/IBluetoothHci.h>

#include "bluetooth_hci.h"

using ::android::hardware::configureRpcThreadpool;
using ::android::hardware::bluetooth::V1_0::IBluetoothHci;
using ::android::hardware::bluetooth::V1_0::kingfisher::BluetoothHci;
using ::android::hardware::joinRpcThreadpool;
using ::android::sp;

int main(int /* argc */, char** /* argv */)
{
    sp<IBluetoothHci> bt_hal = new BluetoothHci;
    configureRpcThreadpool(1, true);

    auto status = bt_hal->registerAsService();
    CHECK_EQ(status, android::OK) << "Failed to register Bluetooth HAL";

    joinRpcThreadpool();
}
