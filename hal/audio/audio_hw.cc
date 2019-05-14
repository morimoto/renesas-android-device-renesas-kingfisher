/*
 * Copyright (C) 2011 The Android Open Source Project
 * Copyright (C) 2013 Renesas Electronics Corporation
 * Copyright (C) 2017 GlobalLogic
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

#define LOG_TAG "audio_hw_primary_kingfisher"
//#define LOG_NDEBUG 0

#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/time.h>
#include <stdlib.h>
#include <unistd.h>

#include <log/log.h>
#include <cutils/str_parms.h>
#include <cutils/properties.h>

#include <hardware/hardware.h>
#include <system/audio.h>
#include <hardware/audio.h>

#include <tinyalsa/asoundlib.h>
#include <audio_utils/resampler.h>
#include <audio_utils/echo_reference.h>
#include <hardware/audio_effect.h>
#include <audio_effects/effect_aec.h>

#include "audio_hw_defs.h"

/* Mixer control names */
#define MIXER_PLAY_VOL_CTRL_TYPE    "DAC Volume Control Type"
#define MIXER_PLAY_VOL_MASTER       "Master Playback Volume"
#define MIXER_PLAY_VOL_DAC1         "DAC1 Playback Volume"
#define MIXER_PLAY_VOL_DAC2         "DAC2 Playback Volume"
#define MIXER_PLAY_VOL_DAC3         "DAC3 Playback Volume"
#define MIXER_PLAY_VOL_DAC4         "DAC4 Playback Volume"

#define MIXER_CAP_VOL_CTRL_TYPE     "ADC Volume Control Type"
#define MIXER_CAP_MASTER_VOL        "Master Capture Volume"
#define MIXER_CAP_VOL_ADC1          "ADC1 Capture Volume"
#define MIXER_CAP_VOL_ADC2          "ADC2 Capture Volume"
#define MIXER_CAP_VOL_ADC3          "ADC3 Capture Volume"

#define MIXER_DVC_IN_CAPTURE_VOL    "DVC In Capture Volume"

/* GEN3 mixer values */
#define MIXER_PLAY_V_MASTER_DEF     180
#define MIXER_PLAY_V_DAC_DEF        192

#define MIXER_CAP_V_MASTER_DEF      230
#define MIXER_CAP_V_ADC_DEF         230

#define MIXER_DVC_IN_CAP_VOL_DEF    2200000

/* ALSA cards for GEN3 */
#define CARD_GEN3_PCM3168A          0
#define CARD_GEN3_AK4613            1
#define CARD_GEN3_FM                2
#define CARD_GEN3_BTSCO             3

/* ALSA ports(devices) for GEN3 */
#define PORT_DAC                    0
#define PORT_MIC                    0
#define PORT_FM                     0
#define PORT_BTSCO                  0

/* */
struct pcm_config pcm_config_dac = {
    .channels = AUDIO_CHANNEL_OUT_COUNT,
    .rate = DEFAULT_OUT_SAMPLING_RATE,
    .period_size = PLAY_PERIOD_SIZE,
    .period_count = PLAY_PERIOD_COUNT,
    .format = DEFAULT_OUT_PCM_FORMAT
};

struct pcm_config pcm_config_hfp_out = {
    .channels = 2,
    .rate = DEFAULT_HFP_SAMPLING_RATE,
    .period_size = PLAY_PERIOD_SIZE,
    .period_count = PLAY_PERIOD_COUNT,
    .format = DEFAULT_OUT_PCM_FORMAT
};

struct pcm_config pcm_config_mic = {
    .channels = AUDIO_CHANNEL_IN_COUNT,
    .rate = DEFAULT_IN_SAMPLING_RATE,
    .period_size = CAPTURE_PERIOD_SIZE,
    .period_count = CAPTURE_PERIOD_COUNT,
    .format = DEFAULT_IN_PCM_FORMAT
};

struct pcm_config pcm_config_fm = {
    .channels = 2,
    .rate = DEFAULT_IN_SAMPLING_RATE,
    .period_size = CAPTURE_PERIOD_SIZE,
    .period_count = CAPTURE_PERIOD_COUNT,
    .format = DEFAULT_IN_PCM_FORMAT
};

struct pcm_config pcm_config_hfp_in = {
    .channels = 2,
    .rate = DEFAULT_HFP_SAMPLING_RATE,
    .period_size = CAPTURE_PERIOD_SIZE,
    .period_count = CAPTURE_PERIOD_COUNT,
    .format = DEFAULT_IN_PCM_FORMAT
};

#define MIN(x, y) ((x) > (y) ? (y) : (x))

#define PTHREAD_MUTEX_LOCK(lock) \
    pthread_mutex_lock(lock);

#define PTHREAD_MUTEX_UNLOCK(lock) \
    pthread_mutex_unlock(lock);

struct route_setting
{
    const char *    ctl_name;
    int             intval;
    const char *    strval;
};

/* These are values that never change */
struct route_setting defaults3168[] = {
    /* playback */
    {
        .ctl_name = MIXER_PLAY_VOL_CTRL_TYPE,
        .intval = 1, /* Master + Individual */
    },
    {
        .ctl_name = MIXER_PLAY_VOL_MASTER,
        .intval = MIXER_PLAY_V_MASTER_DEF,
    },
    {
        .ctl_name = MIXER_PLAY_VOL_DAC1,
        .intval = MIXER_PLAY_V_DAC_DEF,
    },
    {
        .ctl_name = MIXER_PLAY_VOL_DAC2,
        .intval = MIXER_PLAY_V_DAC_DEF,
    },
    {
        .ctl_name = MIXER_PLAY_VOL_DAC3,
        .intval = MIXER_PLAY_V_DAC_DEF,
    },
    {
        .ctl_name = MIXER_PLAY_VOL_DAC4,
        .intval = MIXER_PLAY_V_DAC_DEF,
    },

    /* capture */
    {
        .ctl_name = MIXER_CAP_VOL_CTRL_TYPE,
        .intval = 1, /* Master + Individual */
    },
    {
        .ctl_name = MIXER_CAP_MASTER_VOL,
        .intval = MIXER_CAP_V_MASTER_DEF,
    },
    {
        .ctl_name = MIXER_CAP_VOL_ADC1,
        .intval = MIXER_CAP_V_ADC_DEF,
    },
    {
        .ctl_name = MIXER_CAP_VOL_ADC2,
        .intval = MIXER_CAP_V_ADC_DEF,
    },
    {
        .ctl_name = MIXER_CAP_VOL_ADC3,
        .intval = MIXER_CAP_V_ADC_DEF,
    },

    /* end of list */
    { .ctl_name = NULL, },
};

struct route_setting defaultsfm[] = {
    {
        .ctl_name = MIXER_DVC_IN_CAPTURE_VOL,
        .intval = MIXER_DVC_IN_CAP_VOL_DEF,
    },

    /* end of list */
    { .ctl_name = NULL, },
};

enum output_type {
    OUTPUT_STEREO,
    OUTPUT_MULTICHANNEL,
    OUTPUT_BT_SCO,
    OUTPUT_TOTAL
};

struct gen3_audio_device {
    struct audio_hw_device hw_device;

    pthread_mutex_t lock;       /* see note below on mutex acquisition order */
    struct mixer *mixer3168;
    struct mixer *mixerfm;
    audio_mode_t mode;
    int devices;
    struct gen3_stream_in *mic_input;
    struct gen3_stream_out *outputs[OUTPUT_TOTAL];
    bool mic_mute;
    bool master_mute;
    float master_volume;
    int tty_mode;
    struct echo_reference_itfe *echo_reference;

    unsigned int connected_in_device;
    struct gen3_stream_in *fm_input;
    struct gen3_stream_in *hfp_input;

};

enum pcm_type {
    PCM_NORMAL = 0,
    PCM_TOTAL,
};

struct gen3_stream_out {
    struct audio_stream_out stream;

    pthread_mutex_t lock;       /* see note below on mutex acquisition order */
    struct pcm_config config[PCM_TOTAL];
    struct pcm *pcm[PCM_TOTAL];
    struct resampler_itfe *resampler;
    char *buffer;
    size_t buffer_frames;
    int standby;
    struct echo_reference_itfe *echo_reference;
    audio_channel_mask_t channel_mask;
    unsigned int sample_rate;
    audio_format_t format;

    uint64_t written;

    struct gen3_audio_device *dev;

    char *upmixed_buffer;
    size_t upmixed_buffer_frames;

    int (*start_locked)(struct gen3_stream_out *stream);
};

#define MAX_PREPROCESSORS 1 /* maximum one AEC per input stream */

struct gen3_stream_in {
    struct audio_stream_in stream;

    pthread_mutex_t lock;       /* see note below on mutex acquisition order */
    struct pcm_config config;
    struct pcm *pcm;
    struct pcm_config rcar_config;
    struct pcm *rcar_pcm;
    int device;
    struct resampler_itfe *resampler;
    struct resampler_buffer_provider buf_provider;
    unsigned int requested_rate;
    int standby;
    int source;
    struct echo_reference_itfe *echo_reference;
    bool need_echo_reference;

    char *upmixed_read_buffer;

    int16_t *read_buf;
    size_t read_buf_size;
    size_t read_buf_frames;

    int16_t *proc_buf_in;
    int16_t *proc_buf_out;
    size_t proc_buf_size;
    size_t proc_buf_frames;

    int16_t *ref_buf;
    size_t ref_buf_size;
    size_t ref_buf_frames;

    int read_status;

    int num_preprocessors;
    effect_handle_t preprocessors[MAX_PREPROCESSORS];

    bool aux_channels_changed;
    uint32_t main_channels;
    uint32_t aux_channels;

    int (*start_locked)(struct gen3_stream_in *stream);
    int (*standby_locked)(struct gen3_stream_in *stream);
    int (*read_pcm_locked)(struct gen3_stream_in *in, void *read_buf, size_t bytes);

    struct gen3_audio_device *dev;
};


/**
 * NOTE: when multiple mutexes have to be acquired, always respect the following order:
 *        hw device > in stream > out stream
 */


static int adev_set_voice_volume(struct audio_hw_device *dev, float volume);
static int do_output_standby(struct gen3_stream_out *out);

/* The enable flag when 0 makes the assumption that enums are disabled by
 * "Off" and integers/booleans by 0 */
static int set_route_by_array(struct mixer *mixer, struct route_setting *route,
                              int enable)
{
    struct mixer_ctl *ctl;
    unsigned int i, j;

    /* Go through the route array and set each value */
    i = 0;
    while (route[i].ctl_name) {
        ctl = mixer_get_ctl_by_name(mixer, route[i].ctl_name);
        if (!ctl)
            return -EINVAL;

        if (route[i].strval) {
            if (enable)
                mixer_ctl_set_enum_by_string(ctl, route[i].strval);
            else
                mixer_ctl_set_enum_by_string(ctl, "Off");
        } else {
            /* This ensures multiple (i.e. stereo) values are set jointly */
            for (j = 0; j < mixer_ctl_get_num_values(ctl); j++) {
                if (enable)
                    mixer_ctl_set_value(ctl, j, route[i].intval);
                else
                    mixer_ctl_set_value(ctl, j, 0);
            }
        }
        i++;
    }

    return 0;
}

/* must be called with hw device and output stream mutexes locked */
static int start_output_stream(struct gen3_stream_out *out)
{
    ALOGV("%s",__FUNCTION__);
    struct gen3_audio_device *adev = out->dev;

    /* Something not a dock in use */
    out->config[PCM_NORMAL] = pcm_config_dac;
    out->config[PCM_NORMAL].rate = DEFAULT_OUT_SAMPLING_RATE;

    out->pcm[PCM_NORMAL] = pcm_open(CARD_GEN3_PCM3168A, PORT_DAC,
            PCM_OUT | PCM_MONOTONIC, &out->config[PCM_NORMAL]);

    /* Close any PCMs that could not be opened properly and return an error */
    if (out->pcm[PCM_NORMAL] && !pcm_is_ready(out->pcm[PCM_NORMAL])) {
        ALOGE("cannot open pcm_out driver normal: %s", pcm_get_error(out->pcm[PCM_NORMAL]));
        pcm_close(out->pcm[PCM_NORMAL]);
        out->pcm[PCM_NORMAL] = NULL;
        return -ENOMEM;
    }

    out->buffer_frames = pcm_config_dac.period_size * 2;

    if (out->buffer == NULL)
        out->buffer = (char*)malloc(out->buffer_frames * audio_stream_out_frame_size(&out->stream));

    if (adev->echo_reference != NULL)
        out->echo_reference = adev->echo_reference;
    out->resampler->reset(out->resampler);

    return 0;
}

/* must be called with hw device and output stream mutexes locked */
static int start_output_stream_stereo(struct gen3_stream_out *out)
{
    ALOGV("%s",__FUNCTION__);
    struct gen3_audio_device *adev = out->dev;

    /* Something not a dock in use */
    out->config[PCM_NORMAL] = pcm_config_dac;
    out->config[PCM_NORMAL].rate = DEFAULT_OUT_SAMPLING_RATE;

    out->pcm[PCM_NORMAL] = pcm_open(CARD_GEN3_PCM3168A, PORT_DAC,
            PCM_OUT | PCM_MONOTONIC, &out->config[PCM_NORMAL]);

    /* Close any PCMs that could not be opened properly and return an error */
    if (out->pcm[PCM_NORMAL] && !pcm_is_ready(out->pcm[PCM_NORMAL])) {
        ALOGE("cannot open pcm_out driver normal: %s", pcm_get_error(out->pcm[PCM_NORMAL]));
        pcm_close(out->pcm[PCM_NORMAL]);
        out->pcm[PCM_NORMAL] = NULL;
        return -ENOMEM;
    }

    if (out->buffer == NULL) {
        out->buffer_frames = pcm_config_dac.period_size * 2;
        out->buffer = (char*)malloc(out->buffer_frames * audio_stream_out_frame_size(&out->stream));
    }
    if (out->upmixed_buffer == NULL) {
        out->upmixed_buffer_frames = out->buffer_frames * 4;
        out->upmixed_buffer = (char*)malloc(out->upmixed_buffer_frames * audio_stream_out_frame_size(&out->stream));
        memset(out->upmixed_buffer, 0x0, out->upmixed_buffer_frames * audio_stream_out_frame_size(&out->stream));
    }

    if (adev->echo_reference != NULL)
        out->echo_reference = adev->echo_reference;
    out->resampler->reset(out->resampler);

    return 0;
}

