/*
 * Copyright (C) 2018 GlobalLogic
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

#define LOG_TAG "android.hardware.automotive.evs@1.0.kingfisher"

#include "EvsEnumerator.h"
#include "EvsCamera.h"
#include "EvsDisplay.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

namespace android {
namespace hardware {
namespace automotive {
namespace evs {
namespace V1_0 {
namespace kingfisher {


// NOTE:  All members values are static so that all clients operate on the same state
//        That is to say, this is effectively a singleton despite the fact that HIDL
//        constructs a new instance for each client.
std::list<EvsEnumerator::CameraRecord>   EvsEnumerator::sCameraList;
wp<EvsDisplay>                           EvsEnumerator::sActiveDisplay;


EvsEnumerator::EvsEnumerator() {
    ALOGD("EvsEnumerator created");

    // Constant predefined list of EVS cameras in the "/dev" filesystem.
    const std::vector<const char *> cameraNames {
        "/dev/video3",
        "/dev/video2",
        "/dev/video1",
        "/dev/video0"
    };

    // Probe each camera one by one and use only that which can be opened and requested.
    for (auto cn : cameraNames) {
        const int fd = open(cn, O_RDWR);
        if (-1 == fd) {
            ALOGE("Error while opening device %s: %s.", cn, strerror(errno));
            continue;
        }

        v4l2_format format;
        std::memset(&format, 0x00, sizeof(format));
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(fd, VIDIOC_G_FMT, &format) < 0) {
            ALOGE("VIDIOC_G_FMT: %s for device %s.", strerror(errno), cn);
        } else {
            sCameraList.emplace_back(cn, format.fmt.pix.width, format.fmt.pix.height);
            ALOGI("Camera %s (%ux%u) is successfully probed.",
                    cn, format.fmt.pix.width, format.fmt.pix.height);
        }

        close(fd);
    }
}


// Methods from ::android::hardware::automotive::evs::V1_0::IEvsEnumerator follow.
Return<void> EvsEnumerator::getCameraList(getCameraList_cb _hidl_cb)  {
    ALOGD("getCameraList");

    const unsigned numCameras = sCameraList.size();

    // Build up a packed array of CameraDesc for return
    hidl_vec<CameraDesc> hidlCameras;
    hidlCameras.resize(numCameras);
    unsigned i = 0;
    for (const auto& cam : sCameraList) {
        hidlCameras[i++] = cam.desc;
    }

    // Send back the results
    ALOGD("reporting %zu cameras available", hidlCameras.size());
    _hidl_cb(hidlCameras);

    // HIDL convention says we return Void if we sent our result back via callback
    return Void();
}


Return<sp<IEvsCamera>> EvsEnumerator::openCamera(const hidl_string& cameraId) {
    ALOGD("openCamera");

    // Is this a recognized camera id?
    CameraRecord *pRecord = findCameraById(cameraId);
    if (!pRecord) {
        ALOGE("Requested camera %s not found", cameraId.c_str());
        return nullptr;
    }

    // Has this camera already been instantiated by another caller?
    sp<EvsCamera> pActiveCamera = pRecord->activeInstance.promote();
    if (pActiveCamera != nullptr) {
        ALOGW("Killing previous camera because of new caller");
        closeCamera(pActiveCamera);
    }

    // Construct a camera instance for the caller
    pActiveCamera = new EvsCamera(cameraId.c_str(), pRecord->dim.width, pRecord->dim.height);
    pRecord->activeInstance = pActiveCamera;
    if (pActiveCamera == nullptr) {
        ALOGE("Failed to allocate new EvsCamera object for %s\n", cameraId.c_str());
    }

    return pActiveCamera;
}


Return<void> EvsEnumerator::closeCamera(const ::android::sp<IEvsCamera>& pCamera) {
    ALOGD("closeCamera");

    if (pCamera == nullptr) {
        ALOGE("Ignoring call to closeCamera with null camera ptr");
        return Void();
    }

    // Get the camera id so we can find it in our list
    std::string cameraId;
    pCamera->getCameraInfo([&cameraId](CameraDesc desc) {
                               cameraId = desc.cameraId;
                           }
    );

    // Find the named camera
    CameraRecord *pRecord = findCameraById(cameraId);

    // Is the display being destroyed actually the one we think is active?
    if (!pRecord) {
        ALOGE("Asked to close a camera whose name isn't recognized");
    } else {
        sp<EvsCamera> pActiveCamera = pRecord->activeInstance.promote();

        if (pActiveCamera == nullptr) {
            ALOGE("Somehow a camera is being destroyed when the enumerator didn't know one existed");
        } else if (pActiveCamera != pCamera) {
            // This can happen if the camera was aggressively reopened, orphaning this previous instance
            ALOGW("Ignoring close of previously orphaned camera - why did a client steal?");
        } else {
            // Drop the active camera
            pActiveCamera->shutdown();
            pRecord->activeInstance = nullptr;
        }
    }

    return Void();
}


Return<sp<IEvsDisplay>> EvsEnumerator::openDisplay() {
    ALOGD("openDisplay");

    // If we already have a display active, then we need to shut it down so we can
    // give exclusive access to the new caller.
    sp<EvsDisplay> pActiveDisplay = sActiveDisplay.promote();
    if (pActiveDisplay != nullptr) {
        ALOGW("Killing previous display because of new caller");
        closeDisplay(pActiveDisplay);
    }

    // Create a new display interface and return it
    pActiveDisplay = new EvsDisplay();
    sActiveDisplay = pActiveDisplay;

    ALOGD("Returning new EvsDisplay object %p", pActiveDisplay.get());
    return pActiveDisplay;
}


Return<void> EvsEnumerator::closeDisplay(const ::android::sp<IEvsDisplay>& pDisplay) {
    ALOGD("closeDisplay");

    // Do we still have a display object we think should be active?
    sp<EvsDisplay> pActiveDisplay = sActiveDisplay.promote();
    if (pActiveDisplay == nullptr) {
        ALOGE("Somehow a display is being destroyed when the enumerator didn't know one existed");
    } else if (sActiveDisplay != pDisplay) {
        ALOGW("Ignoring close of previously orphaned display - why did a client steal?");
    } else {
        // Drop the active display
        pActiveDisplay->forceShutdown();
        sActiveDisplay = nullptr;
    }

    return Void();
}


Return<DisplayState> EvsEnumerator::getDisplayState()  {
    ALOGD("getDisplayState");

    // Do we still have a display object we think should be active?
    sp<IEvsDisplay> pActiveDisplay = sActiveDisplay.promote();
    if (pActiveDisplay != nullptr) {
        return pActiveDisplay->getDisplayState();
    } else {
        return DisplayState::NOT_OPEN;
    }
}

EvsEnumerator::CameraRecord* EvsEnumerator::findCameraById(const std::string& cameraId) {
    // Find the named camera
    for (auto &&cam : sCameraList) {
        if (cam.desc.cameraId == cameraId) {
            // Found a match!
            return &cam;
        }
    }

    // We didn't find a match
    return nullptr;
}

} // namespace kingfisher
} // namespace V1_0
} // namespace evs
} // namespace automotive
} // namespace hardware
} // namespace android
