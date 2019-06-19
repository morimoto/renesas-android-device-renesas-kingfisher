#
# Copyright (C) 2018 GlobalLogic
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
#

# Include only for Kingfisher board
ifneq (,$(filter $(TARGET_PRODUCT), kingfisher))

LOCAL_PATH:= $(call my-dir)

BROADCASTRADIO_PREBUILT_FW := \
    patch.bin \
    fm.bif \
    am.bif

define _build-broadcastradio-prebuilt-fw
include $$(CLEAR_VARS)
LOCAL_MODULE                := $(notdir $1)
LOCAL_MODULE_CLASS          := ETC
LOCAL_SRC_FILES             := $1
LOCAL_MODULE_PATH           := $(dir $$(TARGET_OUT_VENDOR)/etc/firmware/si46xx/$1)
_modules += $$(LOCAL_MODULE)
include $$(BUILD_PREBUILT)
endef

_file :=
_modules :=
si46xx_firmware:
    $(foreach _file, $(BROADCASTRADIO_PREBUILT_FW), \
        $(eval $(call _build-broadcastradio-prebuilt-fw,$(_file))))

include $(CLEAR_VARS)
LOCAL_MODULE                := si46xx_firmware
LOCAL_MODULE_TAGS           := optional
LOCAL_REQUIRED_MODULES      := $(_modules)
include $(BUILD_PHONY_PACKAGE)

endif # $(TARGET_PRODUCT) kingfisher