static int start_output_stream_hfp(struct gen3_stream_out *out)
{
    ALOGV("%s",__FUNCTION__);

    /* Something not a dock in use */
    out->config[PCM_NORMAL] = pcm_config_hfp_out;
    out->pcm[PCM_NORMAL] = pcm_open(CARD_GEN3_BTSCO, PORT_BTSCO,
                                    PCM_OUT, &out->config[PCM_NORMAL]);

    /* Close any PCMs that could not be opened properly and return an error */
    if (out->pcm[PCM_NORMAL] && !pcm_is_ready(out->pcm[PCM_NORMAL])) {
        ALOGE("cannot open pcm_out driver normal: %s", pcm_get_error(out->pcm[PCM_NORMAL]));
        pcm_close(out->pcm[PCM_NORMAL]);
        out->pcm[PCM_NORMAL] = NULL;
        return -ENOMEM;
    }

    out->buffer_frames = pcm_config_hfp_out.period_size * 2;
    if (out->buffer == NULL)
        out->buffer = (char*)malloc(out->buffer_frames * audio_stream_out_frame_size(&out->stream));

    out->resampler->reset(out->resampler);

    return 0;
}

static int check_input_parameters(uint32_t sample_rate, audio_format_t format, int channel_count)
{
    if (format != DEFAULT_IN_ANDROID_FORMAT)
        return -EINVAL;

    if ((channel_count < 1) || (channel_count > 8))
        return -EINVAL;

    switch(sample_rate) {
    case 8000:
    case 11025:
    case 16000:
    case 22050:
    case 24000:
    case 32000:
    case 44100:
    case 48000:
    case 96000:
    case 192000:
        break;
    default:
        return -EINVAL;
    }

    return 0;
}

static size_t get_input_buffer_size(uint32_t sample_rate, audio_format_t format, int channel_count)
{
    size_t size;

    if (check_input_parameters(sample_rate, format, channel_count) != 0)
        return 0;

    /* take resampling into account and return the closest majoring
    multiple of 16 frames, as audioflinger expects audio buffers to
    be a multiple of 16 frames */
    size = (pcm_config_mic.period_size * sample_rate) / pcm_config_mic.rate;
    size = ((size + 15) / 16) * 16;
    return size * channel_count * sizeof(short);
}

static void add_echo_reference(struct gen3_stream_out *out,
                               struct echo_reference_itfe *reference)
{
    PTHREAD_MUTEX_LOCK(&out->lock);
    out->echo_reference = reference;
    PTHREAD_MUTEX_UNLOCK(&out->lock);
}

static void remove_echo_reference(struct gen3_stream_out *out,
                                  struct echo_reference_itfe *reference)
{
    PTHREAD_MUTEX_LOCK(&out->lock);
    if (out->echo_reference == reference) {
        /* stop writing to echo reference */
        reference->write(reference, NULL);
        out->echo_reference = NULL;
    }
    PTHREAD_MUTEX_UNLOCK(&out->lock);
}

static void put_echo_reference(struct gen3_audio_device *adev,
                          struct echo_reference_itfe *reference)
{
    if (adev->echo_reference != NULL &&
            reference == adev->echo_reference) {
        /* echo reference is taken from the low latency output stream used
         * for voice use cases */
        if (adev->outputs[OUTPUT_MULTICHANNEL] != NULL &&
                !adev->outputs[OUTPUT_MULTICHANNEL]->standby)
            remove_echo_reference(adev->outputs[OUTPUT_MULTICHANNEL], reference);
        release_echo_reference(reference);
        adev->echo_reference = NULL;
    }
}

static struct echo_reference_itfe *get_echo_reference(struct gen3_audio_device *adev,
                                               audio_format_t format,
                                               uint32_t channel_count,
                                               uint32_t sampling_rate)
{
    put_echo_reference(adev, adev->echo_reference);
    /* echo reference is taken from the low latency output stream used
     * for voice use cases */
    if (adev->outputs[OUTPUT_MULTICHANNEL] != NULL &&
            !adev->outputs[OUTPUT_MULTICHANNEL]->standby) {
        struct audio_stream *stream =
                &adev->outputs[OUTPUT_MULTICHANNEL]->stream.common;
        uint32_t wr_channel_count = popcount(stream->get_channels(stream));
        uint32_t wr_sampling_rate = stream->get_sample_rate(stream);

        int status = create_echo_reference(format,
                                           channel_count,
                                           sampling_rate,
                                           format,
                                           wr_channel_count,
                                           wr_sampling_rate,
                                           &adev->echo_reference);
        if (status == 0)
            add_echo_reference(adev->outputs[OUTPUT_MULTICHANNEL],
                               adev->echo_reference);
    }
    return adev->echo_reference;
}

static int get_playback_delay(struct gen3_stream_out *out,
                       size_t frames,
                       struct echo_reference_buffer *buffer)
{
    unsigned int kernel_frames;
    int status;
    int primary_pcm = 0;

    /* Find the first active PCM to act as primary */
    while ((primary_pcm < PCM_TOTAL) && !out->pcm[primary_pcm])
        primary_pcm++;

    status = pcm_get_htimestamp(out->pcm[primary_pcm], &kernel_frames, &buffer->time_stamp);
    if (status < 0) {
        buffer->time_stamp.tv_sec  = 0;
        buffer->time_stamp.tv_nsec = 0;
        buffer->delay_ns           = 0;
        ALOGV("get_playback_delay(): pcm_get_htimestamp error,"
                "setting playbackTimestamp to 0");
        return status;
    }

    kernel_frames = pcm_get_buffer_size(out->pcm[primary_pcm]) - kernel_frames;

    /* adjust render time stamp with delay added by current driver buffer.
     * Add the duration of current frame as we want the render time of the last
     * sample being written. */
    buffer->delay_ns = (long)(((int64_t)(kernel_frames + frames)* 1000000000)/
                            DEFAULT_OUT_SAMPLING_RATE);

    return 0;
}

static uint32_t out_get_sample_rate(const struct audio_stream * stream)
{
    struct gen3_stream_out *out = (struct gen3_stream_out *)stream;

    return out->sample_rate;
}

static int out_set_sample_rate(struct audio_stream * stream, uint32_t rate)
{
    ALOGD("%s: rate = %d",__FUNCTION__, rate);
    struct gen3_stream_out *out = (struct gen3_stream_out *)stream;

    out->sample_rate = rate;
    return 0;
}

static size_t out_get_buffer_size_hfp(const struct audio_stream *stream)
{
    struct gen3_stream_out *out = (struct gen3_stream_out *)stream;

    /* take resampling into account and return the closest majoring
    multiple of 16 frames, as audioflinger expects audio buffers to
    be a multiple of 16 frames. Note: we use the default rate here
    from pcm_config_hfp_out.rate. */
    size_t size = (PLAY_PERIOD_SIZE * DEFAULT_OUT_SAMPLING_RATE) / pcm_config_hfp_out.rate;
    size = ((size + 15) / 16) * 16;
    return size * audio_stream_out_frame_size(&out->stream);
}

static size_t out_get_buffer_size_stereo(const struct audio_stream *stream)
{
    struct gen3_stream_out *out = (struct gen3_stream_out *)stream;

    /* take resampling into account and return the closest majoring
    multiple of 16 frames, as audioflinger expects audio buffers to
    be a multiple of 16 frames. Note: we use the default rate here
    from pcm_config_dac.rate. */
    size_t size = 0;
#ifdef ENABLE_ADSP
    size = (PLAY_PERIOD_SIZE * PLAY_PERIOD_COUNT * DEFAULT_OUT_SAMPLING_RATE) / pcm_config_dac.rate;
#else
    size = (PLAY_PERIOD_SIZE * DEFAULT_OUT_SAMPLING_RATE) / pcm_config_dac.rate;
#endif
    size = ((size + 15) / 16) * 16;
    return size * audio_stream_out_frame_size(&out->stream);
}

static size_t out_get_buffer_size(const struct audio_stream *stream)
{
    struct gen3_stream_out *out = (struct gen3_stream_out *)stream;

    /* take resampling into account and return the closest majoring
    multiple of 16 frames, as audioflinger expects audio buffers to
    be a multiple of 16 frames. Note: we use the default rate here
    from pcm_config_dac.rate. */
    size_t size = 0;
#ifdef ENABLE_ADSP
    size = (PLAY_PERIOD_SIZE * PLAY_PERIOD_COUNT * DEFAULT_OUT_SAMPLING_RATE) / pcm_config_dac.rate;
#else
    size = (PLAY_PERIOD_SIZE * DEFAULT_OUT_SAMPLING_RATE) / pcm_config_dac.rate;
#endif // ENABLE_ADSP
    size = ((size + 15) / 16) * 16;
    return size * audio_stream_out_frame_size(&out->stream);
}

static uint32_t out_get_channels(const struct audio_stream *stream)
{
    struct gen3_stream_out *out = (struct gen3_stream_out *)stream;

    return out->channel_mask;
}

static audio_format_t out_get_format(const struct audio_stream * stream)
{
    struct gen3_stream_out *out = (struct gen3_stream_out *)stream;

    return out->format; // DEFAULT_OUT_ANDROID_FORMAT;
}

static int out_set_format(struct audio_stream * stream, audio_format_t format)
{
    ALOGV("%s: format = %d",__FUNCTION__, format);
    struct gen3_stream_out *out = (struct gen3_stream_out *)stream;

    out->format = format;
    return 0;
}

/* must be called with hw device and output stream mutexes locked */
static int do_output_standby(struct gen3_stream_out *out)
{
    struct gen3_audio_device *adev = out->dev;
    int i;
    bool all_outputs_in_standby = true;

    if (!out->standby) {
        out->standby = 1;

        for (i = 0; i < PCM_TOTAL; i++) {
            if (out->pcm[i]) {
                pcm_close(out->pcm[i]);
                out->pcm[i] = NULL;
            }
        }

        for (i = 0; i < OUTPUT_TOTAL; i++) {
            if (adev->outputs[i] != NULL && !adev->outputs[i]->standby) {
                all_outputs_in_standby = false;
                break;
            }
        }

        /* stop writing to echo reference */
        if (out->echo_reference != NULL) {
            out->echo_reference->write(out->echo_reference, NULL);
            out->echo_reference = NULL;
        }
    }
    return 0;
}

static int out_standby(struct audio_stream *stream)
{
    struct gen3_stream_out *out = (struct gen3_stream_out *)stream;
    int status;

    PTHREAD_MUTEX_LOCK(&out->lock);
    status = do_output_standby(out);
    PTHREAD_MUTEX_UNLOCK(&out->lock);
    return status;
}

static int out_dump(const struct audio_stream * /*stream*/, int /*fd*/)
{
    return 0;
}

static int out_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    ALOGV("%s: kvpairs=%s", __FUNCTION__, kvpairs);

    struct gen3_stream_out *out = (struct gen3_stream_out *)stream;
    struct gen3_audio_device *adev = out->dev;
    struct str_parms *parms;
    char value[32];
    int ret, val = 0;

    parms = str_parms_create_str(kvpairs);
    memset(value, 0x0, sizeof(value));

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING, value, sizeof(value));
    if (ret >= 0) {
        val = atoi(value);
        PTHREAD_MUTEX_LOCK(&adev->lock);
        PTHREAD_MUTEX_LOCK(&out->lock);
        if (((adev->devices & AUDIO_DEVICE_OUT_ALL) != val) && (val != 0)) {
            adev->devices &= ~AUDIO_DEVICE_OUT_ALL;
            adev->devices |= val;
            do_output_standby(out);
        }
        PTHREAD_MUTEX_UNLOCK(&out->lock);
        PTHREAD_MUTEX_UNLOCK(&adev->lock);
    }

    str_parms_destroy(parms);
    return 0;
}

static int out_set_parameters_hfp(struct audio_stream * /*stream*/, const char *kvpairs)
{
    ALOGD("%s: kvpairs=%s", __FUNCTION__, kvpairs);
    return 0;
}


static char * out_get_parameters(const struct audio_stream *stream, const char *keys)
{
    ALOGV("%s: keys=%s", __FUNCTION__, keys);

    struct gen3_stream_out *out = (struct gen3_stream_out *)stream;
    struct gen3_audio_device *adev = out->dev;
    if (strcmp(keys, "fmread") == 0) {
        return adev->hw_device.get_parameters(&adev->hw_device, keys);
    } else if (strcmp(keys, "hfpread") == 0) {
        return adev->hw_device.get_parameters(&adev->hw_device, keys);
    } else if (strcmp(keys, "micread") == 0) {
        return adev->hw_device.get_parameters(&adev->hw_device, keys);
    } else if (strcmp(keys, "hfpout") == 0) {
        return adev->hw_device.get_parameters(&adev->hw_device, keys);
    }
    return strdup("");
}

static uint32_t out_get_latency(const struct audio_stream_out *stream __unused)
{
    /*  Note: we use the default rate here from pcm_config_dac.rate */
    return (PLAY_PERIOD_SIZE * PLAY_PERIOD_COUNT * 1000) / pcm_config_dac.rate;
}

static int out_set_volume(struct audio_stream_out * /*stream*/,
                            float left, float right)
{
    ALOGV("%s: left = %f, right = %f",__FUNCTION__, left, right);
    return -ENOSYS;
}

static ssize_t out_write(struct audio_stream_out *stream, const void* buffer,
                         size_t bytes)
{
    int ret;
    struct gen3_stream_out *out = (struct gen3_stream_out *)stream;
    struct gen3_audio_device *adev = out->dev;
    size_t frame_size = audio_stream_out_frame_size(&out->stream);
    size_t in_frames = bytes / frame_size;
    size_t out_frames = in_frames;
    bool force_input_standby = false;
    struct gen3_stream_in *in;
    int i;

    /* acquiring hw device mutex systematically is useful if a low priority thread is waiting
     * on the output stream mutex - e.g. executing select_mode() while holding the hw device
     * mutex
     */
    PTHREAD_MUTEX_LOCK(&adev->lock);
    PTHREAD_MUTEX_LOCK(&out->lock);
    if (out->standby) {
        ret = out->start_locked(out);
        if (ret != 0) {
            PTHREAD_MUTEX_UNLOCK(&adev->lock);
            goto exit;
        }
        out->standby = 0;
        /* a change in output device may change the microphone selection */
        if (adev->mic_input &&
                adev->mic_input->source == AUDIO_SOURCE_VOICE_COMMUNICATION)
            force_input_standby = true;
    }
    PTHREAD_MUTEX_UNLOCK(&adev->lock);

    for (i = 0; i < PCM_TOTAL; i++) {
        /* only use resampler if required */
        if (out->pcm[i] && (out->config[i].rate != DEFAULT_OUT_SAMPLING_RATE)) {
            out_frames = out->buffer_frames;
            out->resampler->resample_from_input(out->resampler,
                                                (int16_t *)buffer,
                                                &in_frames,
                                                (int16_t *)out->buffer,
                                                &out_frames);
            break;
        }
    }

    if (out->echo_reference != NULL) {
        struct echo_reference_buffer b;
        b.raw = (void *)buffer;
        b.frame_count = in_frames;

        get_playback_delay(out, out_frames, &b);
        out->echo_reference->write(out->echo_reference, &b);
    }

    /* Write to all active PCMs */
    for (i = 0; i < PCM_TOTAL; i++) {
        if (out->pcm[i]) {
            if (out->config[i].rate == DEFAULT_OUT_SAMPLING_RATE)
                /* PCM uses native sample rate */
                ret = pcm_write(out->pcm[i], (void *)buffer, bytes);
            else
                /* PCM needs resampler */
                ret = pcm_write(out->pcm[i], (void *)out->buffer, out_frames * frame_size);
            if (ret)
                break;
            out->written += bytes / (out->config[i].channels * sizeof(short));
        }
    }

exit:
    PTHREAD_MUTEX_UNLOCK(&out->lock);

    if (ret != 0) {
        usleep(bytes * 1000000 / audio_stream_out_frame_size(stream) /
               DEFAULT_OUT_SAMPLING_RATE);
    }

    if (force_input_standby) {
        PTHREAD_MUTEX_LOCK(&adev->lock);
        if (adev->mic_input) {
            in = adev->mic_input;
            PTHREAD_MUTEX_LOCK(&in->lock);
            in->standby_locked(in);
            PTHREAD_MUTEX_UNLOCK(&in->lock);
        }
        PTHREAD_MUTEX_UNLOCK(&adev->lock);
    }

    return bytes;
}

