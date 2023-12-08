/****************************************************************************
 * frameworks/media/include/media_policy.h
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

#ifndef FRAMEWORKS_MEDIA_INCLUDE_MEDIA_POLICY_H
#define FRAMEWORKS_MEDIA_INCLUDE_MEDIA_POLICY_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <media_defs.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MEDIA_AUDIO_MODE_NORMAL "normal" /*!< play and capture audio */
#define MEDIA_AUDIO_MODE_PHONE "phone"
#define MEDIA_AUDIO_MODE_RINGTONE "ringtone"
#define MEDIA_AUDIO_MODE_VOIP "voip"

#define MEDIA_DEVICE_A2DP "a2dp" /*!< bt cellphone */
#define MEDIA_DEVICE_A2DP_SNK "a2dpsnk"
#define MEDIA_DEVICE_BLE "ble"
#define MEDIA_DEVICE_SCO "sco"
#define MEDIA_DEVICE_MIC "mic"
#define MEDIA_DEVICE_MODEM "modem"

/****************************************************************************
 * Public Funtions
 ****************************************************************************/

/**
 * @brief Set audio mode.
 *
 * @param[in] mode  MEDIA_MODE_*
 * @return int  Zero on success; a negative errno value on failure.
 *
 * @note your application should follow these steps.
 *  1. choose audio mode;
 *  2. Force use devices (protocol) you need;
 *  3. then do your work.
 *  4. Revert the force use.
 *  5. set audio mode to MEDIA_MODE_NORMAL.
 *
 * @code hfp example
 *  // 1. enter the incall mode.
 *  ret = media_policy_set_audio_mode(MEDIA_MODE_PHONE);
 *
 *  // 2. use btsco protocol.
 *  ret = media_policy_set_devices_use(MEDIA_DEVICE_SCO);
 *
 *  // 3. in call.
 *
 *  // 4. revert choose.
 *  ret = media_policy_set_devices_unuse(MEDIA_DEVICE_SCO);
 *
 *  // 5. back to normal mode.
 *  ret = media_policy_set_audio_mode(MEDIA_MODE_NORMAL);
 * @endcode
 */
int media_policy_set_audio_mode(const char* mode);

/**
 * @brief Get current audio mode.
 *
 * @param[out] mode Buffer address.
 * @param[in] len   Buffer length.
 * @return int  Zero on success; a negative errno value on failure.
 */
int media_policy_get_audio_mode(char* mode, int len);

/**
 * @brief Force Use devices (or protocol).
 *
 * @param[in] devices   MEDIA_DEVICE_*,
 *                      support multi-devices with "|" delimiter.
 * @return int  Zero on success; a negative errno value on failure.
 */
int media_policy_set_devices_use(const char* devices);

/**
 * @brief Revert the force use.
 *
 * @param[in] devices   MEDIA_DEVICE_*,
 *                      support multi-devices with "|" delimiter.
 * @return int  Zero on success; a negative errno value on failure.
 */
int media_policy_set_devices_unuse(const char* devices);

/**
 * @brief Get current force use devices.
 *
 * @param[out] devices   Buffer address, device names has "|" delimiter
 *                       (e.g. "sco", "sco|mic", "<none>").
 * @param[in] len        Buffer length.
 * @return int  Zero on success; a negative errno value on failure.
 */
int media_policy_get_devices_use(char* devices, int len);

/**
 * @brief Check whether devices are under force-use.
 *
 * @param[in] devices   MEDIA_DEVICE_*,
 *                      support multi-devices with "|" delimiter.
 * @param[out] use  status of devices.
 *                  - 0: all given devices not used.
 *                  - 1: at least one of devices is being used.
 * @return int  Zero on success; a negative errno value on failure.
 */
int media_policy_is_devices_use(const char* devices, int* use);

/**
 * @brief Set HFP sample rate.
 *
 * HFP (Hand Free Profile) is based on bt-sco, the samplerate
 * is uncertain before negotiation is done.
 *
 * @param[in] rate  8000 for cvsd, 16000 for msbc.
 * @return int  Zero on success; a negative errno value on failure.
 *
 * @warning This api is only for certain service, if you are not sure
 * whether you need this API, then you definitely don't need.
 */
