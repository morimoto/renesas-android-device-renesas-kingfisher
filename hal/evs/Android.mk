LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= 	\
    service.cpp 	\
    EvsCamera.cpp 	\
    EvsEnumerator.cpp 	\
    EvsDisplay.cpp

LOCAL_SHARED_LIBRARIES := \
        android.hardware.automotive.evs@1.0 \
        libui \
        libbase \
        libbinder \
        libcutils \
        libhardware \
        libhidlbase \
        libhidltransport \
        liblog \
        libutils \
        libdrm \
        libselinux

LOCAL_SHARED_LIBRARIES += vendor.renesas.graphics.composer@1.0

LOCAL_C_INCLUDES := \
    $(TOP)/hardware/renesas/hwcomposer \
    system/core/init \
    frameworks/native/libs/gui/include

LOCAL_MODULE:= android.hardware.automotive.evs@1.0-service.kingfisher
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_INIT_RC := android.hardware.automotive.evs@1.0-service.kingfisher.rc

LOCAL_CFLAGS += -std=c++17

ifeq ($(THREE_DISPLAY),true)
LOCAL_CFLAGS += -DTHIRD_DISPLAY_SUPPORT
endif

LOCAL_PROPRIETARY_MODULE := true

include $(BUILD_EXECUTABLE)
include $(call all-makefiles-under,$(LOCAL_PATH))
