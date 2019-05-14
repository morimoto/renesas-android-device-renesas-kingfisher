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

#define LOG_TAG "DevicesFactoryHAL"

#include <string.h>

#include <android/log.h>

#include "Device.h"
#include "DevicesFactory.h"
#include "PrimaryDevice.h"

namespace android {
namespace hardware {
namespace audio {
namespace AUDIO_HAL_VERSION {
namespace kingfisher {

DevicesFactory::DevicesFactory()
{
	ALOGD("DevicesFactory created");
}

DevicesFactory::~DevicesFactory()
{
}

// static
int DevicesFactory::loadAudioInterface(const char *if_name, audio_hw_device_t **dev)
{
    const hw_module_t *mod;
    int rc;

    rc = hw_get_module_by_class(AUDIO_HARDWARE_MODULE_ID, if_name, &mod);
    if (rc) {
        ALOGE("%s couldn't load audio hw module %s.%s (%s)", __func__,
                AUDIO_HARDWARE_MODULE_ID, if_name, strerror(-rc));
        goto out;
    }
    rc = audio_hw_device_open(mod, dev);
    if (rc) {
        ALOGE("%s couldn't open audio hw device in %s.%s (%s)", __func__,
                AUDIO_HARDWARE_MODULE_ID, if_name, strerror(-rc));
        goto out;
    }
    if ((*dev)->common.version < AUDIO_DEVICE_API_VERSION_MIN) {
        ALOGE("%s wrong audio hw device version %04x", __func__, (*dev)->common.version);
        rc = -EINVAL;
        audio_hw_device_close(*dev);
        goto out;
    }
    return OK;

out:
    *dev = NULL;
    return rc;
}

#ifdef AUDIO_HAL_VERSION_2_0
// static
const char* DevicesFactory::deviceToString(IDevicesFactory::Device device) {
    switch (device) {
        case IDevicesFactory::Device::PRIMARY: return AUDIO_HARDWARE_MODULE_ID_PRIMARY;
        case IDevicesFactory::Device::A2DP: return AUDIO_HARDWARE_MODULE_ID_A2DP;
        case IDevicesFactory::Device::USB: return AUDIO_HARDWARE_MODULE_ID_USB;
        case IDevicesFactory::Device::R_SUBMIX: return AUDIO_HARDWARE_MODULE_ID_REMOTE_SUBMIX;
        case IDevicesFactory::Device::STUB: return AUDIO_HARDWARE_MODULE_ID_STUB;
    }
    return nullptr;
}

// Methods from ::android::hardware::audio::AUDIO_HAL_VERSION::IDevicesFactory follow.
Return<void> DevicesFactory::openDevice(IDevicesFactory::Device device, openDevice_cb _hidl_cb)  {
    audio_hw_device_t *halDevice;
    Result retval(Result::INVALID_ARGUMENTS);
    sp<IDevice> result;
    const char* moduleName = deviceToString(device);
    if (moduleName != nullptr) {
        int halStatus = loadAudioInterface(moduleName, &halDevice);
        if (halStatus == OK) {
            if (device == IDevicesFactory::Device::PRIMARY) {
                result = new PrimaryDevice(halDevice);
            } else {
                result = new ::android::hardware::audio::AUDIO_HAL_VERSION::kingfisher::
                    Device(halDevice);
            }
            retval = Result::OK;
        } else if (halStatus == -EINVAL) {
            retval = Result::NOT_INITIALIZED;
        }
    }
    _hidl_cb(retval, result);
    return Void();
}
#endif

#ifdef AUDIO_HAL_VERSION_4_0
Return<void> DevicesFactory::openDevice(const hidl_string& moduleName, openDevice_cb _hidl_cb) {
    if (moduleName == AUDIO_HARDWARE_MODULE_ID_PRIMARY) {
        return openDevice<::android::hardware::audio::AUDIO_HAL_VERSION::kingfisher::PrimaryDevice>
                (moduleName.c_str(), _hidl_cb);
    }
    return openDevice(moduleName.c_str(), _hidl_cb);
}

Return<void> DevicesFactory::openPrimaryDevice(openPrimaryDevice_cb _hidl_cb) {
    return openDevice<::android::hardware::audio::AUDIO_HAL_VERSION::kingfisher::PrimaryDevice>
            (AUDIO_HARDWARE_MODULE_ID_PRIMARY, _hidl_cb);
}
#endif

Return<void> DevicesFactory::openDevice(const char* moduleName, openDevice_cb _hidl_cb) {
    return openDevice<::android::hardware::audio::AUDIO_HAL_VERSION::kingfisher::Device>(moduleName, _hidl_cb);
}

template <class DeviceShim, class Callback>
Return<void> DevicesFactory::openDevice(const char* moduleName, Callback _hidl_cb) {
    audio_hw_device_t* halDevice;
    Result retval(Result::INVALID_ARGUMENTS);
    sp<DeviceShim> result;
    int halStatus = loadAudioInterface(moduleName, &halDevice);
    if (halStatus == OK) {
        result = new DeviceShim(halDevice);
        retval = Result::OK;
    } else if (halStatus == -EINVAL) {
        retval = Result::NOT_INITIALIZED;
    }
    _hidl_cb(retval, result);
    return Void();
}

IDevicesFactory* HIDL_FETCH_IDevicesFactory(const char* /* name */) {
    return new DevicesFactory();
}

}  // namespace kingfisher
}  // namespace AUDIO_HAL_VERSION
}  // namespace audio
}  // namespace hardware
}  // namespace android