int media_policy_set_hfp_samplerate(int rate);

/**
 * @brief Report devices (or protocol) available.
 *
 * @param[in] devices   MEDIA_DEVICE_*,
 *                      support multi-devices with "|" delimiter.
 * @return int  Zero on success; a negative errno value on failure.
 *
 * @warning This api is only for certain service, if you are not sure
 * whether you need this API, then you definitely don't need.
 */
int media_policy_set_devices_available(const char* devices);

/**
 * @brief Report devices (or protocol) unavailable.
 *
 * @param[in] devices   MEDIA_DEVICE_*,
 *                      support multi-devices with "|" delimiter.
 * @return int  Zero on success; a negative errno value on failure.
 *
 * @warning This api is only for certain service, if you are not sure
 * whether you need this API, then you definitely don't need.
 */
int media_policy_set_devices_unavailable(const char* devices);

/**
 * @brief Get current available devices.
 *
 * @param[out] devices  MEDIA_DEVICE_*, device names has "|" delimiter
 *                       (e.g. "sco", "sco|mic", "<none>").
 * @param[in] len       Buffer len.
 * @return int  Zero on success; a negative errno value on failure.
 */
int media_policy_get_devices_available(char* devices, int len);

/**
 * @brief Check whether devices are available.
 * @param[in] devices   devices to check, use MEDIA_DEVICE_XXX.
                        device names are separated by "|".
 * @param[out] available  status of devices.
 *                        - 0: all given devices are unavailable.
 *                        - 1: at least one of devices is available.
 * @return int  Zero on success; a negative errno value on failure.
 */
int media_policy_is_devices_available(const char* devices, int* available);

/**
 * @brief Set mute mode.
 *
 * @param[in] mute  New mute mode.
 *                  - 0: mute mode is off.
 *                  - 1: mute mode is on.
 * @return int  Zero on success; a negative errno value on failure.
 */
int media_policy_set_mute_mode(int mute);

/**
 * @brief Get mute mode.
 *
 * @param[out] mute Current mute mode.
 *                  - 0: mute mode is off.
 *                  - 1: mute mode is on.
 * @return int  Zero on success; a negative errno value on failure.
 */
int media_policy_get_mute_mode(int* mute);

/**
 * @brief Set stream type volume index.
 *
 * @param[in] stream    MEDIA_STREAM_XXX.
 * @param[in] volume    new volume index.
 * @return int  Zero on success; a negative errno value on failure.
 */
int media_policy_set_stream_volume(const char* stream, int volume);

/**
 * @brief Get stream type volume index.
 *
 * @param[in] stream    MEDIA_STREAM_XXX.
 * @param[out] volume   current volume index.
 * @return int  Zero on success; a negative errno value on failure.
 */
int media_policy_get_stream_volume(const char* stream, int* volume);

/**
 * @brief Increase stream type volume index by 1.
 *
 * @param[in] stream    MEDIA_STREAM_XXX.
 * @return int  Zero on success; a negative errno value on failure.
 */
int media_policy_increase_stream_volume(const char* stream);

/**
 * @brief Decrease stream type volume index by 1.
 *
 * @param[in] stream    MEDIA_STREAM_XXX.
 * @return int  Zero on success; a negative errno value on failure.
 */
int media_policy_decrease_stream_volume(const char* stream);

/**
 * @brief Mute the mic.
 *
 * @param[in] mute  mute mode.
 *                  - 1: mute mode is off.
 *                  - 0: mute mode is on.
 * @return int  Zero on success; a negative errno value on failure.
 */
int media_policy_set_mic_mute(int mute);

/**
 * @brief Set numerical value to criterion.
 *
 * @param[in] name  Criterion name.
 * @param[in] value Numerical value.
 * @param[in] apply Whether apply change to policy.
 *
 * @return int  Zero on success; a negative errno value on failure.
 *
 * @warning This api is only for certain service, if you are not sure
 * whether you need this API, then you definitely don't need.
 */
