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

#define MEDIA_AUDIO_MODE_NORMAL      "normal"   /*!< play and capture audio */
#define MEDIA_AUDIO_MODE_PHONE       "phone"

#define MEDIA_DEVICE_A2DP            "a2dp"     /*!< bt cellphone */

#define MEDIA_STREAM_SCO             "SCO"      /*!< in call */
#define MEDIA_STREAM_RING            "Ring"
#define MEDIA_STREAM_NOTIFICATION    "Notify"   /*!< message notification */
#define MEDIA_STREAM_ACCESSIBILITY   "Health"   /*!< health notification */
#define MEDIA_STREAM_MUSIC           "Music"
#define MEDIA_STREAM_SPORT           "Sport"
#define MEDIA_STREAM_TTS             "TTS"      /*!< text-to-speech */
#define MEDIA_STREAM_ALARM           "Alarm"
#define MEDIA_STREAM_SYSTEM_ENFORCED "Enforced"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/**
 * Set audio mode.
 * @param[in] mode  new audio mode, MEDIA_AUDIO_MODE_XXX.
 * @return Zero on success; a negated errno value on failure.
 */
int media_policy_set_audio_mode(const char *mode);

/**
 * Get audio mode.
 * @param[in] mode  current audio mode.
 * @param[out] len  sizeof mode.
 * @return Zero on success; a negated errno value on failure.
 */
int media_policy_get_audio_mode(char *mode, int len);

/**
 * Set mute mode.
 * @param[in] mute  new mute mode.
 *                  - 0: mute mode is off.
 *                  - 1: mute mode is on.
 * @return Zero on success; a negated errno value on failure.
 */
int media_policy_set_mute_mode(int mute);

/**
 * Get mute mode.
 * @param[out] mute current mute mode.
 *                  - 0: mute mode is off.
 *                  - 1: mute mode is on.
 * @return Zero on success; a negated errno value on failure.
 */
int media_policy_get_mute_mode(int *mute);

/**
 * Set no-disturb mode.
 * @param[in] no_disturb    new no-disturb mode.
 *                          - 0: no-disturb mode is off.
 *                          - 1: no-disturb mode is on.
 * @return Zero on success; a negated errno value on failure.
 */
int media_policy_set_no_disturb_mode(int no_disturb);

/**
 * Get no-disturb mode.
 * @param[in] no_disturb    current no-disturb mode.
 *                          - 0: no-disturb mode is off.
 *                          - 1: no-disturb mode is on.
 * @return Zero on success; a negated errno value on failure.
 */
int media_policy_get_no_disturb_mode(int *no_disturb);

/**
 * Set divices available.
 * @param[in] devices   new available devices, MEDIA_DEVICE_XXX.
 * @return Zero on success; a negated errno value on failure.
 */
int media_policy_set_devices_available(const char *devices);

/**
 * Set divices unavailable.
 * @param[in] devices   new unavailable devices, MEDIA_DEVICE_XXX.
 * @return Zero on success; a negated errno value on failure.
 */
int media_policy_set_devices_unavailable(const char *devices);

/**
 * Get current available devices.
 * @param[out] devices   current available devices.
 * @param[in] len        sizeof devices.
 * @return Zero on success; a negated errno value on failure.
 */
int media_policy_get_devices_available(char *devices, int len);

/**
 * Set stream type volume index.
 * @param[in] stream    stream type, MEDIA_STREAM_XXX.
 * @param[in] volume    new volume index.
 * @return Zero on success; a negated errno value on failure.
 */
int media_policy_set_stream_volume(const char *stream, int volume);

/**
 * Get stream type volume index.
 * @param[in] stream    stream type, MEDIA_STREAM_XXX.
 * @param[out] volume   current volume index
 * @return Zero on success; a negated errno value on failure.
 */
int media_policy_get_stream_volume(const char *stream, int *volume);

/**
 * Increase stream type volume index by 1.
 * @param[in] stream    stream type, MEDIA_STREAM_XXX.
 * @return Zero on success; a negated errno value on failure.
 */
int media_policy_increase_stream_volume(const char *stream);

/**
 * Decrease stream type volume index by 1.
 * @param[in] stream    stream type, MEDIA_STREAM_XXX.
 * @return Zero on success; a negated errno value on failure.
 */
int media_policy_decrease_stream_volume(const char *stream);

#ifdef __cplusplus
}
#endif

#endif /* FRAMEWORKS_MEDIA_WRAPPER_MEDIA_WRAPPER_H */
