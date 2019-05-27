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

$(call inherit-product, device/renesas/common/DeviceCommon.mk)
$(call inherit-product, device/renesas/kingfisher/modules.mk)

# ----------------------------------------------------------------------
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.sensor.accelerometer.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.accelerometer.xml \
    frameworks/native/data/etc/android.hardware.sensor.compass.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.compass.xml \
    frameworks/native/data/etc/android.hardware.sensor.gyroscope.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.gyroscope.xml \
    frameworks/native/data/etc/android.hardware.broadcastradio.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.broadcastradio.xml

PRODUCT_COPY_FILES += \
    device/renesas/kingfisher/permissions/privapp-permissions-kingfisher.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/privapp-permissions-kingfisher.xml

# Init RC files
PRODUCT_COPY_FILES += \
    device/renesas/kingfisher/init/init.kingfisher.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/hw/init.kingfisher.rc \
    device/renesas/kingfisher/init/init.kingfisher.usb.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/hw/init.kingfisher.usb.rc \
    device/renesas/kingfisher/init/ueventd.kingfisher.rc:$(TARGET_COPY_OUT_VENDOR)/ueventd.rc \
    device/renesas/kingfisher/init/init.recovery.kingfisher.rc:root/init.recovery.kingfisher.rc

# Audio
USE_XML_AUDIO_POLICY_CONF := 1
AUDIO_HAL_INTERFACE_VERSION := V4_0

PRODUCT_PACKAGES += \
    audio.primary.kingfisher \
    android.hardware.audio.effect@4.0-service.renesas

PRODUCT_PACKAGES += android.hardware.audio@4.0-service.kingfisher

PRODUCT_COPY_FILES += \
    device/renesas/kingfisher/hal/audio/audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_configuration.xml \
    device/renesas/kingfisher/hal/audio/r_submix_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/r_submix_audio_policy_configuration.xml \
    device/renesas/kingfisher/hal/audio/a2dp_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/a2dp_audio_policy_configuration.xml \
    device/renesas/kingfisher/hal/audio/audio_policy_volumes.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_volumes.xml \
    device/renesas/kingfisher/hal/audio/default_volume_tables.xml:$(TARGET_COPY_OUT_VENDOR)/etc/default_volume_tables.xml

# Bluetooth
PRODUCT_PACKAGES += \
    uim \
    TIInit_11.8.32.bts

# Wi-Fi
PRODUCT_PACKAGES += \
    android.hardware.wifi@1.2-service.kingfisher \
    wl18xx-fw-4.bin \
    wl18xx-conf.bin

# External camera
ifeq ($(USE_CAMERA_V4L2_KINGFISHER_HAL),true)
PRODUCT_PACKAGES += camera.v4l2.kingfisher
PRODUCT_PROPERTY_OVERRIDES += ro.hardware.camera=v4l2.kingfisher

PRODUCT_PACKAGES += \
    android.hardware.camera.provider@2.4-service.kingfisher \
    android.hardware.camera.provider@2.4-external-service.kingfisher

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.camera.external.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.external.xml \
    device/renesas/$(TARGET_PRODUCT)/external_camera_config.xml:$(TARGET_COPY_OUT_VENDOR)/etc/external_camera_config.xml
endif # External camera

# Touchcreen configuration
PRODUCT_COPY_FILES += \
    device/renesas/kingfisher/touchscreen_skeleton.idc:$(TARGET_COPY_OUT_VENDOR)/usr/idc/touchscreen_skeleton.idc