int media_policy_set_int(const char* name, int value, int apply);

/**
 * @brief Get numerical value of criterion.
 *
 * @param[in] name      Criterion name
 * @param[out] value    Numerical value.
 * @return int  Zero on success; a negative errno value on failure.
 *
 * @note This api is only for certain service, if you are not sure
 * whether you need this API, then you definitely don't need.
 */
int media_policy_get_int(const char* name, int* value);

/**
 * @brief Set literal value to criterion.
 *
 * @param[in] name  Criterion name.
 * @param[in] value Literal value.
 * @param[in] apply Whether apply change to policy.
 * @return int  Zero on success; a negative errno value on failure.
 *
 * @note This api is only for certain service, if you are not sure
 * whether you need this API, then you definitely don't need.
 */
int media_policy_set_string(const char* name, const char* value, int apply);

/**
 * @brief Get literal value from criterion.
 *
 * @param[in] name      Criterion name.
 * @param[out] value    Buffer address.
 * @param[in] len       Buffer length.
 * @return int  Zero on success; a negative errno value on failure.
 *
 * @note This api is only for certain service, if you are not sure
 * whether you need this API, then you definitely don't need.
 */
int media_policy_get_string(const char* name, char* value, int len);

/**
 * @brief Insert literal values to InclusiveCriterion.
 *
 * @param[in] name      Criterion name
 * @param[in] values    Literal value.
 * @param[in] apply     Whether apply change to policy.
 * @return int  Zero on success; a negative errno value on failure.
 *
 * @note This api is only for certain service, if you are not sure
 * whether you need this API, then you definitely don't need.
 */
int media_policy_include(const char* name, const char* values, int apply);

/**
 * @brief Remove literal values from InclusiveCriterion.
 *
 * @param[in] name      Criterion name
 * @param[in] values    Literal value.
 * @param[in] apply     Whether apply change to policy.
 * @return int  Zero on success; a negative errno value on failure.
 *
 * @note This api is only for certain service, if you are not sure
 * whether you need this API, then you definitely don't need.
 */
int media_policy_exclude(const char* name, const char* values, int apply);

/**
 * @brief Check whether literal values included in InclusiveCriterion.
 *
 * @param[in] name      Criterion name
 * @param[in] values    Literal values
 * @param[out] result   whether the values are included.
 * @return int  Zero on success; a negative errno value on failure.
 *
 * @note This api is only for certain service, if you are not sure
 * whether you need this API, then you definitely don't need.
 */
int media_policy_contain(const char* name, const char* values, int* result);

/**
 * @brief Increase value of NumericalCriterion by 1.
 *
 * @param[in] name      Criterion name
 * @param[in] apply     Whether apply change to policy.
 *
 * @return int  Zero on success; a negative errno value on failure.
 *
 * @note This api is only for certain service, if you are not sure
 * whether you need this API, then you definitely don't need.
 */
int media_policy_increase(const char* name, int apply);

/**
 * @brief Decrease value of NumericalCriterion by 1.
 *
 * @param[in] name      Criterion name
 * @param[in] apply     Whether apply change to policy.
 *
 * @return int  Zero on success; a negative errno value on failure.
 *
 * @note This api is only for certain service, if you are not sure
 * whether you need this API, then you definitely don't need.
 */
int media_policy_decrease(const char* name, int apply);

#ifdef CONFIG_LIBUV
/**
 * @brief Set numerical value to a criterion.
 *
 * @param[in] loop      Loop handle of current thread.
 * @param[in] name      Criterion name.
 * @param[in] value     Numerical value to set.
 * @param[in] apply     Whether apply new value to policy configurations.
 * @param[out] cb       Call after receiving result.
 * @param[in] cookie    Callback argument.
 * @return int  Zero on sucess, negative errno on else.
 *
 * @note This api is only for certain service, if you are not sure
 * whether you need this API, then you definitely don't need.
 */
int media_uv_policy_set_int(void* loop, const char* name,
    int value, int apply, media_uv_callback cb, void* cookie);

