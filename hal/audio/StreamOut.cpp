/*
 * Copyright (C) 2016 The Android Open Source Project
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

#define LOG_TAG "StreamOutHAL"
//#define LOG_NDEBUG 0
#define ATRACE_TAG ATRACE_TAG_AUDIO

#include <memory>

#include <android/log.h>
#include <hardware/audio.h>
#include <utils/Trace.h>

#include "StreamOut.h"
#include "Util.h"

#include "audio_hw_defs.h"

#include <audio_utils/primitives.h>
#include <media/AudioParameter.h>
namespace android {

#define AUDIO_PARAMETER_KEY__VOLUME "__volume__"
#define AUDIO_PARAMETER_KEY__DEFAULT_VOLUME "__defvolume__"

namespace hardware {
namespace audio {
namespace AUDIO_HAL_VERSION {
namespace kingfisher {

using ::android::hardware::audio::common::AUDIO_HAL_VERSION::ThreadInfo;

namespace {

// -------------------------------------------------------- class CommonStreamOutThread
class CommonStreamOutThread : public Thread {
    public:
        CommonStreamOutThread(std::atomic<bool>* stop, audio_stream_out_t* stream,
                StreamOut::CommandMQ* commandMQ, StreamOut::DataMQ* dataMQ,
                StreamOut::StatusMQ* statusMQ, EventFlag* efGroup)
            : Thread(false /*canCallJava*/),
              mEfGroupOther(nullptr), mCommandOther(nullptr), mDataOther(nullptr),
	          mStop(stop),
	          mStream(stream),
	          mCommandMQ(commandMQ),
	          mDataMQ(dataMQ),
	          mStatusMQ(statusMQ),
	          mEfGroup(efGroup),
	          mBuffer(nullptr),
	          mNanoseconds(0) {}

        bool init() {
            mBuffer.reset(new (std::nothrow) uint8_t[mDataMQ->getQuantumCount()]);
            return mBuffer != nullptr;
        }

        virtual ~CommonStreamOutThread() {}

    public:
        void setOtherThread(EventFlag* efOther, StreamOut::CommandMQ* cmdOther,
                StreamOut::DataMQ* dataOther) {
            mEfGroupOther = efOther;
            mCommandOther = cmdOther;
            mDataOther = dataOther;
        }

    protected:
        EventFlag* mEfGroupOther;
        StreamOut::CommandMQ* mCommandOther;
        StreamOut::DataMQ* mDataOther;

        std::atomic<bool>* mStop;
        audio_stream_out_t* mStream;
        StreamOut::CommandMQ* mCommandMQ;
        StreamOut::DataMQ* mDataMQ;
        StreamOut::StatusMQ* mStatusMQ;
        EventFlag* mEfGroup;
        std::unique_ptr<uint8_t[]> mBuffer;
        IStreamOut::WriteStatus mStatus;

        int64_t mNanoseconds;
};

// -------------------------------------------------------- class WriteThread
class WriteThread : public Thread {
   public:
    // WriteThread's lifespan never exceeds StreamOut's lifespan.
    WriteThread(std::atomic<bool>* stop, audio_stream_out_t* stream,
                StreamOut::CommandMQ* commandMQ, StreamOut::DataMQ* dataMQ,
                StreamOut::DataMQ* dataHfpMQ,
                StreamOut::StatusMQ* statusMQ, EventFlag* efGroup)
        : Thread(false /*canCallJava*/),
          mStop(stop),
          mStream(stream),
          mCommandMQ(commandMQ),
          mDataMQ(dataMQ),
          mDataHfpMQ(dataHfpMQ),
          mStatusMQ(statusMQ),
          mEfGroup(efGroup),
          mBuffer(nullptr) {}
    bool init() {
        mBuffer.reset(new (std::nothrow) uint8_t[mDataMQ->getQuantumCount()]);
        return mBuffer != nullptr;
    }
    virtual ~WriteThread() {}

   private:
    std::atomic<bool>* mStop;
    audio_stream_out_t* mStream;
    StreamOut::CommandMQ* mCommandMQ;
    StreamOut::DataMQ* mDataMQ;
    StreamOut::DataMQ* mDataHfpMQ;
    StreamOut::StatusMQ* mStatusMQ;
    EventFlag* mEfGroup;
    std::unique_ptr<uint8_t[]> mBuffer;
    IStreamOut::WriteStatus mStatus;

    bool threadLoop() override;

    void doGetLatency();
    void doGetPresentationPosition();
    void doWrite();

    void doWriteFm();
    void doWriteHfp();
};

void WriteThread::doWrite() {
    const size_t availToRead = mDataMQ->availableToRead();
    mStatus.retval = Result::OK;
    mStatus.reply.written = 0;
    if (mDataMQ->read(&mBuffer[0], availToRead)) {
        ssize_t writeResult = mStream->write(mStream, &mBuffer[0], availToRead);
        if (writeResult >= 0) {
            mStatus.reply.written = writeResult;
        } else {
            mStatus.retval = Stream::analyzeStatus("write", writeResult);
        }
    }
}

void WriteThread::doGetPresentationPosition() {
    mStatus.retval = StreamOut::getPresentationPositionImpl(
        mStream, &mStatus.reply.presentationPosition.frames,
        &mStatus.reply.presentationPosition.timeStamp);
}

void WriteThread::doGetLatency() {
    mStatus.retval = Result::OK;
    mStatus.reply.latencyMs = mStream->get_latency(mStream);
}

void WriteThread::doWriteFm() {
    mStatus.retval = Result::OK;
    size_t availToRead = mDataMQ->availableToRead();
    mDataMQ->read(&mBuffer[0], availToRead);
    mStream->write(mStream, &mBuffer[0], availToRead);
}

void WriteThread::doWriteHfp() {
    mStatus.retval = Result::OK;
    StreamOut::SHfpBuffer hfpBuffer;
    if (mDataHfpMQ->availableToRead() < sizeof(StreamOut::SHfpBuffer)) {
        return;
    }
    while(mDataHfpMQ->availableToRead() > sizeof(StreamOut::SHfpBuffer)) {
        mDataHfpMQ->read((uint8_t*)&hfpBuffer, sizeof(hfpBuffer));
        if (hfpBuffer.mData) {
            free(hfpBuffer.mData);
        }
    }
    mDataHfpMQ->read((uint8_t*)&hfpBuffer, sizeof(hfpBuffer));
    if (hfpBuffer.mData && hfpBuffer.mSize.mSize) {
        mStream->write(mStream, hfpBuffer.mData, hfpBuffer.mSize.mSize);
        free(hfpBuffer.mData);
    }
}

bool WriteThread::threadLoop() {
    // This implementation doesn't return control back to the Thread until it
    // decides to stop,
    // as the Thread uses mutexes, and this can lead to priority inversion.
    while (!std::atomic_load_explicit(mStop, std::memory_order_acquire)) {
        uint32_t efState = 0;
        mEfGroup->wait(static_cast<uint32_t>(MessageQueueFlagBits::NOT_EMPTY),
                       &efState);
        if (!(efState &
              static_cast<uint32_t>(MessageQueueFlagBits::NOT_EMPTY))) {
            continue;  // Nothing to do.
        }
        if (!mCommandMQ->read(&mStatus.replyTo)) {
            continue;  // Nothing to do.
        }
        switch (mStatus.replyTo) {
            case IStreamOut::WriteCommand::WRITE:
                doWrite();
                break;
            case IStreamOut::WriteCommand::GET_PRESENTATION_POSITION:
                doGetPresentationPosition();
                break;
            case IStreamOut::WriteCommand::GET_LATENCY:
                doGetLatency();
                break;
            case (IStreamOut::WriteCommand)StreamOut::FM_BUFFER_READY:
                doWriteFm();
                break;
            case (IStreamOut::WriteCommand)StreamOut::HFP_BUFFER_READY:
                doWriteHfp();
                break;
            default:
                ALOGE("Unknown write thread command code %d", mStatus.replyTo);
                mStatus.retval = Result::NOT_SUPPORTED;
                break;
        }
        if (mStatus.replyTo < (IStreamOut::WriteCommand)StreamOut::FM_DISCONNECTED) {
            int64_t status_send_delay = 0;
            while ((!mStatusMQ->write(&mStatus)) && (status_send_delay < kStatusSendTimeout)) {
                status_send_delay += kStatusSendDelay;
                usleep(kStatusSendDelay);
                ALOGE("status message queue write failed");
            }
            mEfGroup->wake(static_cast<uint32_t>(MessageQueueFlagBits::NOT_FULL));
        }
    }

    return false;
}

