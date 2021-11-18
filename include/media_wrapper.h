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

// audio mode
#define MEDIA_AUDIO_MODE_NORMAL      "normal"
#define MEDIA_AUDIO_MODE_PHONE       "phone"

// devices
#define MEDIA_DEVICE_A2DP            "a2dp"

// stream types
#define MEDIA_STREAM_SCO             "SCO"
#define MEDIA_STREAM_RING            "Ring"
#define MEDIA_STREAM_NOTIFICATION    "Notify"
#define MEDIA_STREAM_ACCESSIBILITY   "Health"
#define MEDIA_STREAM_MUSIC           "Music"
#define MEDIA_STREAM_SPORT           "Sport"
#define MEDIA_STREAM_TTS             "TTS"
#define MEDIA_STREAM_ALARM           "Alarm"
#define MEDIA_STREAM_SYSTEM_ENFORCED "Enforced"

// stream type volume
#define MEDIA_STREAM_TYPE_VOLUME(x)  x"Volume"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: media_policy_set_audio_mode
 *
 * Description:
 *   Set audio mode, let media_policy_set_string do real work.
 *
 * Input Parameters:
 *   mode       - new audio mode.
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_policy_set_audio_mode(const char *mode);

/****************************************************************************
 * Name: media_policy_get_audio_mode
 *
 * Description:
 *   Get audio mode, let media_policy_get_string do real work.
 *
 * Input Parameters:
 *   mode       - return current audio mode.
 *   len        - size of mode.
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_policy_get_audio_mode(char *mode, int len);

/****************************************************************************
 * Name: media_policy_set_mute_mode
 *
 * Description:
 *   Set mute mode, some stream types are muted when mute mode is ON.
 *
 * Input Parameters:
 *   mute       - zero for OFF, one for ON
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_policy_set_mute_mode(int mute);

/****************************************************************************
 * Name: media_policy_get_mute_mode
 *
 * Description:
 *   Get mute mode, some stream types are muted when mute mode is ON.
 *
 * Input Parameters:
 *   mute       - currtent value, zero for OFF, one for ON.
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_policy_get_mute_mode(int *mute);

/****************************************************************************
 * Name: media_policy_set_no_disturb_mode
 *
 * Description:
 *   Set no-disturb mode, some stream types are muted when it's ON.
 *
 * Input Parameters:
 *   mute       - zero for OFF, one for ON
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_policy_set_no_disturb_mode(int no_disturb);

/****************************************************************************
 * Name: media_policy_get_no_disturb_mode
 *
 * Description:
 *   Get no-disturb mode, some stream types are muted when it's ON.
 *
 * Input Parameters:
 *   no_disturb     - currtent value, zero for OFF, one for ON.
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_policy_get_no_disturb_mode(int *no_disturb);

/****************************************************************************
 * Name: media_policy_set_devices_available
 *
 * Description:
 *   Insert some devices to available devices set, let media_policy_include
 *     do real work.
 *
 * Input Parameters:
 *   devices    - available devices, separate by '|'.
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_policy_set_devices_available(const char *devices);

/****************************************************************************
 * Name: media_policy_set_devices_unavailable
 *
 * Description:
 *   Remove some devices from available devices set, let media_policy_exclude
 *     do real work.
 *
 * Input Parameters:
 *   devices    - unavailable devices, separate by '|'.
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_policy_set_devices_unavailable(const char *devices);

/****************************************************************************
 * Name: media_policy_get_devices_available
 *
 * Description:
 *   Get currenct available devices, let media_policy_get_string do real work.
 *
 * Input Parameters:
 *   devices    - available device names, separate by '|'.
 *   len        - size of devices.
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_policy_get_devices_available(char *devices, int len);

/****************************************************************************
 * Name: media_policy_set_stream_volume
 *
 * Description:
 *   Set stream type's volume index, let media_policy_set_int do real work.
 *
 * Input Parameters:
 *   stream     - stream type volume's criterion name.
 *   volume     - new volume index.
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_policy_set_stream_volume(const char *stream, int volume);

/****************************************************************************
 * Name: media_policy_get_stream_volume
 *
 * Description:
 *   Set stream type's volume index, let media_policy_get_int do real work.
 *
 * Input Parameters:
 *   stream     - stream type volume's criterion name.
 *   volume     - return current volume index.
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_policy_get_stream_volume(const char *stream, int *volume);

/****************************************************************************
 * Name: media_policy_increase_stream_volume
 *
 * Description:
 *   Increase stream type's volume index by 1, let media_policy_increase
 *     do real work.
 *
 * Input Parameters:
 *   stream     - stream type volume's criterion name.
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_policy_increase_stream_volume(const char *stream);

/****************************************************************************
 * Name: media_policy_decrease_stream_volume
 *
 * Description:
 *   Increase stream type's volume index by 1, let media_policy_decrease
 *     do real work.
 *
 * Input Parameters:
 *   stream     - stream type volume's criterion name.
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_policy_decrease_stream_volume(const char *stream);

#ifdef __cplusplus
}
#endif

#endif /* FRAMEWORKS_MEDIA_WRAPPER_MEDIA_WRAPPER_H */
