/*
 * Copyright (C) 2018 GlobalLogic LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#define LOG_TAG "BroadcastRadioKingfisher"

#include <android-base/logging.h>
#include <android/hardware/broadcastradio/2.0/IBroadcastRadio.h>
#include <hidl/HidlTransportSupport.h>

#include "BroadcastRadio.h"

using android::hardware::configureRpcThreadpool;
using android::hardware::joinRpcThreadpool;
using android::hardware::broadcastradio::V2_0::kingfisher::BroadcastRadio;
using android::hardware::broadcastradio::V2_0::IBroadcastRadio;

int main(int /* argc */, char** /* argv */) {
    android::sp<IBroadcastRadio> radio_hal = new BroadcastRadio;

    configureRpcThreadpool(4, true);

    auto status = radio_hal->registerAsService();
    CHECK_EQ(status, android::OK) << "Failed to register Broadcast Radio HAL";

    joinRpcThreadpool();
}