// -------------------------------------------------------- class ReadFmThread
class ReadFmThread : public CommonStreamOutThread {
   public:
    // WriteThread's lifespan never exceeds StreamOut's lifespan.
    ReadFmThread(std::atomic<bool>* stop, audio_stream_out_t* stream,
                StreamOut::CommandMQ* commandMQ, StreamOut::DataMQ* dataMQ,
                StreamOut::StatusMQ* statusMQ, EventFlag* efGroup)
        : CommonStreamOutThread(stop, stream, commandMQ, dataMQ, statusMQ,
                efGroup),
                mVolume(0.0) {}

    virtual ~ReadFmThread() {}

   private:

    bool threadLoop() override;

    void doFm();

    void applyVolume(char* buffer, size_t avail);

   private:
    float mVolume;
};

void ReadFmThread::doFm() {
    mStatus.retval = Result::OK;
    char* buffer = mStream->common.get_parameters(&mStream->common, "fmread");
    if (buffer) {
        IStreamOut::WriteCommand cmd = (IStreamOut::WriteCommand)StreamOut::FM_BUFFER_READY;
        size_t avail = mDataOther->availableToWrite();
        applyVolume(buffer, avail);
        if (mNanoseconds != 0) {
            mCommandOther->write(&cmd);
            mDataOther->write((uint8_t*)buffer, avail);
            mEfGroupOther->wake(static_cast<uint32_t>(MessageQueueFlagBits::NOT_EMPTY));
        }
        free(buffer);
    } else {
        mStream->common.standby(&mStream->common);
    }
}

void ReadFmThread::applyVolume(char* buffer, size_t avail) {
    int16_t* volume_buffer = reinterpret_cast<int16_t*>(buffer);
    size_t size = avail /sizeof(int16_t);
    static const float float_from_q_15 = 1. / (1 << 15);
    for (int counter = 0; counter < size; counter+=2) {
        float float_value_left = volume_buffer[counter] * mVolume * float_from_q_15;
        volume_buffer[counter] = clamp16_from_float(float_value_left);
        float float_value_right = volume_buffer[counter + 1] * mVolume * float_from_q_15;
        volume_buffer[counter + 1] = clamp16_from_float(float_value_right);
    }
}

bool ReadFmThread::threadLoop() {
    // This implementation doesn't return control back to the Thread until it
    // decides to stop,
    // as the Thread uses mutexes, and this can lead to priority inversion.
    while (!std::atomic_load_explicit(mStop, std::memory_order_acquire)) {
        uint32_t efState = 0;
        mEfGroup->wait(static_cast<uint32_t>(MessageQueueFlagBits::NOT_EMPTY),
                       &efState, mNanoseconds);
        if (!(efState &
              static_cast<uint32_t>(MessageQueueFlagBits::NOT_EMPTY))) {
            if (mNanoseconds) {
                doFm();
            }
            continue;  // Nothing to do.
        }
        if (!mCommandMQ->read(&mStatus.replyTo)) {
            continue;  // Nothing to do.
        }
        switch (mStatus.replyTo) {
            case (IStreamOut::WriteCommand)StreamOut::FM_DISCONNECTED:
                ALOGV("%s: mStatus.replyTo = %d", __FUNCTION__, mStatus.replyTo);
                mStatus.retval = Result::OK;
                mNanoseconds = 0;
                break;
            case (IStreamOut::WriteCommand)StreamOut::FM_CONNECTED:
                ALOGV("%s: mStatus.replyTo = %d", __FUNCTION__, mStatus.replyTo);
                mStatus.retval = Result::OK;
                mDataMQ->read((uint8_t*)&mVolume, sizeof(mVolume));
                mNanoseconds = 100000;
                usleep(500000);
                break;
            case (IStreamOut::WriteCommand)StreamOut::FM_VOLUME:
                ALOGE("%s: mStatus.replyTo = %d", __FUNCTION__, mStatus.replyTo);
                mStatus.retval = Result::OK;
                mDataMQ->read((uint8_t*)&mVolume, sizeof(mVolume));
			    break;
            default:
                ALOGE("Unknown write thread command code %d", mStatus.replyTo);
                mStatus.retval = Result::NOT_SUPPORTED;
                break;
        }
    }

    return false;
}

// -------------------------------------------------------- class BTSCOReadThread
class BTSCOReadThread : public CommonStreamOutThread {
   public:
	BTSCOReadThread(std::atomic<bool>* stop, audio_stream_out_t* stream,
                StreamOut::CommandMQ* commandMQ, StreamOut::DataMQ* dataMQ,
                StreamOut::StatusMQ* statusMQ, EventFlag* efGroup,
				const char* param, IStreamOut::WriteCommand command)
        : CommonStreamOutThread(stop, stream, commandMQ, dataMQ, statusMQ,
                efGroup),
                mParam(param),
                mCommand(command) {}

    virtual ~BTSCOReadThread() {}

   private:

    bool threadLoop() override;

    void doRead();

    String8 mParam;
    IStreamOut::WriteCommand mCommand;
    StreamOut::SHfpBufferSize mSize;
};

void BTSCOReadThread::doRead() {
    mStatus.retval = Result::OK;
    char* buffer = mStream->common.get_parameters(&mStream->common, mParam);
    if (buffer) {
        if (!mNanoseconds) {
            free(buffer);
            return;
        }
        mCommandOther->write(&mCommand);
        size_t avail = mDataOther->availableToWrite();
        if (avail >= sizeof(StreamOut::SHfpBuffer)) {
            StreamOut::SHfpBuffer hfpBuffer = { mSize, buffer };
            mDataOther->write((uint8_t*)&hfpBuffer, sizeof(StreamOut::SHfpBuffer));
            mEfGroupOther->wake(static_cast<uint32_t>(MessageQueueFlagBits::NOT_EMPTY));
        } else {
            free(buffer);
        }
    }
}

