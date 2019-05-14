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

#ifndef __ANDROID_HARDWARE_AUDIO_HW_DEFS_H__
#define __ANDROID_HARDWARE_AUDIO_HW_DEFS_H__

/* number of frames per short period (low latency) */
#define PLAY_PERIOD_SIZE            512
/* number of pseudo periods for low latency playback */
#define PLAY_PERIOD_COUNT           4

/* number of frames per capture period */
#define CAPTURE_PERIOD_SIZE         512
/* number of periods for capture */
#define CAPTURE_PERIOD_COUNT        4

#define DEFAULT_OUT_PCM_FORMAT      PCM_FORMAT_S16_LE
#define DEFAULT_OUT_ANDROID_FORMAT  AUDIO_FORMAT_PCM_16_BIT
#define DEFAULT_OUT_SAMPLING_RATE   48000

#define DEFAULT_IN_PCM_FORMAT       PCM_FORMAT_S16_LE
#define DEFAULT_IN_ANDROID_FORMAT   AUDIO_FORMAT_PCM_16_BIT
#define DEFAULT_IN_SAMPLING_RATE    48000

#define AUDIO_CHANNEL_OUT_COUNT 8
#define AUDIO_CHANNEL_IN_COUNT 6

#define DEFAULT_HFP_SAMPLING_RATE   8000

#define kStatusSendDelay            5000 // us
#define kStatusSendTimeout          50000 // us

#endif // __ANDROID_HARDWARE_AUDIO_HW_DEFS_H__