static ssize_t out_write_stereo(struct audio_stream_out *stream, const void* buffer,
                         size_t bytes)
{
    int ret;
    struct gen3_stream_out *out = (struct gen3_stream_out *)stream;
    struct gen3_audio_device *adev = out->dev;
    size_t frame_size = audio_stream_out_frame_size(&out->stream);
    size_t in_frames = bytes / frame_size;
    size_t out_frames = in_frames;
    bool force_input_standby = false;
    struct gen3_stream_in *in;
    int i;

    /* acquiring hw device mutex systematically is useful if a low priority thread is waiting
     * on the output stream mutex - e.g. executing select_mode() while holding the hw device
     * mutex
     */
    PTHREAD_MUTEX_LOCK(&adev->lock);
    PTHREAD_MUTEX_LOCK(&out->lock);
    if (out->standby) {
        ret = out->start_locked(out);
        if (ret != 0) {
            PTHREAD_MUTEX_UNLOCK(&adev->lock);
            goto exit;
        }
        out->standby = 0;
        /* a change in output device may change the microphone selection */
        if (adev->mic_input &&
                adev->mic_input->source == AUDIO_SOURCE_VOICE_COMMUNICATION)
            force_input_standby = true;
    }
    PTHREAD_MUTEX_UNLOCK(&adev->lock);

    for (i = 0; i < PCM_TOTAL; i++) {
        /* only use resampler if required */
        if (out->pcm[i] && (out->config[i].rate != DEFAULT_OUT_SAMPLING_RATE)) {
            out_frames = out->buffer_frames;
            out->resampler->resample_from_input(out->resampler,
                                                (int16_t *)buffer,
                                                &in_frames,
                                                (int16_t *)out->buffer,
                                                &out_frames);
            break;
        }
    }

    if (out->echo_reference != NULL) {
        struct echo_reference_buffer b;
        b.raw = (void *)buffer;
        b.frame_count = in_frames;

        get_playback_delay(out, out_frames, &b);
        out->echo_reference->write(out->echo_reference, &b);
    }

    for (i = 0; i < PCM_TOTAL; i++) {
        if (out->pcm[i]) {
            const char *buffer_to_copy = nullptr;
            size_t frames_to_copy = 0;
            if (out->config[i].rate == DEFAULT_OUT_SAMPLING_RATE) {
                buffer_to_copy = (const char*)buffer;
                frames_to_copy = bytes / frame_size;
            } else {
                buffer_to_copy = out->buffer;
                frames_to_copy = out_frames;
            }
            if (out->upmixed_buffer_frames < frames_to_copy * 4) {
                out->upmixed_buffer_frames = frames_to_copy * 4;
                out->upmixed_buffer = (char*)realloc(out->upmixed_buffer,
                        out->upmixed_buffer_frames * audio_stream_out_frame_size(&out->stream));
                memset(out->upmixed_buffer, 0x0,
                        out->upmixed_buffer_frames * audio_stream_out_frame_size(&out->stream));
            }
            for (size_t counter = 0, offset = 0; counter < frames_to_copy; counter++, offset += (frame_size * 4)) {
                memmove(out->upmixed_buffer + offset, buffer_to_copy + frame_size * counter, frame_size);
            }
            ret = pcm_write(out->pcm[i], out->upmixed_buffer, frames_to_copy * frame_size * 4);
            out->written += bytes / frame_size;
        }
    }
exit:
    PTHREAD_MUTEX_UNLOCK(&out->lock);

    if (ret != 0) {
        usleep(bytes * 1000000 / audio_stream_out_frame_size(stream) /
               DEFAULT_OUT_SAMPLING_RATE);
    }

    if (force_input_standby) {
        PTHREAD_MUTEX_LOCK(&adev->lock);
        if (adev->mic_input) {
            in = adev->mic_input;
            PTHREAD_MUTEX_LOCK(&in->lock);
            in->standby_locked(in);
            PTHREAD_MUTEX_UNLOCK(&in->lock);
        }
        PTHREAD_MUTEX_UNLOCK(&adev->lock);
    }

    return bytes;
}

static ssize_t out_write_hfp(struct audio_stream_out *stream, const void* buffer,
                         size_t bytes)
{
    int ret;
    struct gen3_stream_out *out = (struct gen3_stream_out *)stream;
    struct gen3_audio_device *adev = out->dev;
    size_t frame_size = audio_stream_out_frame_size(&out->stream);
    size_t in_frames = bytes / frame_size;
    size_t out_frames = in_frames;
    int i;

    /* acquiring hw device mutex systematically is useful if a low priority thread is waiting
     * on the output stream mutex - e.g. executing select_mode() while holding the hw device
     * mutex
     */
    PTHREAD_MUTEX_LOCK(&adev->lock);
    PTHREAD_MUTEX_LOCK(&out->lock);
    if (out->standby) {
        ret = out->start_locked(out);
        if (ret != 0) {
            PTHREAD_MUTEX_UNLOCK(&adev->lock);
            goto exit;
        }
        out->standby = 0;
    }
    PTHREAD_MUTEX_UNLOCK(&adev->lock);

    for (i = 0; i < PCM_TOTAL; i++) {
        /* only use resampler if required */
        if (out->pcm[i] && (out->config[i].rate != DEFAULT_OUT_SAMPLING_RATE)) {
            out_frames = out->buffer_frames;
            out->resampler->resample_from_input(out->resampler,
                                                (int16_t *)buffer,
                                                &in_frames,
                                                (int16_t *)out->buffer,
                                                &out_frames);
            break;
        }
    }

    /* Write to all active PCMs */
    for (i = 0; i < PCM_TOTAL; i++) {
        if (out->pcm[i]) {
            if (out->config[i].rate == DEFAULT_OUT_SAMPLING_RATE) {
                /* PCM uses native sample rate */
                ret = pcm_write(out->pcm[i], (void *)buffer, bytes);
            } else {
                /* PCM needs resampler */
                ret = pcm_write(out->pcm[i], (void *)out->buffer, out_frames * frame_size);
            }
            if (ret)
                break;
            out->written += bytes / (out->config[i].channels * sizeof(short));
        }
    }

exit:
    PTHREAD_MUTEX_UNLOCK(&out->lock);

    if (ret != 0) {
        usleep(bytes * 1000000 / audio_stream_out_frame_size(stream) /
               DEFAULT_OUT_SAMPLING_RATE);
    }

    return bytes;
}

static int out_get_render_position(const struct audio_stream_out * /*stream*/,
                                   uint32_t * /*dsp_frames*/)
{
    ALOGV("%s:", __FUNCTION__);
    return -ENOSYS;
}

static int out_get_next_write_timestamp(const struct audio_stream_out * /*stream*/,
                                    int64_t * /*timestamp*/)
{
    ALOGV("%s:", __FUNCTION__);
    return 0;
}

static int out_set_callback(struct audio_stream_out * /*stream*/,
                        stream_callback_t /*callback*/, void * /*cookie*/)
{
    ALOGV("%s:", __FUNCTION__);
    return 0;
}

static int out_drain(struct audio_stream_out * /*stream*/,
                     audio_drain_type_t /*type*/ )
{
    ALOGV("%s:", __FUNCTION__);
    return 0;
}

static int out_flush(struct audio_stream_out * /*stream*/)
{
    ALOGV("%s:", __FUNCTION__);
    return 0;
}

static int out_get_presentation_position(const struct audio_stream_out *stream,
               uint64_t *frames, struct timespec *timestamp)
{
    ALOGV("%s:", __FUNCTION__);
    struct gen3_stream_out *out = (struct gen3_stream_out *)stream;
    int ret = -1;

    pthread_mutex_lock(&out->lock);

    unsigned int avail;
    if (out->pcm[PCM_NORMAL]) {
        if (pcm_get_htimestamp(out->pcm[PCM_NORMAL], &avail, timestamp) == 0) {
            size_t kernel_buffer_size = out->config[PCM_NORMAL].period_size * out->config[PCM_NORMAL].period_count;
            // FIXME This calculation is incorrect if there is buffering after app processor
            int64_t signed_frames = out->written - kernel_buffer_size + avail;
            // It would be unusual for this value to be negative, but check just in case ...
            if (signed_frames >= 0) {
                *frames = signed_frames;
                ret = 0;
            }
        }
    }

    pthread_mutex_unlock(&out->lock);
    return ret;
}

static int out_start(const struct audio_stream_out * /*stream*/)
{
    ALOGV("%s:", __FUNCTION__);
    return 0;
}

static int out_stop(const struct audio_stream_out * /*stream*/)
{
    ALOGV("%s:", __FUNCTION__);
    return 0;
}

static int out_create_mmap_buffer(const struct audio_stream_out * /*stream*/,
               int32_t /*min_size_frames*/, struct audio_mmap_buffer_info * /*info*/)
{
    ALOGV("%s:", __FUNCTION__);
    return 0;
}

static int out_get_mmap_position(const struct audio_stream_out * /*stream*/,
               struct audio_mmap_position * /*position*/)
{
    ALOGV("%s:", __FUNCTION__);
    return 0;
}


static int out_add_audio_effect(const struct audio_stream * /*stream*/, effect_handle_t /*effect*/)
{
    ALOGV("%s", __FUNCTION__);
    return -ENOSYS;
}

static int out_remove_audio_effect(const struct audio_stream * /*stream*/, effect_handle_t /*effect*/)
{
    ALOGV("%s", __FUNCTION__);
    return -ENOSYS;
}

static audio_devices_t out_get_device(const struct audio_stream * /*stream*/)
{
    ALOGV("%s", __FUNCTION__);
    return -ENOSYS;
}

static int out_set_device(struct audio_stream * /*stream*/, audio_devices_t /*device*/)
{
    ALOGV("%s", __FUNCTION__);
    return -ENOSYS;
}

/** audio_stream_in implementation **/

/* must be called with hw device and input stream mutexes locked */
static int start_locked_fm(struct gen3_stream_in *in)
{
    ALOGV("%s", __FUNCTION__);

    in->pcm = pcm_open(CARD_GEN3_FM, PORT_FM, PCM_IN, &pcm_config_fm);
    if (!pcm_is_ready(in->pcm)) {
        ALOGE("cannot open pcm_in driver: %s", pcm_get_error(in->pcm));
        pcm_close(in->pcm);
        return -ENOMEM;
    }

    return 0;
}

static int start_locked_hfp(struct gen3_stream_in *in)
{
    ALOGV("%s", __FUNCTION__);

    in->pcm = pcm_open(CARD_GEN3_BTSCO, PORT_BTSCO, PCM_IN, &pcm_config_hfp_in);
    if (!pcm_is_ready(in->pcm)) {
        ALOGE("cannot open pcm_in driver: %s", pcm_get_error(in->pcm));
        pcm_close(in->pcm);
        return -ENOMEM;
    }

    /* force read and proc buf reallocation case of frame size or channel count change */
    in->read_buf_frames = 0;
    in->read_buf_size = 0;
    in->proc_buf_frames = 0;
    in->proc_buf_size = 0;
    /* if no supported sample rate is available, use the resampler */
    if (in->resampler) {
        in->resampler->reset(in->resampler);
    }

    return 0;
}

static int start_locked_mic(struct gen3_stream_in *in)
{
    int ret = 0;
    struct gen3_audio_device *adev = in->dev;

    adev->mic_input = in;

    if (adev->mode != AUDIO_MODE_IN_CALL) {
        adev->devices &= ~AUDIO_DEVICE_IN_ALL;
        adev->devices |= in->device;
    }

    if (in->aux_channels_changed) {
        in->aux_channels_changed = false;
        in->config.channels = popcount(in->main_channels | in->aux_channels);

        if (in->resampler) {
            /* release and recreate the resampler with the new number of channel of the input */
            release_resampler(in->resampler);
            in->resampler = NULL;
            ret = create_resampler(in->config.rate,
                               in->requested_rate,
                               in->config.channels,
                               RESAMPLER_QUALITY_DEFAULT,
                               &in->buf_provider,
                               &in->resampler);
        }
        ALOGD("start_locked_mic(): New channel configuration, "
                "main_channels = [%04x], aux_channels = [%04x], config.channels = %d",
                in->main_channels, in->aux_channels, in->config.channels);
    }

    if (in->need_echo_reference && in->echo_reference == NULL)
        in->echo_reference = get_echo_reference(adev,
                                        AUDIO_FORMAT_PCM_16_BIT,
                                        in->config.channels,
                                        in->requested_rate);

    /* this assumes routing is done previously */
    in->pcm = pcm_open(CARD_GEN3_PCM3168A, PORT_MIC, PCM_IN, &pcm_config_mic);
    if (!pcm_is_ready(in->pcm)) {
        ALOGE("cannot open pcm_in driver: %s", pcm_get_error(in->pcm));
        pcm_close(in->pcm);
        adev->mic_input = NULL;
        return -ENOMEM;
    }

    /* force read and proc buf reallocation case of frame size or channel count change */
    in->read_buf_frames = 0;
    in->read_buf_size = 0;
    in->proc_buf_frames = 0;
    in->proc_buf_size = 0;
    /* if no supported sample rate is available, use the resampler */
    if (in->resampler) {
        in->resampler->reset(in->resampler);
    }

    size_t pcm_frame_size = audio_stream_in_frame_size(&in->stream) * pcm_config_mic.channels / popcount(in->main_channels);
    if (in->upmixed_read_buffer == NULL) {
        in->upmixed_read_buffer = (char*)malloc(pcm_config_mic.period_size * pcm_frame_size);
        if (in->upmixed_read_buffer == NULL)
        {
            pcm_close(in->pcm);
            adev->mic_input = NULL;
            return -ENOMEM;
        }
        memset(in->upmixed_read_buffer, 0x0, pcm_config_mic.period_size * pcm_frame_size);
    }

    return 0;
}

static uint32_t in_get_sample_rate(const struct audio_stream *stream)
{
    struct gen3_stream_in *in = (struct gen3_stream_in *)stream;

    return in->requested_rate;
}