bool BTSCOReadThread::threadLoop() {
    // This implementation doesn't return control back to the Thread until it
    // decides to stop,
    // as the Thread uses mutexes, and this can lead to priority inversion.
    while (!std::atomic_load_explicit(mStop, std::memory_order_acquire)) {
        uint32_t efState = 0;
        mEfGroup->wait(static_cast<uint32_t>(MessageQueueFlagBits::NOT_EMPTY),
                       &efState, mNanoseconds);
        if (!(efState &
              static_cast<uint32_t>(MessageQueueFlagBits::NOT_EMPTY))) {
            if (mNanoseconds) {
                doRead();
            }
            continue;  // Nothing to do.
        }
        if (!mCommandMQ->read(&mStatus.replyTo)) {
            continue;  // Nothing to do.
        }
        switch (mStatus.replyTo) {
            case (IStreamOut::WriteCommand)StreamOut::HFP_DISCONNECTED:
            {
                ALOGV("%s: mStatus.replyTo = %d", __FUNCTION__, mStatus.replyTo);
                mStatus.retval = Result::OK;
                mNanoseconds = 0;
                break;
            }
            case (IStreamOut::WriteCommand)StreamOut::HFP_CONNECTED:
                ALOGV("%s: mStatus.replyTo = %d", __FUNCTION__, mStatus.replyTo);
                mStatus.retval = Result::OK;
                mDataMQ->read((uint8_t*)&mSize, sizeof(StreamOut::SHfpBufferSize));
                mNanoseconds = 100000;
                usleep(50000);
			    break;
            default:
                ALOGE("Unknown write thread command code %d", mStatus.replyTo);
                mStatus.retval = Result::NOT_SUPPORTED;
                break;
        }
    }

    return false;
}

// -------------------------------------------------------- class MicReadThread
class MicReadThread : public CommonStreamOutThread {
   public:
	MicReadThread(std::atomic<bool>* stop, audio_stream_out_t* stream,
                StreamOut::CommandMQ* commandMQ, StreamOut::DataMQ* dataMQ,
                StreamOut::StatusMQ* statusMQ, EventFlag* efGroup,
				const char* param, IStreamOut::WriteCommand command)
        : CommonStreamOutThread(stop, stream, commandMQ, dataMQ, statusMQ,
                efGroup),
                mParam(param),
                mCommand(command) {}

    virtual ~MicReadThread() {}

   private:

    bool threadLoop() override;

    void doRead();

    String8 mParam;
    IStreamOut::WriteCommand mCommand;
    StreamOut::SHfpBufferSize mSize;
};

void MicReadThread::doRead() {
    mStatus.retval = Result::OK;
    char* buffer = mStream->common.get_parameters(&mStream->common, mParam);
    if (buffer) {
        if (!mNanoseconds) {
            free(buffer);
            return;
        }
        mCommandOther->write(&mCommand);
        size_t avail = mDataOther->availableToWrite();
        if (avail >= sizeof(StreamOut::SHfpBuffer)) {
            StreamOut::SHfpBuffer hfpBuffer = { mSize, buffer };
            mDataOther->write((uint8_t*)&hfpBuffer, sizeof(StreamOut::SHfpBuffer));
            mEfGroupOther->wake(static_cast<uint32_t>(MessageQueueFlagBits::NOT_EMPTY));
        } else {
            free(buffer);
        }
    }
}

bool MicReadThread::threadLoop() {
    // This implementation doesn't return control back to the Thread until it
    // decides to stop,
    // as the Thread uses mutexes, and this can lead to priority inversion.
    while (!std::atomic_load_explicit(mStop, std::memory_order_acquire)) {
        uint32_t efState = 0;
        mEfGroup->wait(static_cast<uint32_t>(MessageQueueFlagBits::NOT_EMPTY),
                       &efState, mNanoseconds);
        if (!(efState &
              static_cast<uint32_t>(MessageQueueFlagBits::NOT_EMPTY))) {
            if (mNanoseconds) {
                doRead();
            }
            continue;  // Nothing to do.
        }
        if (!mCommandMQ->read(&mStatus.replyTo)) {
            continue;  // Nothing to do.
        }
        switch (mStatus.replyTo) {
            case (IStreamOut::WriteCommand)StreamOut::HFP_DISCONNECTED:
                ALOGV("%s: mStatus.replyTo = %d", __FUNCTION__, mStatus.replyTo);
                mStatus.retval = Result::OK;
                mNanoseconds = 0;
                break;
            case (IStreamOut::WriteCommand)StreamOut::HFP_CONNECTED:
                ALOGV("%s: mStatus.replyTo = %d", __FUNCTION__, mStatus.replyTo);
                mStatus.retval = Result::OK;
                mDataMQ->read((uint8_t*)&mSize, sizeof(StreamOut::SHfpBufferSize));
                mNanoseconds = 100000;
                usleep(50000);
			    break;
            default:
                ALOGE("Unknown write thread command code %d", mStatus.replyTo);
                mStatus.retval = Result::NOT_SUPPORTED;
                break;
        }
    }

    return false;
}

// -------------------------------------------------------- class BTSCOWriteThread
class BTSCOWriteThread : public CommonStreamOutThread {
   public:
    // WriteThread's lifespan never exceeds StreamOut's lifespan.
	BTSCOWriteThread(std::atomic<bool>* stop, audio_stream_out_t* stream,
                StreamOut::CommandMQ* commandMQ, StreamOut::DataMQ* dataMQ,
                StreamOut::StatusMQ* statusMQ, EventFlag* efGroup)
        : CommonStreamOutThread(stop, stream, commandMQ, dataMQ, statusMQ,
                efGroup) {}

    virtual ~BTSCOWriteThread() {}

   private:

    bool threadLoop() override;

    void doWrite();
};

void BTSCOWriteThread::doWrite() {
    mStatus.retval = Result::OK;
    StreamOut::SHfpBuffer hfpBuffer;
    if (mDataMQ->availableToRead() < sizeof(StreamOut::SHfpBuffer)) {
        return;
    }
    while(mDataMQ->availableToRead() > sizeof(StreamOut::SHfpBuffer)) {
        mDataMQ->read((uint8_t*)&hfpBuffer, sizeof(hfpBuffer));
        if (hfpBuffer.mData) {
            free(hfpBuffer.mData);
        }
    }
    mDataMQ->read((uint8_t*)&hfpBuffer, sizeof(hfpBuffer));
    if (hfpBuffer.mData && hfpBuffer.mSize.mSize) {
        audio_stream_out_t* hfp_out = (audio_stream_out_t*)
                mStream->common.get_parameters(&mStream->common, "hfpout");
        if (hfp_out != nullptr) {
            hfp_out->write(hfp_out, hfpBuffer.mData, hfpBuffer.mSize.mSize);
        }
        free(hfpBuffer.mData);
    }
}

bool BTSCOWriteThread::threadLoop() {
    // This implementation doesn't return control back to the Thread until it
    // decides to stop,
    // as the Thread uses mutexes, and this can lead to priority inversion.
    while (!std::atomic_load_explicit(mStop, std::memory_order_acquire)) {
        uint32_t efState = 0;
        mEfGroup->wait(static_cast<uint32_t>(MessageQueueFlagBits::NOT_EMPTY),
                       &efState, mNanoseconds);
        if (!(efState &
              static_cast<uint32_t>(MessageQueueFlagBits::NOT_EMPTY))) {
            continue;  // Nothing to do.
        }
        if (!mCommandMQ->read(&mStatus.replyTo)) {
            continue;  // Nothing to do.
        }
        switch (mStatus.replyTo) {
            case (IStreamOut::WriteCommand)StreamOut::MIC_BUFFER_READY:
                doWrite();
                break;
            default:
                ALOGE("Unknown write thread command code %d", mStatus.replyTo);
                mStatus.retval = Result::NOT_SUPPORTED;
                break;
        }
    }

    return false;
}

}  // namespace

// -------------------------------------------------------- class StreamOut
StreamOut::StreamOut(const sp<Device>& device, audio_stream_out_t* stream)
    : mIsClosed(false),
      mDevice(device),
      mStream(stream),
      mStreamCommon(new Stream(&stream->common)),
      mStreamMmap(new StreamMmap<audio_stream_out_t>(stream)),
      mEfGroup(nullptr),
      mStopWriteThread(false),
      mStopFmReadThread(false),
      mStopHfpReadThread(false),
      mStopMicReadThread(false),
      mStopHfpWriteThread(false),
      mFmEfGroup(nullptr),
      mHfpReadEfGroup(nullptr),
      mMicEfGroup(nullptr),
      mHfpWriteEfGroup(nullptr),
      mFmConnected(false),
      mHfpConnected(false),
      mFrameSize(0),
      mFramesCount(0),
      mHfpSampleRate(0),
      mOutVolume(-1.0),
      mDefaultOutVolume(-1.0) {}

