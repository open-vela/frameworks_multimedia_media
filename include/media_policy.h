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

#include <media_stream.h>

#ifdef __cplusplus
extern "C" {
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MEDIA_AUDIO_MODE_NORMAL "normal" /*!< play and capture audio */
#define MEDIA_AUDIO_MODE_PHONE "phone"
#define MEDIA_AUDIO_MODE_RINGTONE "ringtone"

#define MEDIA_DEVICE_A2DP "a2dp" /*!< bt cellphone */
#define MEDIA_DEVICE_A2DP_SNK "a2dpsnk"
#define MEDIA_DEVICE_SCO "sco" /*!< phone */
#define MEDIA_DEVICE_MIC "mic"
#define MEDIA_DEVICE_MODEM "modem"

#define MEDIA_SAMPLERATE_8000 "8000"
#define MEDIA_SAMPLERATE_16000 "16000"
#define MEDIA_SAMPLERATE_22050 "22050"
#define MEDIA_SAMPLERATE_32000 "32000"
#define MEDIA_SAMPLERATE_44100 "44100"
#define MEDIA_SAMPLERATE_48000 "48000"
#define MEDIA_SAMPLERATE_96000 "96000"
#define MEDIA_SAMPLERATE_192000 "192000"

/*
 * @code HFP telephony app example.
 *  // 1. On init, you should request audio focus as telephone APP.
 *  focus_handle = media_focus_request(&first_suggest, MEDIA_STREAM_SCO, cb, cookie);
 *
 *  // 2. Then you should set audio mode to RINGTONE or PHONE,
 *  //    and you should always set mode before setting devices.
 *  ret = media_policy_set_audio_mode(MEDIA_MODE_RINGTONE);
 *
 *  // 3. Then you should choose the devices to use,
 *  //    Under different audio mode, same devices has different meaning,
 *  //    here under PHONE mode, SCO is for hfp.
 *  ret = media_policy_set_devices_use(MEDIA_DEVICE_SCO);
 *
 *  // 4. Do your work.
 *
 *  // 5. On exit, you should restore the devices status,
 *  //    and set audio mode back to NORMAL.
 *  ret = media_policy_set_devices_unuse(MEDIA_DEVICE_SCO);
 *  ret = media_policy_set_audio_mode(MEDIA_MODE_NORMAL);
 * @endcode
 *
 * @code btsco monitor example.
 *  // update media policy when status of btsco changed;
 *  // this usually happened in poll_callback.
 *  if (msbc) {
 *      ret = media_policy_set_hfp_samplerate(MEDIA_SAMPLERATE_8000);
 *      ret = media_policy_set_devices_available(MEDIA_DEVICE_SCO);
 *  } else if (cvsd) {
 *      ret = media_policy_set_hfp_samplerate(MEDIA_SAMPLERATE_16000);
 *      ret = media_policy_set_devices_available(MEDIA_DEVICE_SCO);
 *  } else {
 *      // disconnected.
 *      ret = media_policy_set_devices_unavailable(MEDIA_DEVICE_SCO);
 *  }
 * @endcode
 */

/****************************************************************************
 * Public Functions for Mode Control
 ****************************************************************************/

/**
 * @brief Set audio mode.
 *
 * @param[in] mode  New audio mode, in the format of "MEDIA_AUDIO_MODE_XXX".
 *                  There are five macros to define audio mode，details
 *                  @see MEDIA_DEVICE_* in media_wrapper.h
 * @return Zero on success; a negated errno value on failure.
 * @note Before changing mode, SCO should be turned off.
 */
int media_policy_set_audio_mode(const char* mode);

/**
 * @brief Get audio mode.
 *
 * @param[in] mode  A pointer to save current audio mode.
 * @param[out] len  Size of current mode.
 * @return Zero on success; a negated errno value on failure.
 */
int media_policy_get_audio_mode(char* mode, int len);

/****************************************************************************
 * Public Functions for Devices Control
 ****************************************************************************/

/**
 * @brief Set devices using.
 * @param[in] devices   new using devices, MEDIA_DEVICE_XXX.
 * @return Zero on success; a negated errno value on failure.
 * @note This api is for applications to declare
 *       whether it needs to use the device.
 */
int media_policy_set_devices_use(const char* devices);

/**
 * @brief Set devices not using.
 * @param[in] devices   new unavailable devices, MEDIA_DEVICE_XXX.
 * @return Zero on success; a negated errno value on failure.
 */
int media_policy_set_devices_unuse(const char* devices);

/**
 * @brief Get current using devices.
 * @param[out] devices   current using devices.
 *                       device names are separated by "|".
 * @param[in] len        sizeof devices.
 * @return Zero on success; a negated errno value on failure.
 */
int media_policy_get_devices_use(char* devices, int len);

/**
 * @brief Check whether devices are being used.
 * @param[in] devices   devices to check, use MEDIA_DEVICE_XXX.
                        device names are separated by "|".
 * @param[out] use  status of devices.
 *                  - 0: all given devices not used.
 *                  - 1: at least one of devices is being used.
 * @return Zero on success; a negated errno value on failure.
 */
int media_policy_is_devices_use(const char* devices, int* use);

/**
 * @brief Set hfp(hands free protocol) sampling rate.
 * @param[in] rate  sample rate for SCO, MEDIA_SAMPLING_RATE_XXX.
 * @return Zero on success; a negated errno value on failure.
 * @note Sample rate is a negotiated result, which is sent from BT,
 *       should be set before set sco available.
 */
int media_policy_set_hfp_samplerate(const char* rate);

/**
 * @brief Set devices available.
 * @param[in] devices   new available devices, MEDIA_DEVICE_XXX.
 * @return Zero on success; a negated errno value on failure.
 * @note This api is for device monitors to declare
 *       whether the device is available or not.
 */
int media_policy_set_devices_available(const char* devices);

/**
 * @brief Set devices unavailable.
 * @param[in] devices   new unavailable devices, MEDIA_DEVICE_XXX.
 * @return Zero on success; a negated errno value on failure.
 */
int media_policy_set_devices_unavailable(const char* devices);

/**
 * @brief Get current available devices.
 * @param[out] devices   current available devices.
 *                       device names are separated by "|".
 * @param[in] len        sizeof devices.
 * @return Zero on success; a negated errno value on failure.
 */
int media_policy_get_devices_available(char* devices, int len);

/**
 * @brief Check whether devices are available.
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
 * @brief Set mute mode.
 * @param[in] mute  New mute mode.
 *                  - 0: mute mode is off.
 *                  - 1: mute mode is on.
 * @return Zero on success; a negated errno value on failure.
 */
int media_policy_set_mute_mode(int mute);

/**
 * @brief Get mute mode.
 * @param[out] mute Current mute mode.
 *                  - 0: mute mode is off.
 *                  - 1: mute mode is on.
 * @return Zero on success; a negated errno value on failure.
 */
int media_policy_get_mute_mode(int* mute);

/**
 * @brief Set stream type volume index.
 * @param[in] stream    stream type, MEDIA_STREAM_XXX.
 * @param[in] volume    new volume index.
 * @return Zero on success; a negated errno value on failure.
 */
int media_policy_set_stream_volume(const char* stream, int volume);

/**
 * @brief Get stream type volume index.
 * @param[in] stream    stream type, MEDIA_STREAM_XXX.
 * @param[out] volume   current volume index.
 * @return Zero on success; a negated errno value on failure.
 */
int media_policy_get_stream_volume(const char* stream, int* volume);

/**
 * @brief Increase stream type volume index by 1.
 * @param[in] stream    stream type, MEDIA_STREAM_XXX.
 * @return Zero on success; a negated errno value on failure.
 */
int media_policy_increase_stream_volume(const char* stream);

/**
 * @brief Decrease stream type volume index by 1.
 * @param[in] stream    stream type, MEDIA_STREAM_XXX.
 * @return Zero on success; a negated errno value on failure.
 */
int media_policy_decrease_stream_volume(const char* stream);

/****************************************************************************
 * Public Policy Basic Functions
 ****************************************************************************/

/**
 * @brief Set criterion with interger value.
 * This function is used to set the value of an integer-type policy
 * criterion. You should use this function when you need to update or
 * configure an integer-based policy criterion.
 * @param[in] name      criterion name
 * @param[in] value     new integer value
 * @param[in] apply     Indicates whether to apply the configurations.
 * Set to non-zero to apply configurations, zero to not apply
 * @return Zero on success; a negated errno value on failure.
 * @note Should use wrapper functions rather than using this directly.
 */
int media_policy_set_int(const char* name, int value, int apply);

/**
 * @brief Get criterion in interger value.
 * This function is used to retrieve the value of an integer-type policy
 * criterion. You should use this function when you need to access the
 * current value of an integer-based policy criterion.
 * @param[in] name      criterion name
 * @param[out] value    A pointer to store the current integer value
 * @return Zero on success; a negated errno value on failure.
 * @note Should use wrapper functions rather than using this directly.
 */
int media_policy_get_int(const char* name, int* value);

/**
 * @brief Set criterion with string value.
 * This function is used to set the value of a string-type policy
 * criterion. You should use this function when you need to update
 * or configure a string-based policy criterion.
 * @param[in] name      criterion name
 * @param[in] value     new string value
 * @param[in] apply     Indicates whether to apply the configurations.
 * Set to non-zero to apply configurations, zero to not apply.
 * @return Zero on success; a negated errno value on failure.
 * @note Should use wrapper functions rather than using this directly.
 */
int media_policy_set_string(const char* name, const char* value, int apply);

/**
 * @brief Get criterion in string value.
 * This function is used to retrieve the value of a string-type
 * policy criterion. You should use this function when you need
 * to access the current value of a string-based policy criterion.
 * @param[in] name      criterion name
 * @param[out] value    A buffer to store the current string value
 * @param[in] len       The size of the 'value' buffer
 * @return Zero on success; a negated errno value on failure.
 * @note Should use wrapper functions rather than using this directly.
 */
int media_policy_get_string(const char* name, char* value, int len);

/**
 * @brief Insert string values to InclusiveCriterion.
 * This function is used to insert string values into the
 * InclusiveCriterion of a policy. You should use this function
 * when you need to add string values to an InclusiveCriterion policy.
 * @param[in] name      criterion name
 * @param[in] values    string values
 * @param[in] apply     Indicates whether to apply the configurations.
 * Set to non-zero to apply configurations, zero to not apply.
 * @return Zero on success; a negated errno value on failure.
 * @note Should use wrapper functions rather than using this directly.
 * @warning only for InclusiveCriterion, never call on ExclusiveCriterion.
 */
int media_policy_include(const char* name, const char* values, int apply);

/**
 * @brief Remove string values from InclusiveCriterion.
 * This function is used to remove string values from the
 * InclusiveCriterion of a policy. You should use this function
 * when you need to remove specific string values from an
 * InclusiveCriterion policy.
 * @param[in] name      criterion name
 * @param[in] values    string values
 * @param[in] apply     Indicates whether to apply the configurations.
 * Set to non-zero to apply configurations, zero to not apply.
 * @return Zero on success; a negated errno value on failure.
 * @note Should use wrapper functions rather than using this directly.
 * @warning only for InclusiveCriterion, never call on ExclusiveCriterion.
 */
int media_policy_exclude(const char* name, const char* values, int apply);

/**
 * @brief Check whether string values included in InclusiveCriterion.
 * This function is used to check whether a set of string values is
 * included in an InclusiveCriterion policy. You should use this
 * function when you need to determine if specific string values
 * are part of an InclusiveCriterion policy.
 * @param[in] name      criterion name
 * @param[in] values    string values
 * @param[out] result   whether the values are included or not
 * @return Zero on success; a negated errno value on failure.
 * @note Should use wrapper functions rather than using this directly.
 * @warning only for InclusiveCriterion, never call on ExclusiveCriterion.
 */
int media_policy_contain(const char* name, const char* values, int* result);

/**
 * @brief Increase interger value of criterion by 1.
 * This function is used to increment the integer value of an
 * ExclusiveCriterion policy by 1. You should use this function
 * when you need to increment the integer value of an
 * ExclusiveCriterion policy.
 * @param[in] name      criterion name
 * @param[in] apply     Indicates whether to apply the configurations.
 * Set to non-zero to apply configurations, zero to not apply.
 * @return Zero on success; a negated errno value on failure.
 * @note Should use wrapper functions rather than using this directly.
 * @warning only for ExclusiveCriterion, never call on InclusiveCriterion.
 */
int media_policy_increase(const char* name, int apply);

/**
 * @brief Decrease criterion interger value by 1.
 * This function is used to decrement the integer value of an
 * ExclusiveCriterion policy by 1. You should use this function
 * when you need to decrement the integer value of an
 * ExclusiveCriterion policy.
 * @param[in] name      criterion name
 * @param[in] apply     Indicates whether to apply the configurations.
 * Set to non-zero to apply configurations, zero to not apply.
 * @return Zero on success; a negated errno value on failure.
 * @note Should use wrapper functions rather than using this directly.
 * @warning only for ExclusiveCriterion, never call on InclusiveCriterion.
 */
int media_policy_decrease(const char* name, int apply);

/**
 * Mute microphone for builtin_mic or bluetooth_mic.
 * @param[in] mute   mute mode.
 *                  - 1: mute mode is off.
 *                  - 0: mute mode is on.
 * @return Zero on success; a negated errno value on failure.
 */
int media_policy_set_mic_mute(int mute);

#ifdef __cplusplus
}
#endif

#endif /* FRAMEWORKS_MEDIA_INCLUDE_MEDIA_POLICY_H */