static int in_set_sample_rate(struct audio_stream * /*stream*/, uint32_t rate)
{
    ALOGV("%s: rate = %d",__FUNCTION__, rate);
    return -ENOSYS;
}

static size_t in_get_buffer_size(const struct audio_stream *stream)
{
    struct gen3_stream_in *in = (struct gen3_stream_in *)stream;
    return in->config.period_size * audio_stream_in_frame_size(&in->stream);
}

static uint32_t in_get_channels(const struct audio_stream *stream)
{
    struct gen3_stream_in *in = (struct gen3_stream_in *)stream;

    return in->main_channels;
}

static audio_format_t in_get_format(const struct audio_stream * /*stream*/)
{
    return DEFAULT_IN_ANDROID_FORMAT;
}

static int in_set_format(struct audio_stream * /*stream*/, audio_format_t format)
{
    ALOGV("%s: format = %d",__FUNCTION__, format);
    return -ENOSYS;
}

/* must be called with hw device and input stream mutexes locked */
static int standby_locked_mic(struct gen3_stream_in *in)
{
    struct gen3_audio_device *adev = in->dev;

    if (!in->standby) {
        if (in->upmixed_read_buffer) {
            size_t pcm_frame_size = audio_stream_in_frame_size(&in->stream) * pcm_config_mic.channels / popcount(in->main_channels);
            memset(in->upmixed_read_buffer, 0x0, pcm_config_mic.period_size * pcm_frame_size);
        }

        pcm_close(in->pcm);
        in->pcm = NULL;

        adev->mic_input = 0;
        if (adev->mode != AUDIO_MODE_IN_CALL)
            adev->devices &= ~AUDIO_DEVICE_IN_ALL;

        if (in->echo_reference != NULL) {
            /* stop reading from echo reference */
            in->echo_reference->read(in->echo_reference, NULL);
            put_echo_reference(adev, in->echo_reference);
            in->echo_reference = NULL;
        }

        in->standby = 1;
    }
    return 0;
}

static int standby_locked_fm(struct gen3_stream_in *in)
{
    ALOGV("%s", __FUNCTION__);

    if (!in->standby) {
        pcm_close(in->pcm);
        in->pcm = NULL;
        in->standby = 1;
    }
    return 0;
}

static int standby_locked_hfp(struct gen3_stream_in *in)
{
    ALOGV("%s", __FUNCTION__);

    if (!in->standby) {
        pcm_close(in->pcm);
        in->pcm = NULL;
        in->standby = 1;
    }
    return 0;
}

static int in_standby(struct audio_stream *stream)
{
    struct gen3_stream_in *in = (struct gen3_stream_in *)stream;
    int status;

    PTHREAD_MUTEX_LOCK(&in->dev->lock);
    PTHREAD_MUTEX_LOCK(&in->lock);
    status = in->standby_locked(in);
    PTHREAD_MUTEX_UNLOCK(&in->lock);
    PTHREAD_MUTEX_UNLOCK(&in->dev->lock);
    return status;
}

static int in_dump(const struct audio_stream * /*stream*/, int /*fd*/)
{
    return 0;
}

static int in_set_parameters_fm(struct audio_stream * /*stream*/, const char * /*kvpairs*/)
{
    ALOGV("%s", __FUNCTION__);
    return 0;
}

static int in_set_parameters_hfp(struct audio_stream * /*stream*/, const char * /*kvpairs*/)
{
    ALOGV("%s", __FUNCTION__);
    return 0;
}

static int in_set_parameters_mic(struct audio_stream *stream, const char *kvpairs)
{
    struct gen3_stream_in *in = (struct gen3_stream_in *)stream;
    struct gen3_audio_device *adev = in->dev;
    struct str_parms *parms;
    char value[32];
    int ret, val = 0;
    bool do_standby = false;

    parms = str_parms_create_str(kvpairs);

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_INPUT_SOURCE, value, sizeof(value));

    PTHREAD_MUTEX_LOCK(&adev->lock);
    PTHREAD_MUTEX_LOCK(&in->lock);
    if (ret >= 0) {
        val = atoi(value);
        /* no audio source uses val == 0 */
        if ((in->source != val) && (val != 0)) {
            in->source = val;
            do_standby = true;
        }
    }

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING, value, sizeof(value));
    if (ret >= 0) {
        val = atoi(value);
        if ((in->device != val) && (val != 0)) {
            in->device = val;
            do_standby = true;
        }
    }

    if (do_standby)
        in->standby_locked(in);
    PTHREAD_MUTEX_UNLOCK(&in->lock);
    PTHREAD_MUTEX_UNLOCK(&adev->lock);

    str_parms_destroy(parms);
    return 0;
}

static char * in_get_parameters(const struct audio_stream * /*stream*/,
                                const char * /*keys*/)
{
    return strdup("");
}

static int in_set_gain(struct audio_stream_in * /*stream*/, float gain)
{
    ALOGV("%s: gain = %f",__FUNCTION__, gain);
    return 0;
}

static void get_capture_delay(struct gen3_stream_in *in,
                       size_t frames,
                       struct echo_reference_buffer *buffer)
{

    /* read frames available in kernel driver buffer */
    unsigned int kernel_frames;
    struct timespec tstamp;
    long buf_delay;
    long rsmp_delay;
    long kernel_delay;
    long delay_ns;

    if (pcm_get_htimestamp(in->pcm, &kernel_frames, &tstamp) < 0) {
        buffer->time_stamp.tv_sec  = 0;
        buffer->time_stamp.tv_nsec = 0;
        buffer->delay_ns           = 0;
        ALOGW("read get_capture_delay(): pcm_htimestamp error");
        return;
    }

    /* read frames available in audio HAL input buffer
     * add number of frames being read as we want the capture time of first sample
     * in current buffer */
    /* frames in in->buffer are at driver sampling rate while frames in in->proc_buf are
     * at requested sampling rate */
    buf_delay = (long)(((int64_t)(in->read_buf_frames) * 1000000000) / in->config.rate +
                       ((int64_t)(in->proc_buf_frames) * 1000000000) /
                           in->requested_rate);

    /* add delay introduced by resampler */
    rsmp_delay = 0;
    if (in->resampler) {
        rsmp_delay = in->resampler->delay_ns(in->resampler);
    }

    kernel_delay = (long)(((int64_t)kernel_frames * 1000000000) / in->config.rate);

    delay_ns = kernel_delay + buf_delay + rsmp_delay;

    buffer->time_stamp = tstamp;
    buffer->delay_ns   = delay_ns;
    ALOGV("get_capture_delay time_stamp = [%ld].[%ld], delay_ns: [%d],"
         " kernel_delay:[%ld], buf_delay:[%ld], rsmp_delay:[%ld], kernel_frames:[%u], "
         "in->read_buf_frames:[%zu], in->proc_buf_frames:[%zu], frames:[%zu]",
         buffer->time_stamp.tv_sec , buffer->time_stamp.tv_nsec, buffer->delay_ns,
         kernel_delay, buf_delay, rsmp_delay, kernel_frames,
         in->read_buf_frames, in->proc_buf_frames, frames);

}

static int32_t update_echo_reference(struct gen3_stream_in *in, size_t frames)
{
    struct echo_reference_buffer b;
    b.delay_ns = 0;

    ALOGV("update_echo_reference, frames = [%zu], in->ref_buf_frames = [%zu],  "
          "b.frame_count = [%zu]",
         frames, in->ref_buf_frames, frames - in->ref_buf_frames);
    if (in->ref_buf_frames < frames) {
        if (in->ref_buf_size < frames) {
            in->ref_buf_size = frames;
            in->ref_buf = (int16_t *)realloc(in->ref_buf, pcm_frames_to_bytes(in->pcm, frames));
            ALOG_ASSERT((in->ref_buf != NULL),
                        "update_echo_reference() failed to reallocate ref_buf");
            ALOGV("update_echo_reference(): ref_buf %p extended to %d bytes",
                      in->ref_buf, pcm_frames_to_bytes(in->pcm, frames));
        }
        b.frame_count = frames - in->ref_buf_frames;
        b.raw = (void *)(in->ref_buf + in->ref_buf_frames * in->config.channels);

        get_capture_delay(in, frames, &b);

        if (in->echo_reference->read(in->echo_reference, &b) == 0)
        {
            in->ref_buf_frames += b.frame_count;
            ALOGV("update_echo_reference(): in->ref_buf_frames:[%zu], "
                    "in->ref_buf_size:[%zu], frames:[%zu], b.frame_count:[%zu]",
                 in->ref_buf_frames, in->ref_buf_size, frames, b.frame_count);
        }
    } else
        ALOGW("update_echo_reference(): NOT enough frames to read ref buffer");
    return b.delay_ns;
}

static int set_preprocessor_param(effect_handle_t handle,
                           effect_param_t *param)
{
    uint32_t size = sizeof(int);
    uint32_t psize = ((param->psize - 1) / sizeof(int) + 1) * sizeof(int) +
                        param->vsize;

    int status = (*handle)->command(handle,
                                   EFFECT_CMD_SET_PARAM,
                                   sizeof (effect_param_t) + psize,
                                   param,
                                   &size,
                                   &param->status);
    if (status == 0)
        status = param->status;

    return status;
}

static int set_preprocessor_echo_delay(effect_handle_t handle,
                                     int32_t delay_us)
{
    uint32_t buf[sizeof(effect_param_t) / sizeof(uint32_t) + 2];
    effect_param_t *param = (effect_param_t *)buf;

    param->psize = sizeof(uint32_t);
    param->vsize = sizeof(uint32_t);
    *(uint32_t *)param->data = AEC_PARAM_ECHO_DELAY;
    *((int32_t *)param->data + 1) = delay_us;

    return set_preprocessor_param(handle, param);
}

static void push_echo_reference(struct gen3_stream_in *in, size_t frames)
{
    /* read frames from echo reference buffer and update echo delay
     * in->ref_buf_frames is updated with frames available in in->ref_buf */
    int32_t delay_us = update_echo_reference(in, frames)/1000;
    int i;
    audio_buffer_t buf;

    if (in->ref_buf_frames < frames)
        frames = in->ref_buf_frames;

    buf.frameCount = frames;
    buf.raw = in->ref_buf;

    for (i = 0; i < in->num_preprocessors; i++) {
        if ((*in->preprocessors[i])->process_reverse == NULL)
            continue;

        (*in->preprocessors[i])->process_reverse(in->preprocessors[i],
                                               &buf,
                                               NULL);
        set_preprocessor_echo_delay(in->preprocessors[i], delay_us);
    }

    in->ref_buf_frames -= buf.frameCount;
    if (in->ref_buf_frames) {
        memcpy(in->ref_buf,
               in->ref_buf + buf.frameCount * in->config.channels,
               in->ref_buf_frames * in->config.channels * sizeof(int16_t));
    }
}

static int read_pcm_mic(struct gen3_stream_in *in, void *read_buf, size_t bytes)
{
    size_t frame_size = audio_stream_in_frame_size(&in->stream);
    size_t pcm_frame_size = frame_size * pcm_config_mic.channels / popcount(in->main_channels);
    size_t in_frames = bytes / frame_size;
    int read_status = pcm_read(in->pcm, in->upmixed_read_buffer, in_frames * pcm_frame_size);

    if (read_status == 0) {
        size_t offset = 0;
        size_t read_offset = 0;
        for (size_t counter = 0; counter < in_frames; counter++)
        {
            memmove((char *)read_buf + offset, in->upmixed_read_buffer + read_offset, frame_size);
            offset += frame_size;
            read_offset += pcm_frame_size;
        }
    }

    return read_status;
}

static int get_next_buffer(struct resampler_buffer_provider *buffer_provider,
                                   struct resampler_buffer* buffer)
{
    struct gen3_stream_in *in;

    if (buffer_provider == NULL || buffer == NULL)
        return -EINVAL;

    in = (struct gen3_stream_in *)((char *)buffer_provider -
                                   offsetof(struct gen3_stream_in, buf_provider));

    if (in->pcm == NULL) {
        buffer->raw = NULL;
        buffer->frame_count = 0;
        in->read_status = -ENODEV;
        return -ENODEV;
    }

    if (in->read_buf_frames == 0) {
        size_t size_in_bytes = pcm_frames_to_bytes(in->pcm, in->config.period_size);
        if (in->read_buf_size < in->config.period_size) {
            in->read_buf_size = in->config.period_size;
            in->read_buf = (int16_t *) realloc(in->read_buf, size_in_bytes);
            ALOG_ASSERT((in->read_buf != NULL),
                        "get_next_buffer() failed to reallocate read_buf");
            ALOGV("get_next_buffer(): read_buf %p extended to %zu bytes",
                  in->read_buf, size_in_bytes);
        }

        in->read_status = in->read_pcm_locked(in, (void*)in->read_buf, size_in_bytes);

        if (in->read_status != 0) {
            ALOGE("get_next_buffer() pcm_read error %d", in->read_status);
            buffer->raw = NULL;
            buffer->frame_count = 0;
            return in->read_status;
        }
        in->read_buf_frames = in->config.period_size;
    }

    buffer->frame_count = (buffer->frame_count > in->read_buf_frames) ?
                                in->read_buf_frames : buffer->frame_count;
    buffer->i16 = in->read_buf + (in->config.period_size - in->read_buf_frames) *
                                                in->config.channels;

    return in->read_status;

}

static void release_buffer(struct resampler_buffer_provider *buffer_provider,
                                  struct resampler_buffer* buffer)
{
    struct gen3_stream_in *in;

    if (buffer_provider == NULL || buffer == NULL)
        return;

    in = (struct gen3_stream_in *)((char *)buffer_provider -
                                   offsetof(struct gen3_stream_in, buf_provider));

    in->read_buf_frames -= buffer->frame_count;
}

/* read_frames() reads frames from kernel driver, down samples to capture rate
 * if necessary and output the number of frames requested to the buffer specified */
static ssize_t read_frames(struct gen3_stream_in *in, void *buffer, ssize_t frames)
{
    ssize_t frames_wr = 0;

    while (frames_wr < frames) {
        size_t frames_rd = frames - frames_wr;
        if (in->resampler != NULL) {
            in->resampler->resample_from_provider(in->resampler,
                                                  (int16_t *)((char *)buffer +
                                                      pcm_frames_to_bytes(in->pcm ,frames_wr)),
                                                  &frames_rd);
        } else {
            struct resampler_buffer buf = {
                { .raw = NULL, },
                .frame_count = frames_rd,
            };
            get_next_buffer(&in->buf_provider, &buf);
            if (buf.raw != NULL) {
                memcpy((char *)buffer +
                            pcm_frames_to_bytes(in->pcm, frames_wr),
                        buf.raw,
                        pcm_frames_to_bytes(in->pcm, buf.frame_count));
                frames_rd = buf.frame_count;
            }
            release_buffer(&in->buf_provider, &buf);
        }
        /* in->read_status is updated by getNextBuffer() also called by
         * in->resampler->resample_from_provider() */
        if (in->read_status != 0)
            return in->read_status;

        frames_wr += frames_rd;
    }
    return frames_wr;
}

