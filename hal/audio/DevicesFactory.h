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

#ifndef ANDROID_HARDWARE_AUDIO_DEVICESFACTORY_H_
#define ANDROID_HARDWARE_AUDIO_DEVICESFACTORY_H_

#include <hardware/audio.h>

#ifdef AUDIO_HAL_VERSION_4_0
#include <android/hardware/audio/4.0/IDevicesFactory.h>
#include <android/hardware/audio/4.0/IDevice.h>
#elif defined(AUDIO_HAL_VERSION_2_0)
#include <android/hardware/audio/2.0/IDevicesFactory.h>
#include <android/hardware/audio/2.0/IDevice.h>
#endif
#include <hidl/Status.h>

#include <hidl/MQDescriptor.h>

namespace android {
namespace hardware {
namespace audio {
namespace AUDIO_HAL_VERSION {
namespace kingfisher {

using ::android::hardware::audio::AUDIO_HAL_VERSION::IDevice;
using ::android::hardware::audio::AUDIO_HAL_VERSION::IDevicesFactory;
using ::android::hardware::audio::AUDIO_HAL_VERSION::Result;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::hardware::hidl_vec;
using ::android::hardware::hidl_string;
using ::android::sp;

//class Device;

struct DevicesFactory : public IDevicesFactory {

    // Methods from ::android::hardware::audio::AUDIO_HAL_VERSION::IDevicesFactory follow.
#ifdef AUDIO_HAL_VERSION_2_0
    Return<void> openDevice(IDevicesFactory::Device device, openDevice_cb _hidl_cb) override;
#endif
#ifdef AUDIO_HAL_VERSION_4_0
    Return<void> openDevice(const hidl_string& device, openDevice_cb _hidl_cb) override;
    Return<void> openPrimaryDevice(openPrimaryDevice_cb _hidl_cb) override;
#endif
    DevicesFactory();
    ~DevicesFactory() override;

  private:
#ifdef AUDIO_HAL_VERSION_2_0
    static const char* deviceToString(IDevicesFactory::Device device);
#endif

    template <class DeviceShim, class Callback>
    Return<void> openDevice(const char* moduleName, Callback _hidl_cb);
    Return<void> openDevice(const char* moduleName, openDevice_cb _hidl_cb);

    static int loadAudioInterface(const char *if_name, audio_hw_device_t **dev);

};

extern "C" IDevicesFactory* HIDL_FETCH_IDevicesFactory(const char* name);

}  // namespace kingfisher
}  // namespace AUDIO_HAL_VERSION
}  // namespace audio
}  // namespace hardware
}  // namespace android

#endif  // ANDROID_HARDWARE_AUDIO_DEVICESFACTORY_H
