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

TARGET_BOOTLOADER_BOARD_NAME := kingfisher

include device/renesas/common/BoardConfigCommon.mk

# Wi-Fi
BOARD_WIFI_VENDOR := TI

# External camera
ifeq ($(USE_CAMERA_V4L2_KINGFISHER_HAL),true)
  DEVICE_MANIFEST_FILE += device/renesas/kingfisher/manifest.camera.xml
endif

# Kernel build rules
BOARD_KERNEL_CMDLINE := androidboot.selinux=permissive
TARGET_KERNEL_CONFIG := android_q_kingfisher_defconfig