/* process_frames() reads frames from kernel driver (via read_frames()),
 * calls the active audio pre processings and output the number of frames requested
 * to the buffer specified */
static ssize_t process_frames(struct gen3_stream_in *in, void* buffer, ssize_t frames)
{
    ssize_t frames_wr = 0;
    audio_buffer_t in_buf;
    audio_buffer_t out_buf;
    int i;
    bool has_aux_channels = (~in->main_channels & in->aux_channels);
    void *proc_buf_out;

    if (has_aux_channels)
        proc_buf_out = in->proc_buf_out;
    else
        proc_buf_out = buffer;

    /* since all the processing below is done in frames and using the config.channels
     * as the number of channels, no changes is required in case aux_channels are present */
    while (frames_wr < frames) {
        /* first reload enough frames at the end of process input buffer */
        if (in->proc_buf_frames < (size_t)frames) {
            ssize_t frames_rd;

            if (in->proc_buf_size < (size_t)frames) {
                size_t size_in_bytes = pcm_frames_to_bytes(in->pcm, frames);

                in->proc_buf_size = (size_t)frames;
                in->proc_buf_in = (int16_t *)realloc(in->proc_buf_in, size_in_bytes);
                ALOG_ASSERT((in->proc_buf_in != NULL),
                            "process_frames() failed to reallocate proc_buf_in");
                if (has_aux_channels) {
                    in->proc_buf_out = (int16_t *)realloc(in->proc_buf_out, size_in_bytes);
                    ALOG_ASSERT((in->proc_buf_out != NULL),
                                "process_frames() failed to reallocate proc_buf_out");
                    proc_buf_out = in->proc_buf_out;
                }
                ALOGV("process_frames(): proc_buf_in %p extended to %zu bytes",
                     in->proc_buf_in, size_in_bytes);
            }
            frames_rd = read_frames(in,
                                    in->proc_buf_in +
                                        in->proc_buf_frames * in->config.channels,
                                    frames - in->proc_buf_frames);
            if (frames_rd < 0) {
                frames_wr = frames_rd;
                break;
            }
            in->proc_buf_frames += frames_rd;
        }

        if (in->echo_reference != NULL)
            push_echo_reference(in, in->proc_buf_frames);

         /* in_buf.frameCount and out_buf.frameCount indicate respectively
          * the maximum number of frames to be consumed and produced by process() */
        in_buf.frameCount = in->proc_buf_frames;
        in_buf.s16 = in->proc_buf_in;
        out_buf.frameCount = frames - frames_wr;
        out_buf.s16 = (int16_t *)proc_buf_out + frames_wr * in->config.channels;

        /* FIXME: this works because of current pre processing library implementation that
         * does the actual process only when the last enabled effect process is called.
         * The generic solution is to have an output buffer for each effect and pass it as
         * input to the next.
         */
        for (i = 0; i < in->num_preprocessors; i++) {
            (*in->preprocessors[i])->process(in->preprocessors[i],
                                               &in_buf,
                                               &out_buf);
        }

        /* process() has updated the number of frames consumed and produced in
         * in_buf.frameCount and out_buf.frameCount respectively
         * move remaining frames to the beginning of in->proc_buf_in */
        in->proc_buf_frames -= in_buf.frameCount;

        if (in->proc_buf_frames) {
            memcpy(in->proc_buf_in,
                   in->proc_buf_in + in_buf.frameCount * in->config.channels,
                   in->proc_buf_frames * in->config.channels * sizeof(int16_t));
        }

        /* if not enough frames were passed to process(), read more and retry. */
        if (out_buf.frameCount == 0) {
            ALOGW("No frames produced by preproc");
            continue;
        }

        if ((frames_wr + (ssize_t)out_buf.frameCount) <= frames) {
            frames_wr += out_buf.frameCount;
        } else {
            /* The effect does not comply to the API. In theory, we should never end up here! */
            ALOGE("preprocessing produced too many frames: %zd + %zu  > %zd !",
                  frames_wr, out_buf.frameCount, frames);
            frames_wr = frames;
        }
    }

    /* Remove aux_channels that have been added on top of main_channels
     * Assumption is made that the channels are interleaved and that the main
     * channels are first. */
    if (has_aux_channels)
    {
        size_t src_channels = in->config.channels;
        size_t dst_channels = popcount(in->main_channels);
        int16_t* src_buffer = (int16_t *)proc_buf_out;
        int16_t* dst_buffer = (int16_t *)buffer;

        if (dst_channels == 1) {
            for (i = frames_wr; i > 0; i--)
            {
                *dst_buffer++ = *src_buffer;
                src_buffer += src_channels;
            }
        } else {
            for (i = frames_wr; i > 0; i--)
            {
                memcpy(dst_buffer, src_buffer, dst_channels*sizeof(int16_t));
                dst_buffer += dst_channels;
                src_buffer += src_channels;
            }
        }
    }

    return frames_wr;
}

static ssize_t in_read_fm(struct audio_stream_in *stream, void* buffer,
                       size_t bytes)
{
    int ret = 0;
    struct gen3_stream_in *in = (struct gen3_stream_in *)stream;

    /* acquiring hw device mutex systematically is useful if a low priority thread is waiting
     * on the input stream mutex - e.g. executing select_mode() while holding the hw device
     * mutex
     */
    PTHREAD_MUTEX_LOCK(&in->lock);
    if (in->standby) {
        ret = in->start_locked(in);
        if (ret == 0)
            in->standby = 0;
    }

    if (ret < 0) {
        goto exit;
    }

    ret = in->read_pcm_locked(in, buffer, bytes);

    if (ret > 0)
        ret = 0;

exit:
    if (ret < 0) {
        usleep(bytes * 1000000 / audio_stream_in_frame_size(stream) /
               in_get_sample_rate(&stream->common));
    }

    PTHREAD_MUTEX_UNLOCK(&in->lock);
    return bytes;
}

int read_pcm_fm(struct gen3_stream_in *in, void *read_buf, size_t bytes)
{
    return pcm_read(in->pcm, read_buf, bytes);
}

static ssize_t in_read_mic(struct audio_stream_in *stream, void* buffer,
                       size_t bytes)
{
    int ret = 0;
    struct gen3_stream_in *in = (struct gen3_stream_in *)stream;
    struct gen3_audio_device *adev = in->dev;

    /* acquiring hw device mutex systematically is useful if a low priority thread is waiting
     * on the input stream mutex - e.g. executing select_mode() while holding the hw device
     * mutex
     */
    PTHREAD_MUTEX_LOCK(&in->lock);
    if (in->standby) {
        ret = in->start_locked(in);
        if (ret == 0)
            in->standby = 0;
    }

    if (ret < 0)
        goto exit;

    if (in->num_preprocessors != 0) {
        size_t frames_rq = bytes / audio_stream_in_frame_size(stream);
        ret = process_frames(in, buffer, frames_rq);
    } else if (in->resampler != NULL) {
        size_t frames_req = bytes / audio_stream_in_frame_size(stream);
        size_t frames_read = in->config.period_size * in->config.rate / in->requested_rate;
        size_t bytes_read = bytes * in->config.rate / in->requested_rate;
        if (in->read_buf_size < frames_read) {
            in->read_buf_size = frames_read;
            in->read_buf = (int16_t *)realloc(in->read_buf, bytes_read);
        }
        ret = in->read_pcm_locked(in, in->read_buf, bytes_read);
        in->resampler->resample_from_input(in->resampler,
                in->read_buf, &frames_read, (int16_t *)buffer, &frames_req);

    } else {
        ret = in->read_pcm_locked(in, buffer, bytes);
    }

    if (ret > 0)
        ret = 0;

    if (ret == 0 && adev->mic_mute)
        memset(buffer, 0, bytes);

exit:
    if (ret < 0)
        usleep(bytes * 1000000 / audio_stream_in_frame_size(stream) /
               in_get_sample_rate(&stream->common));

    PTHREAD_MUTEX_UNLOCK(&in->lock);
    return bytes;
}

static ssize_t in_read_hfp(struct audio_stream_in *stream, void* buffer,
                       size_t bytes)
{
    ALOGV("%s", __FUNCTION__);
    int ret = 0;
    struct gen3_stream_in *in = (struct gen3_stream_in *)stream;
    struct gen3_audio_device *adev = in->dev;

    /* acquiring hw device mutex systematically is useful if a low priority thread is waiting
     * on the input stream mutex - e.g. executing select_mode() while holding the hw device
     * mutex
     */
    PTHREAD_MUTEX_LOCK(&in->lock);
    if (in->standby) {
        ret = in->start_locked(in);
        if (ret == 0)
            in->standby = 0;
    }

    if (ret < 0)
        goto exit;

    if (in->resampler != NULL) {
        size_t frames_req = in->config.period_size;
        size_t out_frames = in->config.period_size * (DEFAULT_IN_SAMPLING_RATE / in->config.rate);
        size_t size_in_bytes = pcm_frames_to_bytes(in->pcm, frames_req);
        if (in->read_buf_size < frames_req) {
            in->read_buf_size = frames_req;
            in->read_buf = (int16_t *)realloc(in->read_buf, size_in_bytes);
        }
        ret = in->read_pcm_locked(in, in->read_buf, size_in_bytes);
        in->resampler->resample_from_input(in->resampler,
                in->read_buf, &frames_req, (int16_t *)buffer, &out_frames);
    } else {
        ret = in->read_pcm_locked(in, buffer, bytes);
    }

    if (ret > 0)
        ret = 0;

    if (ret == 0 && adev->mic_mute)
        memset(buffer, 0, bytes);

exit:
    if (ret < 0)
        usleep(bytes * 1000000 / audio_stream_in_frame_size(stream) /
               in_get_sample_rate(&stream->common));

    PTHREAD_MUTEX_UNLOCK(&in->lock);
    return bytes;
}

int read_pcm_hfp(struct gen3_stream_in *in, void *read_buf, size_t bytes)
{
    int ret = pcm_read(in->pcm, read_buf, bytes);
    size_t frame_size = audio_stream_in_frame_size(&in->stream);
    size_t frames = bytes / frame_size;
    for (size_t counter = 0; counter < frames; counter++) {
        size_t offset = frame_size * counter;
        memcpy((char*)read_buf + offset, (char*)read_buf + offset + frame_size / 2, frame_size / 2);
    }
    return ret;
}


static uint32_t in_get_input_frames_lost(struct audio_stream_in * /*stream*/)
{
    ALOGV("%s", __FUNCTION__);
    return 0;
}

static int in_get_capture_position(const struct audio_stream_in * /*stream*/,
                                   int64_t * /*frames*/, int64_t * /*time*/)
{
    ALOGV("%s", __FUNCTION__);
    return 0;
}

static int in_start(const struct audio_stream_in* /*stream*/)
{
    ALOGV("%s", __FUNCTION__);
    return 0;
}

static int in_stop(const struct audio_stream_in* /*stream*/)
{
    ALOGV("%s", __FUNCTION__);
    return 0;
}

static int in_create_mmap_buffer(const struct audio_stream_in * /*stream*/,
               int32_t /*min_size_frames*/, struct audio_mmap_buffer_info * /*info*/)
{
    ALOGV("%s", __FUNCTION__);
    return 0;
}

static int in_get_mmap_position(const struct audio_stream_in * /*stream*/,
                                struct audio_mmap_position * /*position*/)
{
    ALOGV("%s", __FUNCTION__);
    return 0;
}


#define GET_COMMAND_STATUS(status, fct_status, cmd_status) \
            do {                                           \
                if (fct_status != 0)                       \
                    status = fct_status;                   \
                else if (cmd_status != 0)                  \
                    status = cmd_status;                   \
            } while(0)

static int in_configure_reverse(struct gen3_stream_in *in)
{
    int32_t cmd_status;
    uint32_t size = sizeof(int);
    effect_config_t config;
    int32_t status = 0;
    int32_t fct_status = 0;
    int i;

    if (in->num_preprocessors > 0) {
        config.inputCfg.channels = in->main_channels;
        config.outputCfg.channels = in->main_channels;
        config.inputCfg.format = AUDIO_FORMAT_PCM_16_BIT;
        config.outputCfg.format = AUDIO_FORMAT_PCM_16_BIT;
        config.inputCfg.samplingRate = in->requested_rate;
        config.outputCfg.samplingRate = in->requested_rate;
        config.inputCfg.mask =
                ( EFFECT_CONFIG_SMP_RATE | EFFECT_CONFIG_CHANNELS | EFFECT_CONFIG_FORMAT );
        config.outputCfg.mask =
                ( EFFECT_CONFIG_SMP_RATE | EFFECT_CONFIG_CHANNELS | EFFECT_CONFIG_FORMAT );

        for (i = 0; i < in->num_preprocessors; i++)
        {
            if ((*in->preprocessors[i])->process_reverse == NULL)
                continue;
            fct_status = (*(in->preprocessors[i]))->command(
                                                        in->preprocessors[i],
                                                        EFFECT_CMD_SET_CONFIG_REVERSE,
                                                        sizeof(effect_config_t),
                                                        &config,
                                                        &size,
                                                        &cmd_status);
            GET_COMMAND_STATUS(status, fct_status, cmd_status);
        }
    }
    return status;
}

static int in_add_audio_effect_fm(const struct audio_stream * /*stream*/,
                               effect_handle_t /*effect*/)
{
    ALOGV("%s", __FUNCTION__);
    return -ENOSYS;
}

static int in_add_audio_effect_hfp(const struct audio_stream * /*stream*/,
                               effect_handle_t /*effect*/)
{
    ALOGV("%s", __FUNCTION__);
    return -ENOSYS;
}

static int in_add_audio_effect_mic(const struct audio_stream *stream,
                               effect_handle_t effect)
{
    struct gen3_stream_in *in = (struct gen3_stream_in *)stream;
    int status;
    effect_descriptor_t desc;

    PTHREAD_MUTEX_LOCK(&in->dev->lock);
    PTHREAD_MUTEX_LOCK(&in->lock);
    if (in->num_preprocessors >= MAX_PREPROCESSORS) {
        status = -ENOSYS;
        goto exit;
    }

    status = (*effect)->get_descriptor(effect, &desc);
    if (status != 0)
        goto exit;

    in->preprocessors[in->num_preprocessors] = effect;
    in->num_preprocessors++;

    ALOGV("in_add_audio_effect(), effect type: %08x", desc.type.timeLow);

    if (memcmp(&desc.type, FX_IID_AEC, sizeof(effect_uuid_t)) == 0) {
        in->need_echo_reference = true;
        in->standby_locked(in);
        in_configure_reverse(in);
    }

exit:

    ALOGW_IF(status != 0, "in_add_audio_effect() error %d", status);
    PTHREAD_MUTEX_UNLOCK(&in->lock);
    PTHREAD_MUTEX_UNLOCK(&in->dev->lock);
    return status;
}