StreamOut::~StreamOut() {
    ATRACE_CALL();
    close();
    if (mWriteThread.get()) {
        ATRACE_NAME("mWriteThread->join");
        status_t status = mWriteThread->join();
        ALOGE_IF(status, "write thread exit error: %s", strerror(-status));
    }
    if (mEfGroup) {
        status_t status = EventFlag::deleteEventFlag(&mEfGroup);
        ALOGE_IF(status, "write MQ event flag deletion error: %s",
                 strerror(-status));
    }
    if (mFmReadThread.get()) {
        ATRACE_NAME("mFmReadThread->join");
        status_t status = mFmReadThread->join();
        ALOGE_IF(status, "write thread exit error: %s", strerror(-status));
    }
    if (mFmEfGroup) {
        status_t status = EventFlag::deleteEventFlag(&mFmEfGroup);
        ALOGE_IF(status, "write MQ event flag deletion error: %s",
                 strerror(-status));
    }
    if (mHfpReadThread.get()) {
        ATRACE_NAME("mHfpReadThread->join");
        status_t status = mHfpReadThread->join();
        ALOGE_IF(status, "write thread exit error: %s", strerror(-status));
    }
    if (mHfpReadEfGroup) {
        status_t status = EventFlag::deleteEventFlag(&mHfpReadEfGroup);
        ALOGE_IF(status, "write MQ event flag deletion error: %s",
                 strerror(-status));
    }
    if (mMicReadThread.get()) {
        ATRACE_NAME("mMicReadThread->join");
        status_t status = mMicReadThread->join();
        ALOGE_IF(status, "write thread exit error: %s", strerror(-status));
    }
    if (mMicEfGroup) {
        status_t status = EventFlag::deleteEventFlag(&mMicEfGroup);
        ALOGE_IF(status, "write MQ event flag deletion error: %s",
                 strerror(-status));
    }
    if (mHfpWriteThread.get()) {
        ATRACE_NAME("mHfpWriteThread->join");
        status_t status = mHfpWriteThread->join();
        ALOGE_IF(status, "write thread exit error: %s", strerror(-status));
    }
    if (mHfpWriteEfGroup) {
        status_t status = EventFlag::deleteEventFlag(&mHfpWriteEfGroup);
        ALOGE_IF(status, "write MQ event flag deletion error: %s",
                 strerror(-status));
    }
    mCallback.clear();
    mDevice->closeOutputStream(mStream);
    mStream = nullptr;
}

int StreamOut::setVolumeParametersInternal(const hidl_vec<ParameterValue>& parameters) {
    AudioParameter params;

    for (size_t i = 0; i < parameters.size(); ++i) {
        params.add(String8(parameters[i].key.c_str()),
                   String8(parameters[i].value.c_str()));
    }

    String8 keyVolume = String8(AUDIO_PARAMETER_KEY__VOLUME);
    float volume = 0.0;
    if (params.getFloat(keyVolume, volume) == NO_ERROR) {
        ALOGV("%s, got volume %f", __FUNCTION__, volume);
        mOutVolume = volume;

        if (mFmConnected) {
            IStreamOut::WriteCommand cmd = (IStreamOut::WriteCommand)FM_VOLUME;

            if (!mFmCommandMQ->write(&cmd)) {
                ALOGE("command message queue write failed for %d", cmd);
                return 0;
            }
            mFmDataMQ->write((uint8_t*)&mOutVolume, sizeof(mOutVolume));
            if (mFmEfGroup) {
                mFmEfGroup->wake(static_cast<uint32_t>(MessageQueueFlagBits::NOT_EMPTY));
            }
        }

        return 0;
    }

    String8 keyDefaultVolume = String8(AUDIO_PARAMETER_KEY__DEFAULT_VOLUME);
    if (params.getFloat(keyDefaultVolume, volume) == NO_ERROR) {
        ALOGV("%s, got default volume %f", __FUNCTION__, volume);
        mDefaultOutVolume = volume;
    }

    return 0;
}

int StreamOut::setFmParametersInternal(const hidl_vec<ParameterValue>& parameters) {
    if (!(mFmCommandMQ && mFmEfGroup)) {
        return 0;
    }

    AudioParameter params;
    int device = 0;
    int found_fm = 0;
    int found_hfp = 0;

    for (size_t i = 0; i < parameters.size(); ++i) {
        params.add(String8(parameters[i].key.c_str()),
                   String8(parameters[i].value.c_str()));
    }

    String8 keyConnect = String8(AudioParameter::keyStreamConnect);
    if (params.getInt(keyConnect, device) == NO_ERROR) {
        ALOGE("%s, connected device %x", __FUNCTION__, device);
        if (AUDIO_DEVICE_IN_FM_TUNER == device) {
            mFmConnected = true;
        }
        found_fm = true;
    }
    String8 keyDisconnect = String8(AudioParameter::keyStreamDisconnect);
    if (params.getInt(keyDisconnect, device) == NO_ERROR) {
        ALOGE("%s, disconnected device %x", __FUNCTION__, device);
        if (AUDIO_DEVICE_IN_FM_TUNER == device) {
            mFmConnected = false;
        }
        found_fm = true;
    }

    String8 keyHfp("hfp_enable");
    String8 hfpState;
    if (params.get(keyHfp, hfpState) == NO_ERROR) {
        if (hfpState == "true") {
            found_hfp = true;
            mHfpConnected = true;
        } else if (hfpState == "false") {
            found_hfp = true;
            mHfpConnected = false;
        }
    }

    String8 keyHfpSampleRate("hfp_set_sampling_rate");
    int hfpSampleRate;
    if (params.getInt(keyHfpSampleRate, hfpSampleRate) == NO_ERROR) {
        mHfpSampleRate = hfpSampleRate;
    }

    if (found_fm) {
        IStreamOut::WriteCommand cmd = (IStreamOut::WriteCommand)
                (mFmConnected ? FM_CONNECTED : FM_DISCONNECTED);
        if (!mFmCommandMQ->write(&cmd)) {
            ALOGE("command message queue write failed for %d", cmd);
            return 0;
        }
        if (mOutVolume != -1.0) {
            mFmDataMQ->write((uint8_t*)&mOutVolume, sizeof(mOutVolume));
        } else {
            mFmDataMQ->write((uint8_t*)&mDefaultOutVolume, sizeof(mDefaultOutVolume));
        }
        if (mFmEfGroup) {
            mFmEfGroup->wake(static_cast<uint32_t>(MessageQueueFlagBits::NOT_EMPTY));
        }
    }

    if (found_hfp) {
        IStreamOut::WriteCommand cmd = (IStreamOut::WriteCommand)
                (mHfpConnected ? HFP_CONNECTED : HFP_DISCONNECTED);

        if (!mHfpReadCommandMQ->write(&cmd)) {
            ALOGE("command message queue write failed for %d", cmd);
            return 0;
        }

        if (mHfpConnected) {
            SHfpBufferSize size = {
                CAPTURE_PERIOD_SIZE * CAPTURE_PERIOD_COUNT * (DEFAULT_IN_SAMPLING_RATE / mHfpSampleRate),
            };
            mHfpReadDataMQ->write((uint8_t*)&size, sizeof(SHfpBufferSize));
        }
        if (mHfpReadEfGroup) {
            mHfpReadEfGroup->wake(static_cast<uint32_t>(MessageQueueFlagBits::NOT_EMPTY));
        }

        if (!mMicCommandMQ->write(&cmd)) {
            ALOGE("command message queue write failed for %d", cmd);
            return 0;
        }
        if (mHfpConnected) {
            SHfpBufferSize size = {
                CAPTURE_PERIOD_SIZE * CAPTURE_PERIOD_COUNT * (DEFAULT_IN_SAMPLING_RATE / mHfpSampleRate),
            };
            mMicDataMQ->write((uint8_t*)&size, sizeof(SHfpBufferSize));
        }
        if (mMicEfGroup) {
            mMicEfGroup->wake(static_cast<uint32_t>(MessageQueueFlagBits::NOT_EMPTY));
        }
    }

    return (found_fm | found_hfp);
}

