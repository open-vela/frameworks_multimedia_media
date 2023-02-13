/****************************************************************************
 * frameworks/media/include/media_wrapper.h
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

#ifndef FRAMEWORKS_MEDIA_INCLUDE_MEDIA_WRAPPER_H
#define FRAMEWORKS_MEDIA_INCLUDE_MEDIA_WRAPPER_H

#ifdef __cplusplus
extern "C" {
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MEDIA_AUDIO_MODE_NORMAL "normal" /*!< play and capture audio */
#define MEDIA_AUDIO_MODE_PHONE "phone"

#define MEDIA_DEVICE_A2DP "a2dp" /*!< bt cellphone */
#define MEDIA_DEVICE_A2DP_SNK "a2dpsnk"
#define MEDIA_DEVICE_SCO "sco" /*!< phone */
#define MEDIA_DEVICE_MIC "mic"

#define MEDIA_SAMPLERATE_8000 "8000"
#define MEDIA_SAMPLERATE_16000 "16000"
#define MEDIA_SAMPLERATE_22050 "22050"
#define MEDIA_SAMPLERATE_32000 "32000"
#define MEDIA_SAMPLERATE_44100 "44100"
#define MEDIA_SAMPLERATE_48000 "48000"
#define MEDIA_SAMPLERATE_96000 "96000"
#define MEDIA_SAMPLERATE_192000 "192000"

#define MEDIA_STREAM_SCO "SCO" /*!< in call */
#define MEDIA_STREAM_RING "Ring"
#define MEDIA_STREAM_ALARM "Alarm"
#define MEDIA_STREAM_SYSTEM_ENFORCED "Enforced"
#define MEDIA_STREAM_NOTIFICATION "Notify" /*!< message notification */
#define MEDIA_STREAM_RECORD "Record"
#define MEDIA_STREAM_TTS "TTS" /*!< text-to-speech */
#define MEDIA_STREAM_ACCESSIBILITY "Health" /*!< health notification */
#define MEDIA_STREAM_SPORT "Sport"
#define MEDIA_STREAM_INFO "Info"
#define MEDIA_STREAM_MUSIC "Music"

/****************************************************************************
 * Public Functions for Mode Control
 ****************************************************************************/

/**
 * Set audio mode.
 * @param[in] mode  new audio mode, MEDIA_AUDIO_MODE_XXX.
 * @return Zero on success; a negated errno value on failure.
 * @note Before changing mode, call media_policy_set_btsco_use(0), which
 *       can turn off SCO and avoid noise.
 */
int media_policy_set_audio_mode(const char* mode);

/**
 * Get audio mode.
 * @param[in] mode  current audio mode.
 * @param[out] len  sizeof mode.
 * @return Zero on success; a negated errno value on failure.
 */
int media_policy_get_audio_mode(char* mode, int len);

/****************************************************************************
 * Public Functions for Devices Control
 ****************************************************************************/

/**
 * Set devices using.
 * @param[in] devices   new using devices, MEDIA_DEVICE_XXX.
 * @return Zero on success; a negated errno value on failure.
 * @note This api is for applications to declare
 *       whether it needs to use the device.
 */
int media_policy_set_devices_use(const char* devices);

/**
 * Set devices not using.
 * @param[in] devices   new unavailable devices, MEDIA_DEVICE_XXX.
 * @return Zero on success; a negated errno value on failure.
 */
int media_policy_set_devices_unuse(const char* devices);

/**
 * Get current using devices.
 * @param[out] devices   current using devices.
 *                       device names are separated by "|".
 * @param[in] len        sizeof devices.
 * @return Zero on success; a negated errno value on failure.
 */
int media_policy_get_devices_use(char* devices, int len);

/**
 * Check whether devices are being used.
 * @param[in] devices   devices to check, use MEDIA_DEVICE_XXX.
                        device names are separated by "|".
 * @param[out] use  status of devices.
 *                  - 0: all given devices not used.
 *                  - 1: at least one of devices is being used.
 * @return Zero on success; a negated errno value on failure.
 */
int media_policy_is_devices_use(const char* devices, int* use);

/**
 * Set hfp(hands free protocol) sampling rate.
 * @param[in] rate  sample rate for SCO, MEDIA_SAMPLING_RATE_XXX.
 * @return Zero on success; a negated errno value on failure.
 * @note Sample rate is a negotiated result, which is sent from BT,
 *       should be set before set sco available.
 */
int media_policy_set_hfp_samplerate(const char* rate);

/**
 * Set devices available.
 * @param[in] devices   new available devices, MEDIA_DEVICE_XXX.
 * @return Zero on success; a negated errno value on failure.
 * @note This api is for device monitors to declare
 *       whether the device is available or not.
 */
int media_policy_set_devices_available(const char* devices);

/**
 * Set devices unavailable.
 * @param[in] devices   new unavailable devices, MEDIA_DEVICE_XXX.
 * @return Zero on success; a negated errno value on failure.
 */
int media_policy_set_devices_unavailable(const char* devices);

/**
 * Get current available devices.
 * @param[out] devices   current available devices.
 *                       device names are separated by "|".
 * @param[in] len        sizeof devices.
 * @return Zero on success; a negated errno value on failure.
 */
int media_policy_get_devices_available(char* devices, int len);

/**
 * Check whether devices are available.
 * @param[in] devices   devices to check, use MEDIA_DEVICE_XXX.
                        device names are separated by "|".
 * @param[out] available  status of devices.
 *                        - 0: all given devices are unavailable.
 *                        - 1: at least one of devices is available.
 * @return Zero on success; a negated errno value on failure.
 */
int media_policy_is_devices_available(const char* devices, int* available);

/****************************************************************************
 * Public Functions for Volume Control
 ****************************************************************************/

/**
 * Set mute mode.
 * @param[in] mute  New mute mode.
 *                  - 0: mute mode is off.
 *                  - 1: mute mode is on.
 * @return Zero on success; a negated errno value on failure.
 */
int media_policy_set_mute_mode(int mute);

/**
 * Get mute mode.
 * @param[out] mute Current mute mode.
 *                  - 0: mute mode is off.
 *                  - 1: mute mode is on.
 * @return Zero on success; a negated errno value on failure.
 */
int media_policy_get_mute_mode(int* mute);

/**
 * Set stream type volume index.
 * @param[in] stream    stream type, MEDIA_STREAM_XXX.
 * @param[in] volume    new volume index.
 * @return Zero on success; a negated errno value on failure.
 */
int media_policy_set_stream_volume(const char* stream, int volume);

/**
 * Get stream type volume index.
 * @param[in] stream    stream type, MEDIA_STREAM_XXX.
 * @param[out] volume   current volume index.
 * @return Zero on success; a negated errno value on failure.
 */
int media_policy_get_stream_volume(const char* stream, int* volume);

/**
 * Increase stream type volume index by 1.
 * @param[in] stream    stream type, MEDIA_STREAM_XXX.
 * @return Zero on success; a negated errno value on failure.
 */
int media_policy_increase_stream_volume(const char* stream);

/**
 * Decrease stream type volume index by 1.
 * @param[in] stream    stream type, MEDIA_STREAM_XXX.
 * @return Zero on success; a negated errno value on failure.
 */
int media_policy_decrease_stream_volume(const char* stream);

#ifdef __cplusplus
}
#endif

#endif /* FRAMEWORKS_MEDIA_WRAPPER_MEDIA_WRAPPER_H */
