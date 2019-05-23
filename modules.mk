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

PRODUCT_OUT         := $(OUT_DIR)/target/product/$(TARGET_PRODUCT)
KERNEL_MODULES_OUT  := $(PRODUCT_OUT)/obj/KERNEL_MODULES

BOARD_VENDOR_KERNEL_MODULES += \
	$(KERNEL_MODULES_OUT)/wl18xx.ko \
	$(KERNEL_MODULES_OUT)/wlcore.ko \
	$(KERNEL_MODULES_OUT)/wlcore_sdio.ko

BOARD_VENDOR_KERNEL_MODULES += \
	$(KERNEL_MODULES_OUT)/btwilink.ko \
	$(KERNEL_MODULES_OUT)/st_drv.ko

BOARD_VENDOR_KERNEL_MODULES += \
	$(KERNEL_MODULES_OUT)/radio-i2c-si4689.ko

BOARD_VENDOR_KERNEL_MODULES += \
	$(KERNEL_MODULES_OUT)/lsm9ds0.ko \
	$(KERNEL_MODULES_OUT)/industrialio-triggered-buffer.ko

BOARD_VENDOR_KERNEL_MODULES += \
	$(KERNEL_MODULES_OUT)/uvcvideo.ko

include device/renesas/common/ModulesCommon.mk