// Methods from ::android::hardware::audio::AUDIO_HAL_VERSION::IStream follow.
Return<uint64_t> StreamOut::getFrameSize() {
    return audio_stream_out_frame_size(mStream);
}

Return<uint64_t> StreamOut::getFrameCount() {
    return mStreamCommon->getFrameCount();
}

Return<uint64_t> StreamOut::getBufferSize() {
    return mStreamCommon->getBufferSize();
}

Return<uint32_t> StreamOut::getSampleRate() {
    return mStreamCommon->getSampleRate();
}

Return<Result> StreamOut::setSampleRate(uint32_t sampleRateHz) {
    return mStreamCommon->setSampleRate(sampleRateHz);
}

Return<AudioChannelBitfield> StreamOut::getChannelMask() {
    return mStreamCommon->getChannelMask();
}

#ifdef AUDIO_HAL_VERSION_2_0
Return<void> StreamOut::getSupportedChannelMasks(getSupportedChannelMasks_cb _hidl_cb) {
    return mStreamCommon->getSupportedChannelMasks(_hidl_cb);
}
Return<void> StreamOut::getSupportedSampleRates(getSupportedSampleRates_cb _hidl_cb) {
    return mStreamCommon->getSupportedSampleRates(_hidl_cb);
}
#endif

Return<void> StreamOut::getSupportedChannelMasks(AudioFormat format,
                                                 getSupportedChannelMasks_cb _hidl_cb) {
    return mStreamCommon->getSupportedChannelMasks(format, _hidl_cb);
}
Return<void> StreamOut::getSupportedSampleRates(AudioFormat format,
                                                getSupportedSampleRates_cb _hidl_cb) {
    return mStreamCommon->getSupportedSampleRates(format, _hidl_cb);
}

Return<Result> StreamOut::setChannelMask(AudioChannelBitfield mask) {
    return mStreamCommon->setChannelMask(mask);
}

Return<AudioFormat> StreamOut::getFormat() {
    return mStreamCommon->getFormat();
}

Return<void> StreamOut::getSupportedFormats(getSupportedFormats_cb _hidl_cb) {
    return mStreamCommon->getSupportedFormats(_hidl_cb);
}

Return<Result> StreamOut::setFormat(AudioFormat format) {
    return mStreamCommon->setFormat(format);
}

Return<void> StreamOut::getAudioProperties(getAudioProperties_cb _hidl_cb) {
    return mStreamCommon->getAudioProperties(_hidl_cb);
}

Return<Result> StreamOut::addEffect(uint64_t effectId) {
    return mStreamCommon->addEffect(effectId);
}

Return<Result> StreamOut::removeEffect(uint64_t effectId) {
    return mStreamCommon->removeEffect(effectId);
}

Return<Result> StreamOut::standby() {
    return mStreamCommon->standby();
}

Return<Result> StreamOut::setHwAvSync(uint32_t hwAvSync) {
    return mStreamCommon->setHwAvSync(hwAvSync);
}

#ifdef AUDIO_HAL_VERSION_2_0
Return<Result> StreamOut::setConnectedState(const DeviceAddress& address,
                                            bool connected) {
    return mStreamCommon->setConnectedState(address, connected);
}

Return<void> StreamOut::debugDump(const hidl_handle& fd) {
    return mStreamCommon->debugDump(fd);
}

Return<AudioDevice> StreamOut::getDevice() {
    return mStreamCommon->getDevice();
}

Return<Result> StreamOut::setDevice(const DeviceAddress& address) {
    return mStreamCommon->setDevice(address);
}

Return<void> StreamOut::getParameters(const hidl_vec<hidl_string>& keys,
                                      getParameters_cb _hidl_cb) {
    return mStreamCommon->getParameters(keys, _hidl_cb);
}

Return<Result> StreamOut::setParameters(
    const hidl_vec<ParameterValue>& parameters) {

    setVolumeParametersInternal(parameters);
    setFmParametersInternal(parameters);

    return mStreamCommon->setParameters(parameters);
}

#elif defined(AUDIO_HAL_VERSION_4_0)
Return<void> StreamOut::getDevices(getDevices_cb _hidl_cb) {
    return mStreamCommon->getDevices(_hidl_cb);
}

Return<Result> StreamOut::setDevices(const hidl_vec<DeviceAddress>& devices) {
    return mStreamCommon->setDevices(devices);
}

Return<void> StreamOut::getParameters(const hidl_vec<ParameterValue>& context,
                                      const hidl_vec<hidl_string>& keys,
                                      getParameters_cb _hidl_cb) {
    return mStreamCommon->getParameters(context, keys, _hidl_cb);
}

Return<Result> StreamOut::setParameters(const hidl_vec<ParameterValue>& context,
                                        const hidl_vec<ParameterValue>& parameters) {

    setVolumeParametersInternal(parameters);
    setFmParametersInternal(parameters);

    return mStreamCommon->setParameters(context, parameters);
}

#endif

Return<Result> StreamOut::close() {
    if (mIsClosed) return Result::INVALID_STATE;
    mIsClosed = true;
    if (mWriteThread.get()) {
        mStopWriteThread.store(true, std::memory_order_release);
    }
    if (mEfGroup) {
        mEfGroup->wake(static_cast<uint32_t>(MessageQueueFlagBits::NOT_EMPTY));
    }
    if (mFmReadThread.get()) {
        mStopFmReadThread.store(true, std::memory_order_release);
    }
    if (mFmEfGroup) {
        mFmEfGroup->wake(static_cast<uint32_t>(MessageQueueFlagBits::NOT_EMPTY));
    }
    if (mHfpReadThread.get()) {
        mStopHfpReadThread.store(true, std::memory_order_release);
    }
    if (mHfpReadEfGroup) {
        mHfpReadEfGroup->wake(static_cast<uint32_t>(MessageQueueFlagBits::NOT_EMPTY));
    }
    if (mMicReadThread.get()) {
        mStopMicReadThread.store(true, std::memory_order_release);
    }
    if (mMicEfGroup) {
        mMicEfGroup->wake(static_cast<uint32_t>(MessageQueueFlagBits::NOT_EMPTY));
    }
    if (mHfpWriteThread.get()) {
        mStopHfpWriteThread.store(true, std::memory_order_release);
    }
    if (mHfpWriteEfGroup) {
        mHfpWriteEfGroup->wake(static_cast<uint32_t>(MessageQueueFlagBits::NOT_EMPTY));
    }
    return Result::OK;
}

// Methods from ::android::hardware::audio::AUDIO_HAL_VERSION::IStreamOut follow.
Return<uint32_t> StreamOut::getLatency() {
    return mStream->get_latency(mStream);
}

