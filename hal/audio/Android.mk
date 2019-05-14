#
# Copyright (C) 2016 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.


LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

ifeq ($(AUDIO_HAL_INTERFACE_VERSION),V4_0)
    LOCAL_CFLAGS += -DAUDIO_HAL_VERSION_4_0
else ifeq ($(AUDIO_HAL_INTERFACE_VERSION),V2_0)
    LOCAL_CFLAGS += -DAUDIO_HAL_VERSION_2_0
else
    $(error "AUDIO_HAL_INTERFACE_VERSION isn't set to V2_0 nor V4_0")
endif
LOCAL_CFLAGS += -DAUDIO_HAL_VERSION=$(AUDIO_HAL_INTERFACE_VERSION) \
                -DMAJOR_VERSION=4 \
                -DMINOR_VERSION=0

LOCAL_SRC_FILES := \
    service.cpp \
    Conversions.cpp \
    Device.cpp \
    DevicesFactory.cpp \
    ParametersUtil.cpp \
    PrimaryDevice.cpp \
    Stream.cpp \
    StreamIn.cpp \
    StreamOut.cpp \

LOCAL_SHARED_LIBRARIES := \
    libbase \
    libcutils \
    libfmq \
    libhardware \
    libhidlbase \
    libhidltransport \
    liblog \
    libutils \
    android.hardware.audio.common-util

ifeq ($(AUDIO_HAL_INTERFACE_VERSION),V4_0)
LOCAL_SHARED_LIBRARIES += \
    android.hardware.audio@4.0 \
    android.hardware.audio.common@4.0 \
    android.hardware.audio.common@4.0-util
else ifeq ($(AUDIO_HAL_INTERFACE_VERSION),V2_0)
LOCAL_SHARED_LIBRARIES += \
    android.hardware.audio@2.0 \
    android.hardware.audio.common@2.0 \
    android.hardware.audio.common@2.0-util
endif

LOCAL_WHOLE_STATIC_LIBRARIES := libmedia_helper

LOCAL_C_INCLUDES := \
    hardware/interfaces/audio/common/all-versions/default/include/common/all-versions/default \
    $(call include-path-for, audio-utils) \
    frameworks/av/include


LOCAL_MODULE := android.hardware.audio@4.0-service.kingfisher
LOCAL_INIT_RC := android.hardware.audio@4.0-service.kingfisher.rc

LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_PROPRIETARY_MODULE := true

include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)

LOCAL_MODULE := audio.primary.kingfisher
LOCAL_SRC_FILES := audio_hw.cc

LOCAL_SHARED_LIBRARIES := \
    liblog \
    libcutils \
    libtinyalsa \
    libaudioutils \
    libdl \
    libhardware

LOCAL_C_INCLUDES := \
    external/tinyalsa/include \
    system/media/audio_utils/include \
    system/media/audio_effects/include

ifeq ($(ENABLE_ADSP),true)
LOCAL_CFLAGS += -DENABLE_ADSP
endif

LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_PROPRIETARY_MODULE := true
LOCAL_CLANG := true
LOCAL_CPP_EXTENSION := .cc

include $(BUILD_SHARED_LIBRARY)