static int in_remove_audio_effect_fm(const struct audio_stream * /*stream*/,
                               effect_handle_t /*effect*/)
{
    ALOGV("%s", __FUNCTION__);
    return -ENOSYS;
}

static int in_remove_audio_effect_hfp(const struct audio_stream * /*stream*/,
                               effect_handle_t /*effect*/)
{
    ALOGV("%s", __FUNCTION__);
    return -ENOSYS;
}

static int in_remove_audio_effect_mic(const struct audio_stream *stream,
                                  effect_handle_t effect)
{
    struct gen3_stream_in *in = (struct gen3_stream_in *)stream;
    int i;
    int status = -EINVAL;
    effect_descriptor_t desc;

    PTHREAD_MUTEX_LOCK(&in->dev->lock);
    PTHREAD_MUTEX_LOCK(&in->lock);
    if (in->num_preprocessors <= 0) {
        status = -ENOSYS;
        goto exit;
    }

    for (i = 0; i < in->num_preprocessors; i++) {
        if (status == 0) { /* status == 0 means an effect was removed from a previous slot */
            in->preprocessors[i - 1] = in->preprocessors[i];
            ALOGV("in_remove_audio_effect moving fx from %d to %d", i, i - 1);
            continue;
        }
        if (in->preprocessors[i] == effect) {
            ALOGV("in_remove_audio_effect found fx at index %d", i);
            status = 0;
        }
    }

    if (status != 0)
        goto exit;

    in->num_preprocessors--;
    /* if we remove one effect, at least the last preproc should be reset */
    in->preprocessors[in->num_preprocessors] = NULL;

    status = (*effect)->get_descriptor(effect, &desc);
    if (status != 0)
        goto exit;

    ALOGV("in_remove_audio_effect(), effect type: %08x", desc.type.timeLow);

    if (memcmp(&desc.type, FX_IID_AEC, sizeof(effect_uuid_t)) == 0) {
        in->need_echo_reference = false;
        in->standby_locked(in);
    }

exit:

    ALOGW_IF(status != 0, "in_remove_audio_effect() error %d", status);
    PTHREAD_MUTEX_UNLOCK(&in->lock);
    PTHREAD_MUTEX_UNLOCK(&in->dev->lock);
    return status;
}

static audio_devices_t in_get_device(const struct audio_stream *stream)
{
    ALOGV("%s", __FUNCTION__);
    struct gen3_stream_in *in = (struct gen3_stream_in *)stream;
    return in->device;
}

static int in_set_device(struct audio_stream * /*stream*/, audio_devices_t /*device*/)
{
    ALOGV("%s", __FUNCTION__);
    return -ENOSYS;
}

static int adev_open_output_stream_multichannel(struct audio_hw_device *dev,
                              audio_io_handle_t /*handle*/,
                              audio_devices_t devices,
                              audio_output_flags_t flags,
                              struct audio_config *config,
                              struct audio_stream_out **stream_out,
                              const char * /*address*/)
{
    struct gen3_audio_device *ladev = (struct gen3_audio_device *)dev;
    struct gen3_stream_out *out;
    int ret;
    int channel_count = popcount(config->channel_mask);

    ALOGV("%s: channels=%d, devices = 0x%08x, flags = 0x%x",
        __FUNCTION__, channel_count, devices, flags);

    *stream_out = NULL;

    out = (struct gen3_stream_out *)calloc(1, sizeof(struct gen3_stream_out));
    if (!out)
        return -ENOMEM;

    out->channel_mask = AUDIO_CHANNEL_OUT_STEREO | AUDIO_CHANNEL_OUT_7POINT1;

    ALOGV("adev_open_output_stream() normal buffer");

    out->stream.common.get_buffer_size = out_get_buffer_size;
    out->stream.common.get_sample_rate = out_get_sample_rate;
    out->stream.get_latency = out_get_latency;
    out->stream.write = out_write;

    ret = create_resampler(DEFAULT_OUT_SAMPLING_RATE,
                           DEFAULT_OUT_SAMPLING_RATE,
                           2,
                           RESAMPLER_QUALITY_DEFAULT,
                           NULL,
                           &out->resampler);
    if (ret != 0) {
        ALOGV("adev_open_output_stream() create_resampler failed, err = %d", ret);
        goto err_open;
    }

    out->stream.common.set_sample_rate = out_set_sample_rate;
    out->stream.common.get_channels = out_get_channels;
    out->stream.common.get_format = out_get_format;
    out->stream.common.set_format = out_set_format;
    out->stream.common.standby = out_standby;
    out->stream.common.dump = out_dump;
    out->stream.common.set_parameters = out_set_parameters;
    out->stream.common.get_parameters = out_get_parameters;
    out->stream.common.add_audio_effect = out_add_audio_effect;
    out->stream.common.remove_audio_effect = out_remove_audio_effect;
    out->stream.common.set_device = out_set_device;
    out->stream.common.get_device = out_get_device;
    out->stream.set_volume = out_set_volume;
    out->stream.get_render_position = out_get_render_position;
    out->stream.get_next_write_timestamp = out_get_next_write_timestamp;
    out->stream.set_callback = out_set_callback;
    out->stream.pause = NULL; //out_pause;
    out->stream.resume = NULL; //out_resume;
    out->stream.drain = out_drain;
    out->stream.flush = out_flush;
    out->stream.start = out_start;
    out->stream.stop = out_stop;
    out->stream.get_presentation_position = out_get_presentation_position;
    out->stream.create_mmap_buffer = out_create_mmap_buffer;
    out->stream.get_mmap_position = out_get_mmap_position;

    out->start_locked = start_output_stream;

    out->dev = ladev;
    out->standby = 1;
    out->written = 0;

    /* FIXME: when we support multiple output devices, we will want to
     * do the following:
     * adev->devices &= ~AUDIO_DEVICE_OUT_ALL;
     * adev->devices |= out->device;
     * select_output_device(adev);
     * This is because out_set_parameters() with a route is not
     * guaranteed to be called after an output stream is opened. */

    out->stream.common.set_format(&out->stream.common, config->format);
    out->channel_mask = config->channel_mask;
    out->stream.common.set_sample_rate(&out->stream.common, config->sample_rate);

    *stream_out = &out->stream;
    ladev->outputs[OUTPUT_MULTICHANNEL] = out;

    return 0;

err_open:
    free(out);
    return ret;
}

static int adev_open_output_stream_stereo(struct audio_hw_device *dev,
                              audio_io_handle_t /*handle*/,
                              audio_devices_t devices,
                              audio_output_flags_t flags,
                              struct audio_config *config,
                              struct audio_stream_out **stream_out,
                              const char * /*address*/)
{
    struct gen3_audio_device *ladev = (struct gen3_audio_device *)dev;
    struct gen3_stream_out *out;
    int ret;
    int output_type = OUTPUT_STEREO;
    int channel_count = popcount(config->channel_mask);

    ALOGV("%s: channels=%d, devices = 0x%08x, flags = 0x%x",
        __FUNCTION__, channel_count, devices, flags);

    *stream_out = NULL;

    out = (struct gen3_stream_out *)calloc(1, sizeof(struct gen3_stream_out));
    if (!out)
        return -ENOMEM;

    out->channel_mask = AUDIO_CHANNEL_OUT_STEREO;

    ALOGV("adev_open_output_stream() normal buffer");

    out->stream.common.get_buffer_size = out_get_buffer_size_stereo;
    out->stream.common.get_sample_rate = out_get_sample_rate;
    out->stream.get_latency = out_get_latency;
    out->stream.write = out_write_stereo;

    ret = create_resampler(DEFAULT_OUT_SAMPLING_RATE,
                           DEFAULT_OUT_SAMPLING_RATE,
                           2,
                           RESAMPLER_QUALITY_DEFAULT,
                           NULL,
                           &out->resampler);
    if (ret != 0) {
        ALOGV("adev_open_output_stream() create_resampler failed, err = %d", ret);
        goto err_open;
    }

    out->stream.common.set_sample_rate = out_set_sample_rate;
    out->stream.common.get_channels = out_get_channels;
    out->stream.common.get_format = out_get_format;
    out->stream.common.set_format = out_set_format;
    out->stream.common.standby = out_standby;
    out->stream.common.dump = out_dump;
    out->stream.common.set_parameters = out_set_parameters;
    out->stream.common.get_parameters = out_get_parameters;
    out->stream.common.add_audio_effect = out_add_audio_effect;
    out->stream.common.remove_audio_effect = out_remove_audio_effect;
    out->stream.common.set_device = out_set_device;
    out->stream.common.get_device = out_get_device;
    out->stream.set_volume = out_set_volume;
    out->stream.get_render_position = out_get_render_position;
    out->stream.get_next_write_timestamp = out_get_next_write_timestamp;
    out->stream.set_callback = out_set_callback;
    out->stream.pause = NULL; //out_pause;
    out->stream.resume = NULL; //out_resume;
    out->stream.drain = out_drain;
    out->stream.flush = out_flush;
    out->stream.start = out_start;
    out->stream.stop = out_stop;
    out->stream.get_presentation_position = out_get_presentation_position;
    out->stream.create_mmap_buffer = out_create_mmap_buffer;
    out->stream.get_mmap_position = out_get_mmap_position;

    out->start_locked = start_output_stream_stereo;

    out->dev = ladev;
    out->standby = 1;
    out->written = 0;

    /* FIXME: when we support multiple output devices, we will want to
     * do the following:
     * adev->devices &= ~AUDIO_DEVICE_OUT_ALL;
     * adev->devices |= out->device;
     * select_output_device(adev);
     * This is because out_set_parameters() with a route is not
     * guaranteed to be called after an output stream is opened. */

    out->stream.common.set_format(&out->stream.common, config->format);
    out->channel_mask = config->channel_mask;
    out->stream.common.set_sample_rate(&out->stream.common, config->sample_rate);

    *stream_out = &out->stream;
    ladev->outputs[output_type] = out;

    return 0;

err_open:
    free(out);
    return ret;
}

static int adev_open_output_stream_hfp(struct audio_hw_device *dev,
                              audio_io_handle_t /*handle*/,
                              audio_devices_t devices,
                              audio_output_flags_t flags,
                              struct audio_config *config,
                              struct audio_stream_out **stream_out,
                              const char * /*address*/)
{
    struct gen3_audio_device *ladev = (struct gen3_audio_device *)dev;
    struct gen3_stream_out *out;
    int ret;
    int output_type = OUTPUT_BT_SCO;
    int channel_count = popcount(config->channel_mask);

    ALOGV("%s: channels=%d, devices = 0x%08x, flags = 0x%x",
        __FUNCTION__, channel_count, devices, flags);

    *stream_out = NULL;

    out = (struct gen3_stream_out *)calloc(1, sizeof(struct gen3_stream_out));
    if (!out)
        return -ENOMEM;

    out->channel_mask = AUDIO_CHANNEL_OUT_STEREO;

    memcpy(&out->config[PCM_NORMAL], &pcm_config_hfp_out, sizeof(pcm_config_hfp_out));

    out->stream.common.get_buffer_size = out_get_buffer_size_hfp;
    out->stream.common.get_sample_rate = out_get_sample_rate;
    out->stream.get_latency = out_get_latency;
    out->stream.write = out_write_hfp;

    ret = create_resampler(config->sample_rate,
                           out->config[PCM_NORMAL].rate,
                           2,
                           RESAMPLER_QUALITY_DEFAULT,
                           NULL,
                           &out->resampler);
    if (ret != 0) {
        ALOGV("adev_open_output_stream() create_resampler failed, err = %d", ret);
        goto err_open;
    }

    out->stream.common.set_sample_rate = out_set_sample_rate;
    out->stream.common.get_channels = out_get_channels;
    out->stream.common.get_format = out_get_format;
    out->stream.common.set_format = out_set_format;
    out->stream.common.standby = out_standby;
    out->stream.common.dump = out_dump;
    out->stream.common.set_parameters = out_set_parameters_hfp;
    out->stream.common.get_parameters = out_get_parameters;
    out->stream.common.add_audio_effect = out_add_audio_effect;
    out->stream.common.remove_audio_effect = out_remove_audio_effect;

    out->stream.common.set_device = out_set_device;
    out->stream.common.get_device = out_get_device;
    out->stream.set_volume = out_set_volume;
    out->stream.get_render_position = out_get_render_position;
    out->stream.get_next_write_timestamp = out_get_next_write_timestamp;

    out->stream.set_callback = out_set_callback;
    out->stream.pause = NULL; //out_pause;
    out->stream.resume = NULL; //out_resume;
    out->stream.drain = out_drain;
    out->stream.flush = out_flush;
    out->stream.start = out_start;
    out->stream.stop = out_stop;

    out->stream.get_presentation_position = out_get_presentation_position;
    out->stream.create_mmap_buffer = out_create_mmap_buffer;
    out->stream.get_mmap_position = out_get_mmap_position;

    out->start_locked = start_output_stream_hfp;

    out->dev = ladev;
    out->standby = 1;

    out->stream.common.set_format(&out->stream.common, config->format);
    out->channel_mask = config->channel_mask;
    out->stream.common.set_sample_rate(&out->stream.common, config->sample_rate);

    *stream_out = &out->stream;
    ladev->outputs[output_type] = out;

    return 0;

err_open:
    free(out);
    return ret;
}

static int adev_open_output_stream(struct audio_hw_device *dev,
                              audio_io_handle_t handle,
                              audio_devices_t devices,
                              audio_output_flags_t flags,
                              struct audio_config *config,
                              struct audio_stream_out **stream_out,
                              const char *address)
{
    int ret_value = 0;
    struct gen3_audio_device *ladev = (struct gen3_audio_device *)dev;
    PTHREAD_MUTEX_LOCK(&ladev->lock);

    if ((devices & AUDIO_DEVICE_OUT_BLUETOOTH_SCO) == AUDIO_DEVICE_OUT_BLUETOOTH_SCO) {
        ret_value = adev_open_output_stream_hfp(dev, handle,
                devices, flags, config, stream_out, address);
    } else if (config->channel_mask == AUDIO_CHANNEL_OUT_STEREO) {
        ret_value = adev_open_output_stream_stereo(dev, handle,
                devices, flags, config, stream_out, address);
    } else {
        ret_value = adev_open_output_stream_multichannel(dev, handle,
                devices, flags, config, stream_out, address);
    }

    PTHREAD_MUTEX_UNLOCK(&ladev->lock);
    return ret_value;
}

static void adev_close_output_stream(struct audio_hw_device *dev,
                                     struct audio_stream_out *stream)
{
    struct gen3_audio_device *ladev = (struct gen3_audio_device *)dev;
    struct gen3_stream_out *out = (struct gen3_stream_out *)stream;
    int i;

    ALOGV("%s",__FUNCTION__);