Return<Result> StreamOut::setVolume(float left, float right) {
    if (mStream->set_volume == NULL) {
        return Result::NOT_SUPPORTED;
    }
    if (!isGainNormalized(left)) {
        ALOGW("Can not set a stream output volume {%f, %f} outside [0,1]", left,
              right);
        return Result::INVALID_ARGUMENTS;
    }
    return Stream::analyzeStatus("set_volume",
                                 mStream->set_volume(mStream, left, right));
}

Return<void> StreamOut::prepareForWriting(uint32_t frameSize,
                                          uint32_t framesCount,
                                          prepareForWriting_cb _hidl_cb) {
    status_t status;
    ThreadInfo threadInfo = {0, 0};

    // Wrap the _hidl_cb to return an error
    auto sendError = [this, &threadInfo, &_hidl_cb](Result result) {
        _hidl_cb(result, CommandMQ::Descriptor(), DataMQ::Descriptor(),
                 StatusMQ::Descriptor(), threadInfo);

    };

    // Create message queues.
    if (mDataMQ) {
        ALOGE("the client attempts to call prepareForWriting twice");
        sendError(Result::INVALID_STATE);
        return Void();
    }
    std::unique_ptr<CommandMQ> tempCommandMQ(new CommandMQ(1));

    // Check frameSize and framesCount
    if (frameSize == 0 || framesCount == 0) {
        ALOGE("Null frameSize (%u) or framesCount (%u)", frameSize,
              framesCount);
        sendError(Result::INVALID_ARGUMENTS);
        return Void();
    }
    if (frameSize > Stream::MAX_BUFFER_SIZE / framesCount) {
        ALOGE("Buffer too big: %u*%u bytes > MAX_BUFFER_SIZE (%u)", frameSize, framesCount,
              Stream::MAX_BUFFER_SIZE);
        sendError(Result::INVALID_ARGUMENTS);
        return Void();
    }
    mFrameSize = frameSize;
    mFramesCount = framesCount;
    std::unique_ptr<DataMQ> tempDataMQ(
        new DataMQ(frameSize * framesCount, true /* EventFlag */));
    std::unique_ptr<DataMQ> tempDataHfpMQ(
        new DataMQ(sizeof(SHfpBuffer) * 4, false /* EventFlag */));

    std::unique_ptr<StatusMQ> tempStatusMQ(new StatusMQ(1));
    if (!tempCommandMQ->isValid() || !tempDataMQ->isValid() ||
        !tempStatusMQ->isValid()) {
        ALOGE_IF(!tempCommandMQ->isValid(), "command MQ is invalid");
        ALOGE_IF(!tempDataMQ->isValid(), "data MQ is invalid");
        ALOGE_IF(!tempStatusMQ->isValid(), "status MQ is invalid");
        sendError(Result::INVALID_ARGUMENTS);
        return Void();
    }
    EventFlag* tempRawEfGroup{};
    status = EventFlag::createEventFlag(tempDataMQ->getEventFlagWord(),
                                        &tempRawEfGroup);
    std::unique_ptr<EventFlag, void (*)(EventFlag*)> tempElfGroup(
        tempRawEfGroup, [](auto* ef) { EventFlag::deleteEventFlag(&ef); });
    if (status != OK || !tempElfGroup) {
        ALOGE("failed creating event flag for data MQ: %s", strerror(-status));
        sendError(Result::INVALID_ARGUMENTS);
        return Void();
    }

    // Create and launch the thread.
    auto tempWriteThread = std::make_unique<WriteThread>(
        &mStopWriteThread, mStream, tempCommandMQ.get(), tempDataMQ.get(),
		tempDataHfpMQ.get(), tempStatusMQ.get(), tempElfGroup.get());
    if (!tempWriteThread->init()) {
        ALOGW("failed to start writer thread: %s", strerror(-status));
        sendError(Result::INVALID_ARGUMENTS);
        return Void();
    }
    status = tempWriteThread->run("writer", PRIORITY_URGENT_AUDIO);
    if (status != OK) {
        ALOGW("failed to start writer thread: %s", strerror(-status));
        sendError(Result::INVALID_ARGUMENTS);
        return Void();
    }

    mCommandMQ = std::move(tempCommandMQ);
    mDataMQ = std::move(tempDataMQ);
    mDataHfpMQ = std::move(tempDataHfpMQ);
    mStatusMQ = std::move(tempStatusMQ);
    mWriteThread = tempWriteThread.release();
    mEfGroup = tempElfGroup.release();

    { // creating a thread to read from FM
        std::unique_ptr<CommandMQ> tempFmCommandMQ(new CommandMQ(1));
        std::unique_ptr<DataMQ> tempFmDataMQ(
            new DataMQ(frameSize * framesCount, true /* EventFlag */));
        std::unique_ptr<StatusMQ> tempFmStatusMQ(new StatusMQ(1));

        if (!tempFmCommandMQ->isValid() || !tempFmDataMQ->isValid() ||
            !tempFmStatusMQ->isValid()) {
            ALOGE_IF(!tempFmCommandMQ->isValid(), "FM command MQ is invalid");
            ALOGE_IF(!tempFmDataMQ->isValid(), "FM data MQ is invalid");
            ALOGE_IF(!tempFmStatusMQ->isValid(), "FM status MQ is invalid");
            sendError(Result::INVALID_ARGUMENTS);
            return Void();
        }
        EventFlag* tempFmRawEfGroup{};
        status = EventFlag::createEventFlag(tempFmDataMQ->getEventFlagWord(),
                                            &tempFmRawEfGroup);
        std::unique_ptr<EventFlag, void (*)(EventFlag*)> tempFmElfGroup(
            tempFmRawEfGroup, [](auto* ef) { EventFlag::deleteEventFlag(&ef); });
        if (status != OK || !tempFmElfGroup) {
            ALOGE("failed creating event flag for data MQ: %s", strerror(-status));
            sendError(Result::INVALID_ARGUMENTS);
            return Void();
        }

        // Create and launch the FM thread.
        auto tempFmReadThread = std::make_unique<ReadFmThread>(
            &mStopFmReadThread, mStream, tempFmCommandMQ.get(), tempFmDataMQ.get(),
            tempFmStatusMQ.get(), tempFmElfGroup.get());
        if (!tempFmReadThread->init()) {
            ALOGW("failed to start writer thread: %s", strerror(-status));
            sendError(Result::INVALID_ARGUMENTS);
            return Void();
        }
        status = tempFmReadThread->run("fmreader", PRIORITY_URGENT_AUDIO);
        if (status != OK) {
            ALOGW("failed to start writer thread: %s", strerror(-status));
            sendError(Result::INVALID_ARGUMENTS);
            return Void();
        }

        mFmCommandMQ = std::move(tempFmCommandMQ);
        mFmDataMQ = std::move(tempFmDataMQ);
        mFmStatusMQ = std::move(tempFmStatusMQ);
        mFmReadThread = tempFmReadThread.release();
        mFmEfGroup = tempFmElfGroup.release();

        mFmReadThread->setOtherThread(mEfGroup, mCommandMQ.get(), mDataMQ.get());
    }

    { // creating a thread to read from BT SCO
        std::unique_ptr<CommandMQ> tempHfpReadCommandMQ(new CommandMQ(1));
        std::unique_ptr<DataMQ> tempHfpReadDataMQ(
            new DataMQ(sizeof(SHfpBuffer) * 4, true /* EventFlag */));
        std::unique_ptr<StatusMQ> tempHfpReadStatusMQ(new StatusMQ(1));

        if (!tempHfpReadCommandMQ->isValid() || !tempHfpReadDataMQ->isValid() ||
            !tempHfpReadStatusMQ->isValid()) {
            ALOGE_IF(!tempHfpReadCommandMQ->isValid(), "BT SCO Read command MQ is invalid");
            ALOGE_IF(!tempHfpReadDataMQ->isValid(), "BT SCO Read data MQ is invalid");
            ALOGE_IF(!tempHfpReadStatusMQ->isValid(), "BT SCO Read status MQ is invalid");
            sendError(Result::INVALID_ARGUMENTS);
            return Void();
        }
        EventFlag* tempHfpReadRawEfGroup{};
        status = EventFlag::createEventFlag(tempHfpReadDataMQ->getEventFlagWord(),
                                            &tempHfpReadRawEfGroup);
        std::unique_ptr<EventFlag, void (*)(EventFlag*)> tempHfpReadElfGroup(
            tempHfpReadRawEfGroup, [](auto* ef) { EventFlag::deleteEventFlag(&ef); });
        if (status != OK || !tempHfpReadElfGroup) {
            ALOGE("failed creating event flag for data MQ: %s", strerror(-status));
            sendError(Result::INVALID_ARGUMENTS);
            return Void();
        }

        // Create and launch the BT SCO thread.
        auto tempHfpReadThread = std::make_unique<BTSCOReadThread>(
            &mStopHfpReadThread, mStream, tempHfpReadCommandMQ.get(), tempHfpReadDataMQ.get(),
            tempHfpReadStatusMQ.get(), tempHfpReadElfGroup.get(), "hfpread",
            (IStreamOut::WriteCommand)StreamOut::HFP_BUFFER_READY);
        if (!tempHfpReadThread->init()) {
            ALOGW("failed to start writer thread: %s", strerror(-status));
            sendError(Result::INVALID_ARGUMENTS);
            return Void();
        }
        status = tempHfpReadThread->run("hfpreader", PRIORITY_URGENT_AUDIO);
        if (status != OK) {
            ALOGW("failed to start writer thread: %s", strerror(-status));
            sendError(Result::INVALID_ARGUMENTS);
            return Void();
        }

        mHfpReadCommandMQ = std::move(tempHfpReadCommandMQ);
        mHfpReadDataMQ = std::move(tempHfpReadDataMQ);
        mHfpReadStatusMQ = std::move(tempHfpReadStatusMQ);
        mHfpReadThread = tempHfpReadThread.release();
        mHfpReadEfGroup = tempHfpReadElfGroup.release();

        mHfpReadThread->setOtherThread(mEfGroup, mCommandMQ.get(), mDataHfpMQ.get());
    }

    { // creating a thread to write to BT SCO
        std::unique_ptr<CommandMQ> tempHfpWriteCommandMQ(new CommandMQ(1));
        std::unique_ptr<DataMQ> tempHfpWriteDataMQ(
            new DataMQ(16384, true /* EventFlag */));
        std::unique_ptr<StatusMQ> tempHfpWriteStatusMQ(new StatusMQ(1));

        if (!tempHfpWriteCommandMQ->isValid() || !tempHfpWriteDataMQ->isValid() ||
            !tempHfpWriteStatusMQ->isValid()) {
            ALOGE_IF(!tempHfpWriteCommandMQ->isValid(), "BT SCO Write command MQ is invalid");
            ALOGE_IF(!tempHfpWriteDataMQ->isValid(), "BT SCO Write data MQ is invalid");
            ALOGE_IF(!tempHfpWriteStatusMQ->isValid(), "BT SCO Write status MQ is invalid");
            sendError(Result::INVALID_ARGUMENTS);
            return Void();
        }
        EventFlag* tempHfpWriteRawEfGroup{};
        status = EventFlag::createEventFlag(tempHfpWriteDataMQ->getEventFlagWord(),
                                            &tempHfpWriteRawEfGroup);
        std::unique_ptr<EventFlag, void (*)(EventFlag*)> tempHfpWriteElfGroup(
            tempHfpWriteRawEfGroup, [](auto* ef) { EventFlag::deleteEventFlag(&ef); });
        if (status != OK || !tempHfpWriteElfGroup) {
            ALOGE("failed creating event flag for data MQ: %s", strerror(-status));
            sendError(Result::INVALID_ARGUMENTS);
            return Void();
        }

        // Create and launch the BT SCO Write thread.
        auto tempHfpWriteThread = std::make_unique<BTSCOWriteThread>(
            &mStopHfpWriteThread, mStream, tempHfpWriteCommandMQ.get(), tempHfpWriteDataMQ.get(),
            tempHfpWriteStatusMQ.get(), tempHfpWriteElfGroup.get());
        if (!tempHfpWriteThread->init()) {
            ALOGW("failed to start writer thread: %s", strerror(-status));
            sendError(Result::INVALID_ARGUMENTS);
            return Void();
        }
        status = tempHfpWriteThread->run("hfpwriter", PRIORITY_URGENT_AUDIO);
        if (status != OK) {
            ALOGW("failed to start writer thread: %s", strerror(-status));
            sendError(Result::INVALID_ARGUMENTS);
            return Void();
        }

        mHfpWriteCommandMQ = std::move(tempHfpWriteCommandMQ);
        mHfpWriteDataMQ = std::move(tempHfpWriteDataMQ);
        mHfpWriteStatusMQ = std::move(tempHfpWriteStatusMQ);
        mHfpWriteThread = tempHfpWriteThread.release();
        mHfpWriteEfGroup = tempHfpWriteElfGroup.release();
    }

    { // creating a thread to read from Mic
        std::unique_ptr<CommandMQ> tempMicCommandMQ(new CommandMQ(1));
        std::unique_ptr<DataMQ> tempMicDataMQ(
            new DataMQ(sizeof(SHfpBuffer) * 4, true /* EventFlag */));
        std::unique_ptr<StatusMQ> tempMicStatusMQ(new StatusMQ(1));

        if (!tempMicCommandMQ->isValid() || !tempMicDataMQ->isValid() ||
            !tempMicStatusMQ->isValid()) {
            ALOGE_IF(!tempMicCommandMQ->isValid(), "Mic command MQ is invalid");
            ALOGE_IF(!tempMicDataMQ->isValid(), "Mic data MQ is invalid");
            ALOGE_IF(!tempMicStatusMQ->isValid(), "Mic status MQ is invalid");
            sendError(Result::INVALID_ARGUMENTS);
            return Void();
        }
        EventFlag* tempMicRawEfGroup{};
        status = EventFlag::createEventFlag(tempMicDataMQ->getEventFlagWord(),
                                            &tempMicRawEfGroup);
        std::unique_ptr<EventFlag, void (*)(EventFlag*)> tempMicElfGroup(
            tempMicRawEfGroup, [](auto* ef) { EventFlag::deleteEventFlag(&ef); });
        if (status != OK || !tempMicElfGroup) {
            ALOGE("failed creating event flag for data MQ: %s", strerror(-status));
            sendError(Result::INVALID_ARGUMENTS);
            return Void();
        }

        // Create and launch the BT SCO thread.
        auto tempMicReadThread = std::make_unique<MicReadThread>(
            &mStopMicReadThread, mStream, tempMicCommandMQ.get(), tempMicDataMQ.get(),
            tempMicStatusMQ.get(), tempMicElfGroup.get(), "micread",
		    (IStreamOut::WriteCommand)StreamOut::MIC_BUFFER_READY);
        if (!tempMicReadThread->init()) {
            ALOGW("failed to start writer thread: %s", strerror(-status));
            sendError(Result::INVALID_ARGUMENTS);
            return Void();
        }
        status = tempMicReadThread->run("micreader", PRIORITY_URGENT_AUDIO);
        if (status != OK) {
            ALOGW("failed to start writer thread: %s", strerror(-status));
            sendError(Result::INVALID_ARGUMENTS);
            return Void();
        }

        mMicCommandMQ = std::move(tempMicCommandMQ);
        mMicDataMQ = std::move(tempMicDataMQ);
        mMicStatusMQ = std::move(tempMicStatusMQ);
        mMicReadThread = tempMicReadThread.release();
        mMicEfGroup = tempMicElfGroup.release();

        mMicReadThread->setOtherThread(mHfpWriteEfGroup, mHfpWriteCommandMQ.get(), mHfpWriteDataMQ.get());
    }

    threadInfo.pid = getpid();
    threadInfo.tid = mWriteThread->getTid();
    _hidl_cb(Result::OK, *mCommandMQ->getDesc(), *mDataMQ->getDesc(),
             *mStatusMQ->getDesc(), threadInfo);
    return Void();
}