/**
 * @brief Get numerical value of a criterion.
 *
 * @param[in] loop      Loop handle of current thread.
 * @param[in] name      Criterion name.
 * @param[out] cb       Call after receiving result.
 * @param[in] cookie    Callback argument.
 * @return int  Zero on sucess, negative errno on else.
 *
 * @note This api is only for certain service, if you are not sure
 * whether you need this API, then you definitely don't need.
 */
int media_uv_policy_get_int(void* loop, const char* name,
    media_uv_int_callback cb, void* cookie);

/**
 * @brief Increase numerical value of a criterion by one.
 *
 * @param[in] loop      Loop handle of current thread.
 * @param[in] name      Criterion name.
 * @param[in] apply     Whether apply new value to policy configurations.
 * @param[out] cb       Call after receiving result.
 * @param[in] cookie    Callback argument.
 * @return int  Zero on sucess, negative errno on else.
 *
 * @note This api is only for certain service, if you are not sure
 * whether you need this API, then you definitely don't need.
 */
int media_uv_policy_increase(void* loop, const char* name, int apply,
    media_uv_callback cb, void* cookie);

/**
 * @brief Set literal value to a criterion.
 *
 * @param[in] loop      Loop handle of current thread.
 * @param[in] name      Criterion name.
 * @param[in] value     Lieteral value to set.
 * @param[in] apply     Whether apply new value to policy configurations.
 * @param[out] cb       Call after receiving result.
 * @param[in] cookie    Callback argument.
 * @return int  Zero on sucess, negative errno on else.
 *
 * @note This api is only for certain service, if you are not sure
 * whether you need this API, then you definitely don't need.
 */
int media_uv_policy_set_string(void* loop, const char* name,
    const char* value, int apply, media_uv_callback cb, void* cookie);

/**
 * @brief Get literal value of a criterion.
 *
 * @param[in] loop      Loop handle of current thread.
 * @param[in] name      Criterion name.
 * @param[out] cb       Call after receiving result.
 * @param[in] cookie    Callback argument.
 * @return int  Zero on sucess, negative errno on else.
 *
 * @note This api is only for certain service, if you are not sure
 * whether you need this API, then you definitely don't need.
 */
int media_uv_policy_get_string(void* loop, const char* name,
    media_uv_string_callback cb, void* cookie);

/**
 * @brief Decrease numerical value of a criterion by one.
 *
 * @param[in] loop      Loop handle of current thread.
 * @param[in] name      Criterion name.
 * @param[in] apply     Whether apply new value to policy configurations.
 * @param[out] cb       Call after receiving result.
 * @param[in] cookie    Callback argument.
 * @return int  Zero on sucess, negative errno on else.
 *
 * @note This api is only for certain service, if you are not sure
 * whether you need this API, then you definitely don't need.
 */
int media_uv_policy_decrease(void* loop, const char* name, int apply,
    media_uv_callback cb, void* cookie);

/**
 * @brief Insert literal values to InclusiveCriterion.
 *
 * @param[in] loop      Loop handle of current thread.
 * @param[in] name      Criterion name.
 * @param[in] value     Literal values.
 * @param[in] apply     Whether apply new value to policy configurations.
 * @param[out] cb       Call after receiving result.
 * @param[in] cookie    Callback argument.
 * @return int  Zero on sucess, negative errno on else.
 *
 * @note This api is only for certain service, if you are not sure
 * whether you need this API, then you definitely don't need.
 */
int media_uv_policy_include(void* loop, const char* name,
    const char* value, int apply, media_uv_callback cb, void* cookie);

/**
 * @brief Remove literal values from InclusiveCriterion.
 *
 * @param[in] loop      Loop handle of current thread.
 * @param[in] name      Criterion name.
 * @param[in] value     Literal values.
 * @param[in] apply     Whether apply new value to policy configurations.
 * @param[out] cb       Call after receiving result.
 * @param[in] cookie    Callback argument.
 * @return int  Zero on sucess, negative errno on else.
 *
 * @note This api is only for certain service, if you are not sure
 * whether you need this API, then you definitely don't need.
 */
int media_uv_policy_exclude(void* loop, const char* name,
    const char* value, int apply, media_uv_callback cb, void* cookie);