    PTHREAD_MUTEX_LOCK(&ladev->lock);
    out_standby(&stream->common);
    for (i = 0; i < OUTPUT_TOTAL; i++) {
        if (ladev->outputs[i] == out) {
            ladev->outputs[i] = NULL;
            break;
        }
    }
    PTHREAD_MUTEX_UNLOCK(&ladev->lock);

    if (out->buffer)
        free(out->buffer);
    if (out->upmixed_buffer)
        free(out->upmixed_buffer);
    if (out->resampler)
        release_resampler(out->resampler);
    free(stream);
}

static int adev_set_parameters(struct audio_hw_device * dev, const char *kvpairs)
{
    ALOGV("%s: kvpairs=%s", __FUNCTION__, kvpairs);

    struct gen3_audio_device *adev = (gen3_audio_device*)dev;
    struct str_parms *parms;
    char value[32];
    unsigned int val = 0;

    parms = str_parms_create_str(kvpairs);
    memset(value, 0x0, sizeof(value));

    if (str_parms_get_str(parms, AUDIO_PARAMETER_DEVICE_CONNECT, value, sizeof(value)) >= 0) {
        val = atoi(value);
        if (val == AUDIO_DEVICE_IN_FM_TUNER) {
            if (adev->fm_input == NULL)
            {
                struct audio_stream_in *stream_in;
                struct audio_config config = {
                        DEFAULT_IN_SAMPLING_RATE, AUDIO_CHANNEL_IN_STEREO, AUDIO_FORMAT_PCM_16_BIT, {}, CAPTURE_PERIOD_SIZE};
                int res = adev->hw_device.open_input_stream(&adev->hw_device, 0,
                        AUDIO_DEVICE_IN_FM_TUNER, &config, &stream_in, AUDIO_INPUT_FLAG_NONE, "",
                        AUDIO_SOURCE_FM_TUNER);
                if (res == 0) {
                    PTHREAD_MUTEX_LOCK(&adev->lock);
                    adev->fm_input = (struct gen3_stream_in*)stream_in;
                    adev->connected_in_device = AUDIO_DEVICE_IN_FM_TUNER;
                    PTHREAD_MUTEX_UNLOCK(&adev->lock);
                }
            }
        }
    } else if (str_parms_get_str(parms, AUDIO_PARAMETER_DEVICE_DISCONNECT, value, sizeof(value)) >= 0) {
        val = atoi(value);
        if (val == AUDIO_DEVICE_IN_FM_TUNER) {
            PTHREAD_MUTEX_LOCK(&adev->lock);
            struct gen3_stream_in *fm = adev->fm_input;
            adev->connected_in_device = 0;
            adev->fm_input = NULL;
            PTHREAD_MUTEX_UNLOCK(&adev->lock);
            adev->hw_device.close_input_stream(&adev->hw_device, &fm->stream);
        }
    } else if (str_parms_get_str(parms, "hfp_enable", value, sizeof(value)) >= 0) {
        if (strcmp(value, "true") == 0) {
            if (adev->hfp_input == NULL)
            {
                struct audio_stream_in *stream_in;
                struct audio_config config = {
                        DEFAULT_IN_SAMPLING_RATE, AUDIO_CHANNEL_IN_STEREO, AUDIO_FORMAT_PCM_16_BIT, {}, CAPTURE_PERIOD_SIZE};
                int res = adev->hw_device.open_input_stream(&adev->hw_device, 0,
                        AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET, &config, &stream_in, AUDIO_INPUT_FLAG_NONE, "",
                        AUDIO_SOURCE_VOICE_CALL);
                if (res == 0) {
                    PTHREAD_MUTEX_LOCK(&adev->lock);
                    adev->hfp_input = (struct gen3_stream_in*)stream_in;
                    adev->connected_in_device |= AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET;
                    PTHREAD_MUTEX_UNLOCK(&adev->lock);
                }
            }
            if (adev->mic_input == NULL)
            {
                struct audio_stream_in *stream_in;
                struct audio_config config = {
                        DEFAULT_IN_SAMPLING_RATE, AUDIO_CHANNEL_IN_STEREO, AUDIO_FORMAT_PCM_16_BIT, {}, CAPTURE_PERIOD_SIZE};
                int res = adev->hw_device.open_input_stream(&adev->hw_device, 0,
                        AUDIO_DEVICE_IN_BUILTIN_MIC, &config, &stream_in, AUDIO_INPUT_FLAG_NONE, "",
                        AUDIO_SOURCE_VOICE_CALL);
                if (res == 0) {
                    PTHREAD_MUTEX_LOCK(&adev->lock);
                    adev->mic_input = (struct gen3_stream_in*)stream_in;
                    adev->connected_in_device |= AUDIO_DEVICE_IN_BUILTIN_MIC;
                    PTHREAD_MUTEX_UNLOCK(&adev->lock);
                }
            }
            if (adev->outputs[OUTPUT_BT_SCO] == NULL) {
                struct audio_stream_out *stream_out;
                struct audio_config config = {
                        DEFAULT_OUT_SAMPLING_RATE, AUDIO_CHANNEL_OUT_STEREO, AUDIO_FORMAT_PCM_16_BIT, {}, PLAY_PERIOD_SIZE};
                int res = adev->hw_device.open_output_stream(&adev->hw_device, 0,
                        AUDIO_DEVICE_OUT_BLUETOOTH_SCO, AUDIO_OUTPUT_FLAG_NONE, &config, &stream_out, "");
                if (res == 0) {
                    PTHREAD_MUTEX_LOCK(&adev->lock);
                    adev->outputs[OUTPUT_BT_SCO] = (struct gen3_stream_out*)stream_out;
                    PTHREAD_MUTEX_UNLOCK(&adev->lock);
                }
            }
        } else if (strcmp(value, "false") == 0) {
            PTHREAD_MUTEX_LOCK(&adev->lock);
            struct gen3_stream_in *hfp_in = adev->hfp_input;
            struct gen3_stream_in *mic = adev->mic_input;
            struct gen3_stream_out *hfp_out = adev->outputs[OUTPUT_BT_SCO];
            struct gen3_stream_out *stereo_out = adev->outputs[OUTPUT_STEREO];
            adev->connected_in_device = 0;
            adev->hfp_input = NULL;
            PTHREAD_MUTEX_UNLOCK(&adev->lock);
            if (hfp_in) {
                adev->hw_device.close_input_stream(&adev->hw_device, &hfp_in->stream);
            }
            if (mic) {
                adev->hw_device.close_input_stream(&adev->hw_device, &mic->stream);
            }
            if (hfp_out) {
                adev->hw_device.close_output_stream(&adev->hw_device, &hfp_out->stream);
            }
            if (stereo_out) {
                stereo_out->stream.common.standby(&stereo_out->stream.common);
            }
        }
    } else if (str_parms_get_str(parms, "hfp_set_sampling_rate", value, sizeof(value)) >= 0) {
        val = atoi(value);
        if (val > 0) {
            pcm_config_hfp_in.rate = val;
            pcm_config_hfp_out.rate = val;
        }
    } else if (str_parms_get_str(parms, "hfp_volume", value, sizeof(value)) >= 0) {
        val = atoi(value);
    }

    str_parms_destroy(parms);

    return 0;
}

static char * adev_get_parameters(const struct audio_hw_device *dev,
                                  const char *keys)
{
    ALOGV("%s: keys=%s", __FUNCTION__, keys);
    struct gen3_audio_device *adev = (gen3_audio_device*)dev;
    struct gen3_stream_in *in = nullptr;
    char* buffer_to_return = nullptr;

    if (strcmp(keys, "hfpout") == 0) {
        PTHREAD_MUTEX_LOCK(&adev->lock);
        struct gen3_stream_out *hfp_out = adev->outputs[OUTPUT_BT_SCO];
        PTHREAD_MUTEX_UNLOCK(&adev->lock);
        return ((char*)hfp_out);
    }

    if ( (strcmp(keys, "fmread") == 0) || (strcmp(keys, "hfpread") == 0) ||
         (strcmp(keys, "micread") == 0) ) {
        PTHREAD_MUTEX_LOCK(&adev->lock);
        if (strcmp(keys, "fmread") == 0) {
            in = adev->fm_input;
        } else if (strcmp(keys, "hfpread") == 0) {
            in = adev->hfp_input;
        } else if (strcmp(keys, "micread") == 0) {
            in = adev->mic_input;
        }
        PTHREAD_MUTEX_UNLOCK(&adev->lock);

        if (!in) {
            return NULL;
        }

        if ((in == adev->fm_input) || (in == adev->hfp_input)) {
            size_t frame_size = audio_stream_in_frame_size(&in->stream);
            size_t bytes_to_return = in->config.period_size * frame_size * (DEFAULT_IN_SAMPLING_RATE / in->config.rate);

            buffer_to_return = (char*)malloc(bytes_to_return);

            memset(buffer_to_return, 0x0, bytes_to_return);
            in->stream.read(&in->stream, buffer_to_return, bytes_to_return);
        } else if (in == adev->mic_input) {
            size_t bytes_to_return = popcount(AUDIO_CHANNEL_OUT_STEREO) * sizeof(short) *
                    in->config.period_size *
                    (DEFAULT_OUT_SAMPLING_RATE / pcm_config_hfp_in.rate);

            buffer_to_return = (char*)malloc(bytes_to_return);

            memset(buffer_to_return, 0x0, bytes_to_return);
            in->stream.read(&in->stream, buffer_to_return, bytes_to_return);
        }

        if (buffer_to_return) {
            return buffer_to_return;
        }

        return NULL;
    }

    return strdup("");
}

static int adev_init_check(const struct audio_hw_device * /*dev*/)
{
    ALOGV("%s",__FUNCTION__);
    return 0;
}

static int adev_set_voice_volume(struct audio_hw_device * /*dev*/, float volume)
{
    ALOGD("%s volume=%f",__FUNCTION__, volume);
    return 0;
}

static int adev_set_master_volume(struct audio_hw_device * dev, float volume)
{
    ALOGD("%s volume=%f",__FUNCTION__, volume);
    struct gen3_audio_device *adev = (struct gen3_audio_device *)dev;

    adev->master_volume = volume;
    return 0;
}

static int adev_get_master_volume(struct audio_hw_device * dev, float * volume)
{
    ALOGD("%s ",__FUNCTION__);
    struct gen3_audio_device *adev = (struct gen3_audio_device *)dev;

    *volume = adev->master_volume;
    return 0;
}

static int adev_set_mode(struct audio_hw_device * /*dev*/, audio_mode_t mode)
{
    ALOGD("%s: mode=%d",__FUNCTION__, mode);
    return 0;
}

static int adev_set_mic_mute(struct audio_hw_device *dev, bool state)
{
    ALOGV("%s",__FUNCTION__);
    struct gen3_audio_device *adev = (struct gen3_audio_device *)dev;

    adev->mic_mute = state;

    return 0;
}

static int adev_get_mic_mute(const struct audio_hw_device *dev, bool *state)
{
    ALOGV("%s",__FUNCTION__);
    struct gen3_audio_device *adev = (struct gen3_audio_device *)dev;

    *state = adev->mic_mute;

    return 0;
}

static size_t adev_get_input_buffer_size(const struct audio_hw_device * /*dev*/,
                                         const struct audio_config *config)
{
    int channel_count = popcount(config->channel_mask);
    if (check_input_parameters(config->sample_rate, config->format, channel_count) != 0)
        return 0;

    return get_input_buffer_size(config->sample_rate, config->format, channel_count);
}

static int adev_open_input_stream_fm(struct audio_hw_device *dev,
                             audio_io_handle_t /*handle*/,
                             audio_devices_t devices,
                             struct audio_config *config,
                             struct audio_stream_in **stream_in,
                             audio_input_flags_t flags,
                             const char * /*address*/,
                             audio_source_t source)
{
    struct gen3_audio_device *ladev = (struct gen3_audio_device *)dev;
    struct gen3_stream_in *in_fm;
    int channel_count = popcount(config->channel_mask);

    *stream_in = NULL;

    ALOGV("%s: req_rate = %d, format = %d, channels = %d, flags = 0x%x, audio_source = %d", __FUNCTION__,
        config->sample_rate, config->format, channel_count, flags, source);

    if (check_input_parameters(config->sample_rate, config->format, channel_count) != 0)
        return -EINVAL;

    in_fm = (struct gen3_stream_in *)calloc(1, sizeof(struct gen3_stream_in));
    if (!in_fm)
        return -ENOMEM;

    in_fm->stream.common.get_sample_rate = in_get_sample_rate;
    in_fm->stream.common.set_sample_rate = in_set_sample_rate;
    in_fm->stream.common.get_buffer_size = in_get_buffer_size;
    in_fm->stream.common.get_channels = in_get_channels;
    in_fm->stream.common.get_format = in_get_format;
    in_fm->stream.common.set_format = in_set_format;
    in_fm->stream.common.standby = in_standby;
    in_fm->stream.common.dump = in_dump;
    in_fm->stream.common.set_parameters = in_set_parameters_fm;
    in_fm->stream.common.get_parameters = in_get_parameters;
    in_fm->stream.common.add_audio_effect = in_add_audio_effect_fm;
    in_fm->stream.common.remove_audio_effect = in_remove_audio_effect_fm;
    in_fm->stream.common.set_device = in_set_device;
    in_fm->stream.common.get_device = in_get_device;
    in_fm->stream.set_gain = in_set_gain;
    in_fm->stream.read = in_read_fm;
    in_fm->stream.get_input_frames_lost = in_get_input_frames_lost;
    in_fm->stream.get_capture_position = in_get_capture_position;
    in_fm->stream.start = in_start;
    in_fm->stream.stop = in_stop;
    in_fm->stream.create_mmap_buffer = in_create_mmap_buffer;
    in_fm->stream.get_mmap_position = in_get_mmap_position;

    in_fm->start_locked = start_locked_fm;
    in_fm->standby_locked = standby_locked_fm;
    in_fm->read_pcm_locked = read_pcm_fm;

    in_fm->requested_rate = config->sample_rate;

    memcpy(&in_fm->config, &pcm_config_fm, sizeof(pcm_config_fm));
    in_fm->config.channels = channel_count;

    in_fm->main_channels = config->channel_mask;
    in_fm->aux_channels = in_fm->main_channels;

    in_fm->dev = ladev;
    in_fm->standby = 1;
    in_fm->device = devices;

    *stream_in = &in_fm->stream;

    return 0;
}

