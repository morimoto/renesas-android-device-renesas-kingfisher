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
#ifndef ANDROID_HARDWARE_BROADCASTRADIO_V2_0_TUNERSESSION_H
#define ANDROID_HARDWARE_BROADCASTRADIO_V2_0_TUNERSESSION_H

#include <android/hardware/broadcastradio/2.0/ITunerCallback.h>
#include <android/hardware/broadcastradio/2.0/ITunerSession.h>
#include <broadcastradio-utils-2x/Utils.h>
#include <broadcastradio-utils/WorkerThread.h>

#include <optional>

namespace android {
namespace hardware {
namespace broadcastradio {
namespace V2_0 {
namespace kingfisher {

class BroadcastRadio;

using utils::FrequencyBand;

class TunerSession : public ITunerSession {
public:
    TunerSession(BroadcastRadio& module, const sp<ITunerCallback>& callback, const int& deviceFd);

    // V2_0::ITunerSession methods
    virtual Return<Result> tune(const ProgramSelector& program) override;
    virtual Return<Result> scan(bool directionUp, bool skipSubChannel) override;
    virtual Return<Result> step(bool directionUp) override;
    virtual Return<void> cancel() override;
    virtual Return<Result> startProgramListUpdates(const ProgramFilter& filter) override;
    virtual Return<void> stopProgramListUpdates() override;
    virtual Return<void> isConfigFlagSet(ConfigFlag flag, isConfigFlagSet_cb _hidl_cb) override;
    virtual Return<Result> setConfigFlag(ConfigFlag flag, bool value) override;
    virtual Return<void> setParameters(const hidl_vec<VendorKeyValue>& parameters,
                                       setParameters_cb _hidl_cb) override;
    virtual Return<void> getParameters(const hidl_vec<hidl_string>& keys,
                                       getParameters_cb _hidl_cb) override;
    virtual Return<void> close() override;

    std::optional<AmFmBandRange> getAmFmRangeLocked() const;

    void launchMetadataFetchTask();
    void switchAmFmBand(const FrequencyBand& band);

private:
    ~TunerSession();

    void cancelLocked();
    bool gatherProgramInfo(ProgramInfo& info);
    const BroadcastRadio& module() const;
    void tuneInternalLocked(const ProgramSelector& sel);


    std::mutex                              mMut;
    WorkerThread                            mThread;
    std::atomic<bool>                       mMetadataThreadExit { false };
    bool                                    mIsClosed = false;
    std::function<void()>                   mMetadataTask;

    const sp<ITunerCallback>                mCallback;

    const int&                              mDeviceFd;
    std::reference_wrapper<BroadcastRadio>  mModule;

    bool                                    mIsTuneCompleted = false;
    ProgramSelector                         mCurrentProgram = {};
    ProgramInfo                             mCurrentProgramInfo = {};
    FrequencyBand                           mCurrentBand = FrequencyBand::UNKNOWN;
};

}  // namespace kingfisher {
}  // namespace V2_0 {
}  // namespace broadcastradio {
}  // namespace hardware {
}  // namespace android {

#endif  // ANDROID_HARDWARE_BROADCASTRADIO_V2_0_TUNERSESSION_H