/**
 * @brief Check whether literal values included in InclusiveCriterion.
 *
 * @param[in] loop      Loop handle of current thread.
 * @param[in] name      Criterion name.
 * @param[in] value     Literal values.
 * @param[out] cb       Call after receiving result.
 * @param[in] cookie    Callback argument.
 * @return int  Zero on sucess, negative errno on else.
 *
 * @note This api is only for certain service, if you are not sure
 * whether you need this API, then you definitely don't need.
 */
int media_uv_policy_contain(void* loop, const char* name,
    const char* value, media_uv_int_callback cb, void* cookie);

/**
 * @brief Set volume of a stream type.
 *
 * @param[in] loop      Loop handle of current thread.
 * @param[in] stream    Stream type, @see MEDIA_STREAM_* .
 * @param[in] volume    Integer volume to set.
 * @param[out] cb       Call after receiving result.
 * @param[in] cookie    Callback argument.
 * @return int  Zero on sucess, negative errno on else.
 */
int media_uv_policy_set_stream_volume(void* loop,
    const char* stream, int volume, media_uv_callback cb, void* cookie);

/**
 * @brief Get volume of a stream type.
 *
 * @param[in] loop      Loop handle of current thread.
 * @param[in] stream    Stream type, @see MEDIA_STREAM_* .
 * @param[in] volume    Integer volume to set.
 * @param[out] cb       Call after receiving result.
 * @param[in] cookie    Callback argument.
 * @return int  Zero on sucess, negative errno on else.
 */
int media_uv_policy_get_stream_volume(void* loop,
    const char* stream, media_uv_int_callback cb, void* cookie);

/**
 * @brief Increase volume of a stream type.
 *
 * @param[in] loop      Loop handle of current thread.
 * @param[in] stream    Stream type, @see MEDIA_STREAM_* .
 * @param[out] cb       Call after receiving result.
 * @param[in] cookie    Callback argument.
 * @return int  Zero on sucess, negative errno on else.
 */
int media_uv_policy_increase_stream_volume(void* loop,
    const char* stream, media_uv_callback cb, void* cookie);

/**
 * @brief Decrease volume of a stream type.
 *
 * @param[in] loop      Loop handle of current thread.
 * @param[in] stream    Stream type, @see MEDIA_STREAM_* .
 * @param[out] cb       Call after receiving result.
 * @param[in] cookie    Callback argument.
 * @return int  Zero on sucess, negative errno on else.
 */
int media_uv_policy_decrease_stream_volume(void* loop,
    const char* stream, media_uv_callback cb, void* cookie);

/**
 * @brief Set audio mode.
 *
 * @param[in] loop      Loop handle of current thread.
 * @param[in] mode      New audio mode.
 * @param[out] cb       Call after receiving result.
 * @param[in] cookie    Callback argument.
 * @return int  Zero on success; a negative errno value on failure.
 */
int media_uv_policy_set_audio_mode(void* loop, const char* mode,
    media_uv_callback cb, void* cookie);

/**
 * @brief Get current audio mode.
 *
 * @param[in] loop      Loop handle of current thread.
 * @param[out] cb       Call after receiving result.
 * @param[in] cookie    Callback argument.
 * @return int  Zero on success; a negative errno value on failure.
 */
int media_uv_policy_get_audio_mode(void* loop,
    media_uv_string_callback cb, void* cookie);

/**
 * @brief Force use/unuse devices (or protocols).
 *
 * @param[in] loop      Loop handle of current thread.
 * @param[in] devices   Target devices.
 * @param[in] use       Whether devices are set using or unusing.
 * @param[out] cb       Call after receiving result.
 * @param[in] cookie    Callback argument.
 * @return int  Zero on success; a negative errno value on failure.
 */
int media_uv_policy_set_devices_use(void* loop,
    const char* devices, bool use, media_uv_callback cb, void* cookie);

/**
 * @brief Get current force-use devices.
 *
 * @param[in] loop      Loop handle of current thread.
 * @param[out] cb       Call after receiving result.
 * @param[in] cookie    Callback argument.
 * @return int  Zero on success; a negative errno value on failure.
 */
