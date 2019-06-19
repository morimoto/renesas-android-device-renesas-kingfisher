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

#define LOG_TAG "BcRadioDef.TunerSession"
#define LOG_NDEBUG 0

#include "TunerSession.h"

#include "BroadcastRadio.h"

#include <broadcastradio-utils-2x/Utils.h>
#include <log/log.h>
#include <linux/videodev2.h>

namespace android {
namespace hardware {
namespace broadcastradio {
namespace V2_0 {
namespace kingfisher {

/* ------------------------------------------------------------------ */
typedef struct si46xx_rds_data_s {
    uint16_t pi;
    uint8_t pty;
    char ps_name[9];
    char radiotext[129];
} si46xx_rds_data_t;

/* ------------------------------------------------------------------ */

using utils::FrequencyBand;
using utils::tunesTo;

using std::lock_guard;
using std::move;
using std::mutex;
using std::sort;
using std::vector;

using namespace std::chrono_literals;

namespace delay {

static constexpr auto config = 5ms;
static constexpr auto seek = 200ms;
static constexpr auto step = 100ms;
static constexpr auto tune = 150ms;
static constexpr auto metadataCheck = 1s;

}  // namespace delay

static ProgramInfo makeDummyProgramInfo(const ProgramSelector& selector) {
    ProgramInfo info = {};
    info.selector = selector;
    info.logicallyTunedTo = utils::make_identifier(
        IdentifierType::AMFM_FREQUENCY,
        utils::getId(selector, IdentifierType::AMFM_FREQUENCY));
    info.physicallyTunedTo = info.logicallyTunedTo;
    return info;
}

TunerSession::TunerSession(BroadcastRadio& module,
                           const sp<ITunerCallback>& callback,
                           const int& deviceFd)
    : mCallback(callback),
      mDeviceFd(deviceFd),
      mModule(module) {
}

TunerSession::~TunerSession() {
    mMetadataThreadExit = true;   // Notify thread to finish and wait for it to terminate.
}

const BroadcastRadio& TunerSession::module() const {
    return mModule.get();
}

void TunerSession::tuneInternalLocked(const ProgramSelector& sel) {
    ALOGV("%s(%s)", __func__, toString(sel).c_str());

    mCurrentProgram = sel;

    ProgramInfo info = makeDummyProgramInfo(sel);

    ALOGI("Tune channel type=%d, value=%ld", sel.primaryId.type, sel.primaryId.value);

    v4l2_frequency freq = {
        .tuner = 0,
        .type = V4L2_TUNER_RADIO,
        .frequency = static_cast<uint32_t>(sel.primaryId.value)
    };

    if (ioctl(mDeviceFd, VIDIOC_S_FREQUENCY, &freq) < 0) {
        ALOGE("ioctl(VIDIOC_S_FREQUENCY) failed, err=%s", strerror(errno));
        mCallback->onTuneFailed(Result::INTERNAL_ERROR, sel);
    } else {
        if (gatherProgramInfo(info)) {
            info.selector = sel;
            info.metadata = hidl_vec<Metadata>(
                {
                    utils::make_metadata(MetadataKey::RDS_PTY, 25689),
                    utils::make_metadata(MetadataKey::RDS_PS, "RDS TEXT")
                });
            mCurrentProgramInfo = info;
        }
        launchMetadataFetchTask();
    }
    ALOGI("Tune channel %ld done, tuned", sel.primaryId.value);
    mIsTuneCompleted = true;
    mCallback->onCurrentProgramInfoChanged(info);
}

void TunerSession::switchAmFmBand(const FrequencyBand& band) {
    ALOGV("%s(band = %d)", __func__, band);

    auto task = [this, band]() {
        ALOGV("%s(band = %d)", __func__, band);
        std::lock_guard<std::mutex> lk(mMut);

        v4l2_control ctrl = {.id = V4L2_TUNER_RADIO};

        uint32_t baseFrequency = 0;

        if (band == FrequencyBand::FM) {
            ALOGI("Setting FM band config");
            ctrl.value = V4L2_BAND_MODULATION_FM;
            baseFrequency = 87500;
        } else if (band == FrequencyBand::AM_LW || band == FrequencyBand::AM_MW ||
                   band == FrequencyBand::AM_SW) {
            ALOGI("Setting AM band config");
            ctrl.value = V4L2_BAND_MODULATION_AM;
            baseFrequency = 1620;
        }

        if (ioctl(mDeviceFd, VIDIOC_S_CTRL, &ctrl) < 0) {
            ALOGE("ioctl(VIDIOC_S_CTRL) failed, err=%s", strerror(errno));
        } else {
            ALOGI("Switched to frequency %d", band);
            mCurrentBand = band;

            ALOGI("Tune to band channel %ld", mCurrentProgram.primaryId.value);
            v4l2_frequency freq = {
                .tuner = 0,
                .type = V4L2_TUNER_RADIO,
                .frequency = baseFrequency
            };

            if (ioctl(mDeviceFd, VIDIOC_S_FREQUENCY, &freq) < 0) {
                ALOGE("ioctl(VIDIOC_S_FREQUENCY) failed, err=%s", strerror(errno));
            } else {
                ALOGV("ioctl(VIDIOC_S_FREQUENCY) success");
            }
        }
    };

    mThread.schedule(task, delay::config);
}

Return<Result> TunerSession::tune(const ProgramSelector& sel) {

    ALOGV("%s: type=%u, value=%lu", __func__, sel.primaryId.type, sel.primaryId.value);

    std::lock_guard<std::mutex> lk(mMut);
    if (mIsClosed) return Result::INVALID_STATE;

    if (!utils::isSupported(module().getProperties(), sel)) {
        ALOGW("Selector not supported");
        return Result::NOT_SUPPORTED;
    }

    if (!utils::isValid(sel)) {
        ALOGE("ProgramSelector is not valid");
        return Result::INVALID_ARGUMENTS;
    }

    cancelLocked();

    FrequencyBand newBand = utils::getBand(sel.primaryId.value);
    if (newBand != mCurrentBand) {
        switchAmFmBand(newBand);
    }

    mIsTuneCompleted = false;

    auto task = [this, sel]() {
        std::lock_guard<std::mutex> lk(mMut);
        tuneInternalLocked(sel);
    };

    mThread.schedule(task, delay::tune);

    return Result::OK;
}

Return<Result> TunerSession::scan(bool directionUp, bool /* skipSubChannel */) {
    ALOGV("%s: direction=%d", __func__, directionUp);

    std::lock_guard<mutex> lk(mMut);
    if (mIsClosed) return Result::INVALID_STATE;

    cancelLocked();

    if (mCurrentBand == FrequencyBand::UNKNOWN) {
        switchAmFmBand(FrequencyBand::FM);
    }

    mIsTuneCompleted = false;

    auto task = [this, directionUp]() {
        std::lock_guard<std::mutex> lk(mMut);
        ProgramInfo info;

        ALOGI("Seek start, direction=%d", directionUp);

        v4l2_hw_freq_seek freq_seek = {
            .tuner = 0,
            .type = V4L2_TUNER_RADIO,
            .seek_upward = directionUp,
            .wrap_around = 1,
        };

        if (ioctl(mDeviceFd, VIDIOC_S_HW_FREQ_SEEK, &freq_seek) < 0) {
            ALOGE("ioctl(VIDIOC_S_HW_FREQ_SEEK) failed, err=%s", strerror(errno));
        } else {
            if (gatherProgramInfo(info)) {
                mCurrentProgram = info.selector;
                mCurrentProgramInfo = info;
            }
            launchMetadataFetchTask();
        }

        ALOGI("Seek done at channel %ld", mCurrentProgram.primaryId.value);
        mIsTuneCompleted = true;
        mCallback->onCurrentProgramInfoChanged(info);
    };

    mThread.schedule(task, delay::seek);
    return Result::OK;
}

Return<Result> TunerSession::step(bool directionUp) {
    ALOGV("%s", __func__);
    std::lock_guard<mutex> lk(mMut);
    if (mIsClosed) return Result::INVALID_STATE;

    cancelLocked();

    if (!utils::hasId(mCurrentProgram, IdentifierType::AMFM_FREQUENCY)) {
        ALOGE("Can't step in anything else than AM/FM");
        return Result::NOT_SUPPORTED;
    }

    auto stepTo = utils::getId(mCurrentProgram, IdentifierType::AMFM_FREQUENCY);
    auto range = getAmFmRangeLocked();
    if (!range) {
        ALOGE("Can't find current band");
        return Result::INTERNAL_ERROR;
    }

    if (directionUp) {
        stepTo += range->spacing;
    } else {
        stepTo -= range->spacing;
    }
    if (stepTo > range->upperBound) stepTo = range->lowerBound;
    if (stepTo < range->lowerBound) stepTo = range->upperBound;

    mIsTuneCompleted = false;
    auto task = [this, stepTo]() {
        ALOGI("Performing step to %s", std::to_string(stepTo).c_str());

        std::lock_guard<mutex> lk(mMut);
        tuneInternalLocked(utils::make_selector_amfm(stepTo));
    };
    mThread.schedule(task, delay::step);

    return Result::OK;
}

void TunerSession::cancelLocked() {
    ALOGV("%s", __func__);

    mMetadataThreadExit = true;
    mThread.cancelAll();
    if (utils::getType(mCurrentProgram.primaryId) != IdentifierType::INVALID) {
        mIsTuneCompleted = true;
    }
}

Return<void> TunerSession::cancel() {
    ALOGV("%s", __func__);
    std::lock_guard<mutex> lk(mMut);
    if (mIsClosed) return {};

    cancelLocked();

    return {};
}

Return<Result> TunerSession::startProgramListUpdates(const ProgramFilter& filter) {
    ALOGV("%s(%s)", __func__, toString(filter).c_str());
    std::lock_guard<mutex> lk(mMut);
    if (mIsClosed) return Result::INVALID_STATE;

    return Result::NOT_SUPPORTED;
}

Return<void> TunerSession::stopProgramListUpdates() {
    ALOGV("%s", __func__);
    return {};
}

Return<void> TunerSession::isConfigFlagSet(ConfigFlag flag, isConfigFlagSet_cb _hidl_cb) {
    ALOGV("%s(%s)", __func__, toString(flag).c_str());

    _hidl_cb(Result::NOT_SUPPORTED, false);
    return {};
}

Return<Result> TunerSession::setConfigFlag(ConfigFlag flag, bool value) {
    ALOGV("%s(%s, %d)", __func__, toString(flag).c_str(), value);

    return Result::NOT_SUPPORTED;
}

Return<void> TunerSession::setParameters(const hidl_vec<VendorKeyValue>& /* parameters */,
                                         setParameters_cb _hidl_cb) {
    ALOGV("%s", __func__);

    _hidl_cb({});
    return {};
}

Return<void> TunerSession::getParameters(const hidl_vec<hidl_string>& /* keys */,
                                         getParameters_cb _hidl_cb) {
    ALOGV("%s", __func__);

    _hidl_cb({});
    return {};
}

Return<void> TunerSession::close() {
    ALOGV("%s", __func__);
    std::lock_guard<mutex> lk(mMut);
    if (mIsClosed) return {};

    mIsClosed = true;
    mMetadataThreadExit = true;
    mThread.cancelAll();
    return {};
}

std::optional<AmFmBandRange> TunerSession::getAmFmRangeLocked() const {
    if (!mIsTuneCompleted) {
        ALOGW("tune operation in process");
        return {};
    }
    if (!utils::hasId(mCurrentProgram, IdentifierType::AMFM_FREQUENCY)) return {};

    auto freq = utils::getId(mCurrentProgram, IdentifierType::AMFM_FREQUENCY);
    for (auto&& range : module().getAmFmConfig().ranges) {
        if (range.lowerBound <= freq && range.upperBound >= freq) return range;
    }

    return {};
}

bool TunerSession::gatherProgramInfo(ProgramInfo& info) {
    /* Get the frequency */
    v4l2_frequency freq = {
        .tuner = 0,
        .type = V4L2_TUNER_RADIO,
        .frequency = 0
    };

    if (ioctl(mDeviceFd, VIDIOC_G_FREQUENCY, &freq) < 0) {
        ALOGE("ioctl(VIDIOC_G_FREQUENCY) failed, err=%s", strerror(errno));
        return false;
    }

    /* Get signal quality */
    v4l2_tuner v4ltun = {
        .index = 0,
        .type = V4L2_TUNER_RADIO,
        .signal = 0,
        .audmode = 0
    };

    if (ioctl(mDeviceFd, VIDIOC_G_TUNER, &v4ltun) < 0) {
        ALOGE("ioctl(VIDIOC_G_TUNER) failed, err=%s", strerror(errno));
        return false;
    }


    info.signalQuality = v4ltun.signal;
    info.selector = utils::make_selector_amfm(freq.frequency);
    if (v4ltun.audmode == V4L2_TUNER_MODE_STEREO) {
        info.infoFlags |= ProgramInfoFlags::STEREO;
    }

    return true;
}

void TunerSession::launchMetadataFetchTask() {
    if (mIsClosed) {
        ALOGW("called metadata fetch in wrong state");
        return;
    }
    mMetadataThreadExit = false;
    mMetadataTask = [this]() {
        struct si46xx_rds_data_s rds = {};
        int ret = read(mDeviceFd, &rds, sizeof(rds));
        if (ret < 0) {
            ALOGE("RDS data read failed, err=%s", strerror(errno));
        } else if (!ret) {
            ALOGV("No RDS data available.");
        } else {
            mCurrentProgramInfo.metadata = hidl_vec<Metadata>(
                {
                    utils::make_metadata(MetadataKey::RDS_PTY, rds.pty),
                    utils::make_metadata(MetadataKey::RDS_PS, rds.ps_name),
                });
            mCallback->onCurrentProgramInfoChanged(mCurrentProgramInfo);
        }

        if (!mMetadataThreadExit) {
            mThread.schedule(mMetadataTask, delay::metadataCheck);
        }
    };
    mThread.schedule(mMetadataTask, delay::metadataCheck);
}

}  // namespace kingfisher {
}  // namespace V2_0 {
}  // namespace broadcastradio {
}  // namespace hardware {
}  // namespace android {
