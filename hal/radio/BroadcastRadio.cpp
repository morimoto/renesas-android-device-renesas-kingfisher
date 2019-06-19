/*
 * Copyright (C) 2018 GlobalLogic LLC
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

#define LOG_TAG "BroadcastRadioKingfisher"
#define LOG_NDEBUG 0

#include <linux/videodev2.h>

#include <log/log.h>
#include <hardware/radio.h>

#include "BroadcastRadio.h"
#include "TunerSession.h"
#include "resources.h"

namespace android {
namespace hardware {
namespace broadcastradio {
namespace V2_0 {
namespace kingfisher {

static const AmFmRegionConfig gDefaultAmFmConfig = {  //
    {
        {65800, 108000, 100, 100},  // FM
        {531, 26100, 9, 9},          // AM
    },
    static_cast<uint32_t>(Deemphasis::D50),
    static_cast<uint32_t>(Rds::RDS)};

static Properties initProperties() {
    Properties properties;

    properties.maker = "GlobalLogic";
    properties.product = "Kingfisher Radio HAL";
    properties.version = "2.0";
    properties.serial = "1234567890";

    properties.supportedIdentifierTypes = hidl_vec<uint32_t>(
        {
            static_cast<uint32_t>(IdentifierType::AMFM_FREQUENCY),
            static_cast<uint32_t>(IdentifierType::RDS_PI),
        });
    return properties;
}

BroadcastRadio::BroadcastRadio() :
    mProperties(initProperties()),
    mAmFmConfig(gDefaultAmFmConfig) {
    init();
}


BroadcastRadio::~BroadcastRadio() {
    ALOGV("%s", __func__);

    if (mFd != -1) {
        close(mFd);
        mFd = -1;
    }
}

void BroadcastRadio::init() {
    ALOGV("%s", __func__);

    if (mFd != -1) { /* Already open? */
        return;
    }

    const char* radio_device = "/dev/radio0";

    mFd = open(radio_device, O_RDONLY);
    if (mFd < 0) {
        ALOGE("Open radio device '%s' failed, err=%s", radio_device, strerror(errno));
        return;
    }

    ALOGI("Radio device '%s', fd=%d", radio_device, mFd);
}

// Methods from ::android::hardware::broadcastradio::V1_0::IBroadcastRadio follow.
Return<void> BroadcastRadio::getProperties(getProperties_cb _hidl_cb) {
    ALOGD("%s", __func__);

    _hidl_cb(mProperties);
    return {};
}

AmFmRegionConfig BroadcastRadio::getAmFmConfig() const {
    std::lock_guard<std::mutex> lk(mMut);
    return mAmFmConfig;
}

Return<void> BroadcastRadio::openSession(const sp<ITunerCallback>& callback,
                                         openSession_cb _hidl_cb) {
    ALOGV("%s", __func__);

    std::lock_guard<std::mutex> lk(mMut);

    auto oldSession = mSession.promote();
    if (oldSession != nullptr) {
        ALOGI("Closing previously opened tuner");
        oldSession->close();
        mSession = nullptr;
    }

    sp<TunerSession> newSession = new TunerSession(*this, callback, mFd);
    mSession = newSession;
    newSession->switchAmFmBand(FrequencyBand::FM);

    _hidl_cb(Result::OK, newSession);
    return {};
}

Return<void> BroadcastRadio::getAmFmRegionConfig(bool full, getAmFmRegionConfig_cb _hidl_cb) {
    ALOGV("%s(%d)", __func__, full);

    if (mFd < 0) {
        _hidl_cb(Result::INVALID_STATE, mAmFmConfig);
        return {};
    }

    if (full) {
        AmFmRegionConfig config = {};
        config.ranges = hidl_vec<AmFmBandRange>(2);
        config.fmDeemphasis = Deemphasis::D50 | Deemphasis::D75;
        config.fmRds = Rds::RDS | Rds::RBDS;

        int ret = 0;
        for (int band = 0;; band++) {
            struct v4l2_frequency_band v4l2_band = {
                .type = V4L2_TUNER_RADIO,
                .tuner = 0,
                .index = static_cast<__u32>(band)
            };

            ret = ioctl(mFd, VIDIOC_ENUM_FREQ_BANDS, &v4l2_band);
            if (ret < 0) {
                ALOGE("ioctl(VIDIOC_ENUM_FREQ_BANDS) failed, err=%s", strerror(errno));
                break;
            }

            config.ranges.resize(band + 1);

            AmFmBandRange& range = config.ranges[band];

            range.lowerBound = v4l2_band.rangelow / 16U;
            range.upperBound = v4l2_band.rangehigh / 16U;
            range.scanSpacing = 0;

            if (v4l2_band.modulation == V4L2_BAND_MODULATION_FM) {
                range.spacing = 100;
            } else if (v4l2_band.modulation == V4L2_BAND_MODULATION_AM) {
                range.spacing = 9;
            }
        }

        _hidl_cb(Result::OK, config);
        return {};
    } else {
        _hidl_cb(Result::OK, getAmFmConfig());
        return {};
    }
}

Return<void> BroadcastRadio::getDabRegionConfig(getDabRegionConfig_cb _hidl_cb) {
    ALOGV("%s", __func__);
    _hidl_cb(Result::NOT_SUPPORTED, {});
    return {};
}

Return<void> BroadcastRadio::getImage(uint32_t id, getImage_cb _hidl_cb) {
    ALOGV("%s(%x)", __func__, id);

    if (id == resources::demoPngId) {
        _hidl_cb(std::vector<uint8_t>(resources::demoPng, std::end(resources::demoPng)));
        return {};
    }

    ALOGI("Image %x doesn't exists", id);
    _hidl_cb({});
    return {};
}

Return<void> BroadcastRadio::registerAnnouncementListener(
    const hidl_vec<AnnouncementType>& enabled, const sp<IAnnouncementListener>& /* listener */,
    registerAnnouncementListener_cb _hidl_cb) {
    ALOGV("%s(%s)", __func__, toString(enabled).c_str());

    _hidl_cb(Result::NOT_SUPPORTED, nullptr);
    return {};
}


}  // namespace kingfisher
}  // namespace V2_0
}  // namespace broadcastradio
}  // namespace hardware
}  // namespace android