int media_uv_policy_get_devices_use(void* loop,
    media_uv_string_callback cb, void* cookie);

/**
 * @brief Check whether devices are being used.
 *
 * @param[in] loop      Loop handle of current thread.
 * @param[in] devices   Devices to check.
 * @param[out] cb       Call after receiving result.
 * @param[in] cookie    Callback argument.
 * @return int  Zero on success; a negative errno value on failure.
 */
int media_uv_policy_is_devices_use(void* loop,
    const char* devices, media_uv_int_callback cb, void* cookie);

/**
 * @brief Set hfp(hands free protocol) sampling rate.
 *
 * @param[in] loop      Loop handle of current thread.
 * @param[in] rate      8000 for cvsd, 16000 for msbc.
 * @param[out] cb       Call after receiving result.
 * @param[in] cookie    Callback argument.
 * @return int  Zero on success; a negative errno value on failure.
 *
 * @warning This api is only for certain service, if you are not sure
 * whether you need this API, then you definitely don't need.
 *
 * @deprecated This api would soon use `int rate`.
 */
int media_uv_policy_set_hfp_samplerate(void* loop, int rate,
    media_uv_callback cb, void* cookie);

/**
 * @brief Set devices available or unavailable.
 *
 * @param[in] loop      Loop handle of current thread.
 * @param[in] devices   Target devices.
 * @param[in] available Whether devices are set available or unavailable.
 * @param[out] cb       Call after receiving result.
 * @param[in] cookie    Callback argument.
 * @return int  Zero on success; a negative errno value on failure.
 *
 * @warning This api is only for certain service, if you are not sure
 * whether you need this API, then you definitely don't need.
 */
int media_uv_policy_set_devices_available(void* loop,
    const char* devices, bool available, media_uv_callback cb, void* cookie);

/**
 * @brief Get current available devices.
 *
 * @param[in] loop      Loop handle of current thread.
 * @param[out] cb       Call after receiving result.
 * @param[in] cookie    Callback argument.
 * @return int  Zero on success; a negative errno value on failure.
 */
int media_uv_policy_get_devices_available(void* loop,
    media_uv_string_callback cb, void* cookie);

/**
 * @brief Check whether devices are available.
 *
 * @param[in] loop      Loop handle of current thread.
 * @param[in] devices   Devices to check.
 * @param[out] cb       Call after receiving result.
 * @param[in] cookie    Callback argument.
 * @return int  Zero on success; a negative errno value on failure.
 */
int media_uv_policy_is_devices_available(void* loop, const char* devices,
    media_uv_int_callback cb, void* cookie);

/**
 * @brief Set mute mode.
 *
 * @param[in] loop      Loop handle of current thread.
 * @param[in] mute      New mute mode.
 * @param[out] cb       Call after receiving result.
 * @param[in] cookie    Callback argument.
 * @return int  Zero on success; a negative errno value on failure.
 */
int media_uv_policy_set_mute_mode(void* loop, int mute,
    media_uv_callback cb, void* cookie);

/**
 * @brief Get mute mode.
 *
 * @param[in] loop      Loop handle of current thread.
 * @param[out] cb       Call after receiving result.
 * @param[in] cookie    Callback argument.
 * @return int  Zero on success; a negative errno value on failure.
 */
int media_uv_policy_get_mute_mode(void* loop,
    media_uv_int_callback cb, void* cookie);

/**
 * @brief Mute microphone for builtin_mic or bluetooth_mic.
 *
 * @param[in] loop      Loop handle of current thread.
 * @param[in] mute      Mute mode.
 * @param[out] cb       Call after receiving result.
 * @param[in] cookie    Callback argument.
 * @return int  Zero on success; a negative errno value on failure.
 */
int media_uv_policy_set_mic_mute(void* loop, int mute,
    media_uv_callback cb, void* cookie);
#endif /* CONFIG_LIBUV */

#ifdef __cplusplus
}
#endif

#endif /* FRAMEWORKS_MEDIA_INCLUDE_MEDIA_POLICY_H */
