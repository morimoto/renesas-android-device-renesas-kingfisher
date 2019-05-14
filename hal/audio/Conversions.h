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

#ifndef android_hardware_audio_Conversions_H_
#define android_hardware_audio_Conversions_H_

#include <string>
#ifdef AUDIO_HAL_VERSION_4_0
#include <android/hardware/audio/4.0/types.h>
#elif defined(AUDIO_HAL_VERSION_2_0)
#include <android/hardware/audio/2.0/types.h>
#endif
#include <system/audio.h>

namespace android {
namespace hardware {
namespace audio {
namespace AUDIO_HAL_VERSION {
namespace kingfisher {

using ::android::hardware::audio::AUDIO_HAL_VERSION::DeviceAddress;

std::string deviceAddressToHal(const DeviceAddress& address);

#ifdef AUDIO_HAL_VERSION_4_0
bool halToMicrophoneCharacteristics(MicrophoneInfo* pDst,
                                    const struct audio_microphone_characteristic_t& src);
#endif

}  // namespace kingfisher
}  // namespace AUDIO_HAL_VERSION
}  // namespace audio
}  // namespace hardware
}  // namespace android

#endif  // android_hardware_audio_Conversions_H_
