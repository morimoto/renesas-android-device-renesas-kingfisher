/*
 * Copyright (C) 2016 The Android Open Source Project
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

#define LOG_TAG "audiohalservice"

#include <utils/StrongPointer.h>
#include <hidl/HidlTransportSupport.h>
#include <hidl/LegacySupport.h>
#ifdef AUDIO_HAL_VERSION_4_0
#include <android/hardware/audio/4.0/IDevicesFactory.h>
#elif defined(AUDIO_HAL_VERSION_2_0)
#include <android/hardware/audio/2.0/IDevicesFactory.h>
#endif
#include "DevicesFactory.h"
#include "Device.h"

using android::hardware::configureRpcThreadpool;
using android::hardware::joinRpcThreadpool;

using android::hardware::audio::AUDIO_HAL_VERSION::IDevicesFactory;
using android::hardware::audio::AUDIO_HAL_VERSION::IDevice;
using namespace android::hardware::audio::AUDIO_HAL_VERSION::kingfisher;
using namespace android;

//using android::OK;

int main() {
    android::sp<IDevicesFactory> service1 =
            new android::hardware::audio::AUDIO_HAL_VERSION::kingfisher::DevicesFactory();
    configureRpcThreadpool(16, true /*callerWillJoin*/);
    status_t status = service1->registerAsService();
    if (status == OK) {
        ALOGD("%s is ready.", "DevicesFactory");
        joinRpcThreadpool();
    } else {
        ALOGE("Could not register service %s (%d).", "DevicesFactory", status);
    }
    return status;
}