static int adev_open_input_stream_hfp(struct audio_hw_device *dev,
                             audio_io_handle_t /*handle*/,
                             audio_devices_t devices,
                             struct audio_config *config,
                             struct audio_stream_in **stream_in,
                             audio_input_flags_t flags,
                             const char * /*address*/,
                             audio_source_t source)
{
    struct gen3_audio_device *ladev = (struct gen3_audio_device *)dev;
    struct gen3_stream_in *in_hfp;
    int ret;
    int channel_count = popcount(config->channel_mask);

    *stream_in = NULL;

    ALOGV("%s: req_rate = %d, format = %d, channels = %d, flags = 0x%x, audio_source = %d", __FUNCTION__,
        config->sample_rate, config->format, channel_count, flags, source);

    if (check_input_parameters(config->sample_rate, config->format, channel_count) != 0)
        return -EINVAL;

    in_hfp = (struct gen3_stream_in *)calloc(1, sizeof(struct gen3_stream_in));
    if (!in_hfp)
        return -ENOMEM;
    in_hfp->stream.common.get_sample_rate = in_get_sample_rate;
    in_hfp->stream.common.set_sample_rate = in_set_sample_rate;
    in_hfp->stream.common.get_buffer_size = in_get_buffer_size;
    in_hfp->stream.common.get_channels = in_get_channels;
    in_hfp->stream.common.get_format = in_get_format;
    in_hfp->stream.common.set_format = in_set_format;
    in_hfp->stream.common.standby = in_standby;
    in_hfp->stream.common.dump = in_dump;
    in_hfp->stream.common.set_parameters = in_set_parameters_hfp;
    in_hfp->stream.common.get_parameters = in_get_parameters;
    in_hfp->stream.common.add_audio_effect = in_add_audio_effect_hfp;
    in_hfp->stream.common.remove_audio_effect = in_remove_audio_effect_hfp;
    in_hfp->stream.common.set_device = in_set_device;
    in_hfp->stream.common.get_device = in_get_device;
    in_hfp->stream.set_gain = in_set_gain;
    in_hfp->stream.read = in_read_hfp;
    in_hfp->stream.get_input_frames_lost = in_get_input_frames_lost;
    in_hfp->stream.get_capture_position = in_get_capture_position;
    in_hfp->stream.start = in_start;
    in_hfp->stream.stop = in_stop;
    in_hfp->stream.create_mmap_buffer = in_create_mmap_buffer;
    in_hfp->stream.get_mmap_position = in_get_mmap_position;

    in_hfp->start_locked = start_locked_hfp;
    in_hfp->standby_locked = standby_locked_hfp;
    in_hfp->read_pcm_locked = read_pcm_hfp;

    in_hfp->requested_rate = config->sample_rate;

    memcpy(&in_hfp->config, &pcm_config_hfp_in, sizeof(pcm_config_hfp_in));
    in_hfp->config.channels = channel_count;

    in_hfp->main_channels = config->channel_mask;
    in_hfp->aux_channels = in_hfp->main_channels;

    /* initialisation of preprocessor structure array is implicit with the calloc.
     * same for in->aux_channels and in->aux_channels_changed */
    if (in_hfp->requested_rate != in_hfp->config.rate) {
        in_hfp->buf_provider.get_next_buffer = get_next_buffer;
        in_hfp->buf_provider.release_buffer = release_buffer;

        ret = create_resampler(in_hfp->config.rate,
                               in_hfp->requested_rate,
                               in_hfp->config.channels,
                               RESAMPLER_QUALITY_DEFAULT,
                               NULL,
                               //&in_hfp->buf_provider,
                               &in_hfp->resampler);
        if (ret != 0) {
            ret = -EINVAL;
            goto err;
        }
    }

    in_hfp->dev = ladev;
    in_hfp->standby = 1;
    in_hfp->device = devices;

    *stream_in = &in_hfp->stream;
    return 0;

err:
    if (in_hfp->resampler)
        release_resampler(in_hfp->resampler);

    free(in_hfp);
    return ret;
}

static int adev_open_input_stream_mic(struct audio_hw_device *dev,
                             audio_io_handle_t /*handle*/,
                             audio_devices_t devices,
                             struct audio_config *config,
                             struct audio_stream_in **stream_in,
                             audio_input_flags_t flags,
                             const char * /*address*/,
                             audio_source_t source)
{
    struct gen3_audio_device *ladev = (struct gen3_audio_device *)dev;
    struct gen3_stream_in *in;
    int ret;
    int channel_count = popcount(config->channel_mask);

    *stream_in = NULL;

    ALOGV("%s: req_rate = %d, format = %d, channels = %d, flags = 0x%x, audio_source = %d", __FUNCTION__,
        config->sample_rate, config->format, channel_count, flags, source);

    if (check_input_parameters(config->sample_rate, config->format, channel_count) != 0)
        return -EINVAL;

    in = (struct gen3_stream_in *)calloc(1, sizeof(struct gen3_stream_in));
    if (!in)
        return -ENOMEM;

    in->stream.common.get_sample_rate = in_get_sample_rate;
    in->stream.common.set_sample_rate = in_set_sample_rate;
    in->stream.common.get_buffer_size = in_get_buffer_size;
    in->stream.common.get_channels = in_get_channels;
    in->stream.common.get_format = in_get_format;
    in->stream.common.set_format = in_set_format;
    in->stream.common.standby = in_standby;
    in->stream.common.dump = in_dump;
    in->stream.common.set_parameters = in_set_parameters_mic;
    in->stream.common.get_parameters = in_get_parameters;
    in->stream.common.add_audio_effect = in_add_audio_effect_mic;
    in->stream.common.remove_audio_effect = in_remove_audio_effect_mic;
    in->stream.common.set_device = in_set_device;
    in->stream.common.get_device = in_get_device;
    in->stream.set_gain = in_set_gain;
    in->stream.read = in_read_mic;
    in->stream.get_input_frames_lost = in_get_input_frames_lost;
    in->stream.get_capture_position = in_get_capture_position;
    in->stream.start = in_start;
    in->stream.stop = in_stop;
    in->stream.create_mmap_buffer = in_create_mmap_buffer;
    in->stream.get_mmap_position = in_get_mmap_position;

    in->start_locked = start_locked_mic;
    in->standby_locked = standby_locked_mic;
    in->read_pcm_locked = read_pcm_mic;

    in->requested_rate = config->sample_rate;

    memcpy(&in->config, &pcm_config_mic, sizeof(pcm_config_mic));
    in->config.channels = channel_count;

    in->main_channels = config->channel_mask;
    in->aux_channels = in->main_channels;

    /* initialisation of preprocessor structure array is implicit with the calloc.
     * same for in->aux_channels and in->aux_channels_changed */

    if (in->requested_rate != in->config.rate) {
        in->buf_provider.get_next_buffer = get_next_buffer;
        in->buf_provider.release_buffer = release_buffer;

        ret = create_resampler(in->config.rate,
                               in->requested_rate,
                               in->config.channels,
                               RESAMPLER_QUALITY_DEFAULT,
                               NULL, //&in->buf_provider,
                               &in->resampler);
        if (ret != 0) {
            ret = -EINVAL;
            goto err;
        }
    }

    in->dev = ladev;
    in->standby = 1;
    in->device = devices;

    *stream_in = &in->stream;
    return 0;

err:
    if (in->resampler)
        release_resampler(in->resampler);

    free(in);
    return ret;
}

static int adev_open_input_stream(struct audio_hw_device *dev,
                             audio_io_handle_t handle,
                             audio_devices_t devices,
                             struct audio_config *config,
                             struct audio_stream_in **stream_in,
                             audio_input_flags_t flags,
                             const char * address,
                             audio_source_t source)
{
    if ((devices & AUDIO_DEVICE_IN_FM_TUNER) == AUDIO_DEVICE_IN_FM_TUNER) {
        return adev_open_input_stream_fm(dev, handle, devices, config,
                stream_in, flags, address, source);
    } else if ((devices & AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET) == AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET) {
        return adev_open_input_stream_hfp(dev, handle, devices, config,
                stream_in, flags, address, source);
    } else {
        return adev_open_input_stream_mic(dev, handle, devices, config,
                stream_in, flags, address, source);
    }
}

static void adev_close_input_stream(struct audio_hw_device * dev __unused,
                                   struct audio_stream_in *stream)
{
    struct gen3_stream_in *in = (struct gen3_stream_in *)stream;

    in->stream.common.standby(&stream->common);

    PTHREAD_MUTEX_LOCK(&in->lock);

    if (in->read_buf) {
        free(in->read_buf);
    }
    if (in->resampler) {
        release_resampler(in->resampler);
    }
    if (in->proc_buf_in)
        free(in->proc_buf_in);
    if (in->proc_buf_out)
        free(in->proc_buf_out);
    if (in->ref_buf)
        free(in->ref_buf);
    if (in->upmixed_read_buffer)
    {
        free(in->upmixed_read_buffer);
        in->upmixed_read_buffer = NULL;
    }

    PTHREAD_MUTEX_UNLOCK(&in->lock);
    free(stream);

    return;
}

static int adev_dump(const audio_hw_device_t * /*device*/, int /*fd*/)
{
    ALOGV("%s",__FUNCTION__);
    return -ENOSYS;
}

static int adev_set_master_mute(struct audio_hw_device * dev, bool mute)
{
    ALOGV("%s, mute = %d", __FUNCTION__, mute);
    struct gen3_audio_device *adev = (struct gen3_audio_device *)dev;

    adev->master_mute = mute;

    return 0;
}

static int adev_get_master_mute(struct audio_hw_device * dev, bool * mute)
{
    ALOGV("%s",__FUNCTION__);
    struct gen3_audio_device *adev = (struct gen3_audio_device *)dev;

    *mute = adev->master_mute;

    return 0;
}

static int adev_create_audio_patch(struct audio_hw_device * /*dev*/, unsigned int num_sources,
                const struct audio_port_config * sources, unsigned int num_sinks,
                const struct audio_port_config * sinks, audio_patch_handle_t * /*handle*/)
{
    ALOGV("%s",__FUNCTION__);
    for (unsigned int source_counter = 0; source_counter < num_sources; source_counter++)
    {
        ALOGV("- - - - >                         SOURCES: %d", num_sources);
        ALOGV("- - - - > sources[%d].channel_mask = %x", source_counter, sources[source_counter].channel_mask);
        ALOGV("- - - - > sources[%d].type = %x", source_counter, sources[source_counter].type);
        ALOGV("- - - - > sources[%d].ext.mix.usecase.stream = %x",
                source_counter, sources[source_counter].ext.mix.usecase.stream);
        ALOGV("- - - - > sources[%d].ext.mix.usecase.source = %x",
                source_counter, sources[source_counter].ext.mix.usecase.source);
    }
    for (unsigned int sinks_counter = 0; sinks_counter < num_sinks; sinks_counter++)
    {
        ALOGV("- - - - >                         SINKS: %d", num_sinks);
        ALOGV("- - - - > sinks[%d].channel_mask = %x", sinks_counter, sinks[sinks_counter].channel_mask);
        ALOGV("- - - - > sinks[%d].type = %x", sinks_counter, sinks[sinks_counter].type);
        ALOGV("- - - - > sinks[%d].ext.device.type = %x", sinks_counter, sinks[sinks_counter].ext.device.type);
    }
    return 0;
}

static int adev_release_audio_patch(struct audio_hw_device * /*dev*/,
                                    audio_patch_handle_t /*handle*/)
{
    ALOGV("%s",__FUNCTION__);
    return 0;
}

static int adev_get_audio_port(struct audio_hw_device * /*dev*/,
                               struct audio_port * /*port*/)
{
    ALOGV("%s",__FUNCTION__);
    return 0;
}

static int adev_set_audio_port_config(struct audio_hw_device * /*dev*/,
                                      const struct audio_port_config * /*config*/)
{
    ALOGV("%s",__FUNCTION__);
    return 0;
}

static int adev_close(hw_device_t *device)
{
    ALOGV("%s",__FUNCTION__);
    struct gen3_audio_device *adev = (struct gen3_audio_device *)device;

    mixer_close(adev->mixer3168);
    mixer_close(adev->mixerfm);
    free(device);
    return 0;
}

static int adev_open(const hw_module_t* module, const char* name,
                     hw_device_t** device)
{
    ALOGV("%s",__FUNCTION__);

    if (strcmp(name, AUDIO_HARDWARE_INTERFACE) != 0)
        return -EINVAL;

    struct gen3_audio_device *adev = (struct gen3_audio_device*)calloc(1, sizeof(struct gen3_audio_device));
    if (!adev)
        return -ENOMEM;

    adev->hw_device.common.tag = HARDWARE_DEVICE_TAG;
    adev->hw_device.common.version = AUDIO_DEVICE_API_VERSION_3_0;
    adev->hw_device.common.module = (struct hw_module_t *) module;
    adev->hw_device.common.close = adev_close;

    adev->hw_device.init_check = adev_init_check;
    adev->hw_device.set_voice_volume = adev_set_voice_volume;
    adev->hw_device.set_master_volume = adev_set_master_volume;
    adev->hw_device.get_master_volume = adev_get_master_volume;
    adev->hw_device.set_mode = adev_set_mode;
    adev->hw_device.set_mic_mute = adev_set_mic_mute;
    adev->hw_device.get_mic_mute = adev_get_mic_mute;
    adev->hw_device.set_parameters = adev_set_parameters;
    adev->hw_device.get_parameters = adev_get_parameters;
    adev->hw_device.get_input_buffer_size = adev_get_input_buffer_size;
    adev->hw_device.open_output_stream = adev_open_output_stream;
    adev->hw_device.close_output_stream = adev_close_output_stream;
    adev->hw_device.open_input_stream = adev_open_input_stream;
    adev->hw_device.close_input_stream = adev_close_input_stream;
    adev->hw_device.dump = adev_dump;

    adev->hw_device.set_master_mute = adev_set_master_mute;
    adev->hw_device.get_master_mute = adev_get_master_mute;
    adev->hw_device.create_audio_patch = adev_create_audio_patch;
    adev->hw_device.release_audio_patch = adev_release_audio_patch;
    adev->hw_device.get_audio_port = adev_get_audio_port;
    adev->hw_device.set_audio_port_config = adev_set_audio_port_config;

    adev->mixer3168 = mixer_open(CARD_GEN3_PCM3168A);

    if (!adev->mixer3168) {
        free(adev);
        ALOGE("Unable to open the pcm3168 mixer, aborting.");
        return -EINVAL;
    }

    adev->mixerfm = mixer_open(CARD_GEN3_FM);

    if (!adev->mixerfm) {
        mixer_close(adev->mixer3168);
        free(adev);
        ALOGE("Unable to open the fm mixer, aborting.");
        return -EINVAL;
    }

    /* Set the default route before the PCM stream is opened */
    PTHREAD_MUTEX_LOCK(&adev->lock);

    set_route_by_array(adev->mixer3168, defaults3168, 1);
    set_route_by_array(adev->mixerfm, defaultsfm, 1);

    adev->mode = AUDIO_MODE_NORMAL;

    adev->connected_in_device = 0;

    PTHREAD_MUTEX_UNLOCK(&adev->lock);

    *device = &adev->hw_device.common;

    return 0;
}

static struct hw_module_methods_t hal_module_methods = {
    .open = adev_open,
};

struct audio_module HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = AUDIO_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = AUDIO_HARDWARE_MODULE_ID,
        .name = "Kingfisher audio HW HAL",
        .author = "The Android Open Source Project",
        .methods = &hal_module_methods,
    },
};