Return<void> StreamOut::getRenderPosition(getRenderPosition_cb _hidl_cb) {
    uint32_t halDspFrames;
    Result retval = Stream::analyzeStatus(
        "get_render_position",
        mStream->get_render_position(mStream, &halDspFrames));
    _hidl_cb(retval, halDspFrames);
    return Void();
}

Return<void> StreamOut::getNextWriteTimestamp(
    getNextWriteTimestamp_cb _hidl_cb) {
    Result retval(Result::NOT_SUPPORTED);
    int64_t timestampUs = 0;
    if (mStream->get_next_write_timestamp != NULL) {
        retval = Stream::analyzeStatus(
            "get_next_write_timestamp",
            mStream->get_next_write_timestamp(mStream, &timestampUs));
    }
    _hidl_cb(retval, timestampUs);
    return Void();
}

Return<Result> StreamOut::setCallback(const sp<IStreamOutCallback>& callback) {
    if (mStream->set_callback == NULL) return Result::NOT_SUPPORTED;
    int result = mStream->set_callback(mStream, StreamOut::asyncCallback, this);
    if (result == 0) {
        mCallback = callback;
    }
    return Stream::analyzeStatus("set_callback", result);
}

Return<Result> StreamOut::clearCallback() {
    if (mStream->set_callback == NULL) return Result::NOT_SUPPORTED;
    mCallback.clear();
    return Result::OK;
}

// static
int StreamOut::asyncCallback(stream_callback_event_t event, void*,
                             void* cookie) {
    wp<StreamOut> weakSelf(reinterpret_cast<StreamOut*>(cookie));
    sp<StreamOut> self = weakSelf.promote();
    if (self == nullptr || self->mCallback == nullptr) return 0;
    ALOGV("asyncCallback() event %d", event);
    switch (event) {
        case STREAM_CBK_EVENT_WRITE_READY:
            self->mCallback->onWriteReady();
            break;
        case STREAM_CBK_EVENT_DRAIN_READY:
            self->mCallback->onDrainReady();
            break;
        case STREAM_CBK_EVENT_ERROR:
            self->mCallback->onError();
            break;
        default:
            ALOGW("asyncCallback() unknown event %d", event);
            break;
    }
    return 0;
}

Return<void> StreamOut::supportsPauseAndResume(
    supportsPauseAndResume_cb _hidl_cb) {
    _hidl_cb(mStream->pause != NULL, mStream->resume != NULL);
    return Void();
}

Return<Result> StreamOut::pause() {
    return mStream->pause != NULL
               ? Stream::analyzeStatus("pause", mStream->pause(mStream))
               : Result::NOT_SUPPORTED;
}

Return<Result> StreamOut::resume() {
    return mStream->resume != NULL
               ? Stream::analyzeStatus("resume", mStream->resume(mStream))
               : Result::NOT_SUPPORTED;
}

Return<bool> StreamOut::supportsDrain() {
    return mStream->drain != NULL;
}

Return<Result> StreamOut::drain(AudioDrain type) {
    return mStream->drain != NULL
               ? Stream::analyzeStatus(
                     "drain",
                     mStream->drain(mStream,
                                    static_cast<audio_drain_type_t>(type)))
               : Result::NOT_SUPPORTED;
}

Return<Result> StreamOut::flush() {
    return mStream->flush != NULL
               ? Stream::analyzeStatus("flush", mStream->flush(mStream))
               : Result::NOT_SUPPORTED;
}

// static
Result StreamOut::getPresentationPositionImpl(audio_stream_out_t* stream,
                                              uint64_t* frames,
                                              TimeSpec* timeStamp) {
    // Don't logspam on EINVAL--it's normal for get_presentation_position
    // to return it sometimes. EAGAIN may be returned by A2DP audio HAL
    // implementation. ENODATA can also be reported while the writer is
    // continuously querying it, but the stream has been stopped.
    static const std::vector<int> ignoredErrors{EINVAL, EAGAIN, ENODATA};
    Result retval(Result::NOT_SUPPORTED);
    if (stream->get_presentation_position == NULL) return retval;
    struct timespec halTimeStamp;
    retval = Stream::analyzeStatus("get_presentation_position",
                                   stream->get_presentation_position(stream, frames, &halTimeStamp),
                                   ignoredErrors);
    if (retval == Result::OK) {
        timeStamp->tvSec = halTimeStamp.tv_sec;
        timeStamp->tvNSec = halTimeStamp.tv_nsec;
    }
    return retval;
}

Return<void> StreamOut::getPresentationPosition(
    getPresentationPosition_cb _hidl_cb) {
    uint64_t frames = 0;
    TimeSpec timeStamp = {0, 0};
    Result retval = getPresentationPositionImpl(mStream, &frames, &timeStamp);
    _hidl_cb(retval, frames, timeStamp);
    return Void();
}

Return<Result> StreamOut::start() {
    return mStreamCommon->start();
}

Return<Result> StreamOut::stop() {
    return mStreamCommon->stop();
}

Return<void> StreamOut::createMmapBuffer(int32_t minSizeFrames,
                                         createMmapBuffer_cb _hidl_cb) {
    return mStreamCommon->createMmapBuffer(
        minSizeFrames, _hidl_cb);
}

Return<void> StreamOut::getMmapPosition(getMmapPosition_cb _hidl_cb) {
    return mStreamCommon->getMmapPosition(_hidl_cb);
}

Return<void> StreamOut::debug(const hidl_handle& fd, const hidl_vec<hidl_string>& options) {
    return mStreamCommon->debug(fd, options);
}

#ifdef AUDIO_HAL_VERSION_4_0
Return<void> StreamOut::updateSourceMetadata(const SourceMetadata& sourceMetadata) {
    if (mStream->update_source_metadata == nullptr) {
        return Void();  // not supported by the HAL
    }
    std::vector<playback_track_metadata> halTracks;
    halTracks.reserve(sourceMetadata.tracks.size());
    for (auto& metadata : sourceMetadata.tracks) {
        halTracks.push_back({
            .usage = static_cast<audio_usage_t>(metadata.usage),
            .content_type = static_cast<audio_content_type_t>(metadata.contentType),
            .gain = metadata.gain,
        });
    }
    const source_metadata_t halMetadata = {
        .track_count = halTracks.size(), .tracks = halTracks.data(),
    };
    mStream->update_source_metadata(mStream, &halMetadata);
    return Void();
}
Return<Result> StreamOut::selectPresentation(int32_t /*presentationId*/, int32_t /*programId*/) {
    return Result::NOT_SUPPORTED;  // TODO: propagate to legacy
}
#endif

}  // namespace kingfisher
}  // namespace AUDIO_HAL_VERSION
}  // namespace audio
}  // namespace hardware
}  // namespace android
