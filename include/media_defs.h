/****************************************************************************
 * frameworks/media/include/media_defs.h
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

#ifndef FRAMEWORKS_MEDIA_INCLUDE_MEDIA_DEFS_H
#define FRAMEWORKS_MEDIA_INCLUDE_MEDIA_DEFS_H

#ifdef __cplusplus
extern "C" {
#endif

/****************************************************************************
 * Event Definitions
 ****************************************************************************/

/****************************************************************************
 * Stream State Machine
 *
 *     open
 *       |
 *       V
 *  +---------+                         +----------+
 *  |         | ------ prepare -------> |          |
 *  | STOPPED | <------ stop ---------- | PREPARED |
 *  |         | <----+                  |          |
 *  +---------+       \                 +----------+
 *    ^    ^         stop                       |
 *    |    \           \                        |
 *    |     \       +-----------+               |
 *    |      \      |           |             start
 *  stop      \     | COMPLETED | ----------+   |
 *    |        \    |           | <----+    |   |
 *    |         \   +-----------+       \  seek |
 *    |          \                       \  |   |
 *    |           \                       \ V   V
 *  +---------+    \                    +---------+
 *  |         |     +--- stop --------- |         |
 *  | PAUSED  | <------ pause --------- | STARTED |
 *  |         | ------- start --------> |         |
 *  +---------+                         +---------+
 *
 ****************************************************************************/

#define MEDIA_EVENT_NOP 0

/* Stream status change, used by player&recorder. */

#define MEDIA_EVENT_PREPARED 1
#define MEDIA_EVENT_STARTED 2
#define MEDIA_EVENT_PAUSED 3
#define MEDIA_EVENT_STOPPED 4
#define MEDIA_EVENT_SEEKED 5 /* SEEKED is not a state. */
#define MEDIA_EVENT_COMPLETED 6

/* Control message and its result, used by session. */

#define MEDIA_EVENT_CHANGED 101 /* Controllee changed (auto generate). */
#define MEDIA_EVENT_UPDATED 102 /* Controllee updated (auto generate). */
#define MEDIA_EVENT_START 103
#define MEDIA_EVENT_PAUSE 104
#define MEDIA_EVENT_STOP 105
#define MEDIA_EVENT_PREV_SONG 106
#define MEDIA_EVENT_NEXT_SONG 107
#define MEDIA_EVENT_INCREASE_VOLUME 108
#define MEDIA_EVENT_DECREASE_VOLUME 109

/**
 * @brief Callback to notify event to user.
 *
 * For player&recorder: events mean stream status change.
 * For session: events mean control message and its result.
 *
 * @param[in] cookie    User's private context.
 * @param[in] event     MEDIA_EVENT_* .
 * @param[in] result    Result of the event.
 * @param[in] extra     Extra message corresponding with event.
 *
 * @note player, recorder, session use different event set.
 */
typedef void (*media_event_callback)(void* cookie, int event, int result,
    const char* extra);

/****************************************************************************
 * Focus Definitions
 ****************************************************************************/

/* Suggestions for users. */

#define MEDIA_FOCUS_PLAY 0
#define MEDIA_FOCUS_STOP 1
#define MEDIA_FOCUS_PAUSE 2
#define MEDIA_FOCUS_PLAY_BUT_SILENT 3
#define MEDIA_FOCUS_PLAY_WITH_DUCK 4 /* Play with low volume. */
#define MEDIA_FOCUS_PLAY_WITH_KEEP 5 /* Nothing should be done. */

/**
 * @brief Callback to receive suggestions.
 *
 * @param[in] suggestion    MEDIA_FOCUS_* .
 * @param[in] cookie        Argument set by focus request.
 *
 * @code
 *  void user_focu_callback(int suggestion, void* cookie)
 *  {
 *      switch(suggestion) {
 *      case MEDIA_FOCUS_PLAY:
 *      case MEDIA_FOCUS_STOP:
 *      case MEDIA_FOCUS_PAUSE:
 *      case MEDIA_FOCUS_PLAY_BUT_SILENT:
 *      case MEDIA_FOCUS_PLAY_WITH_DUCK:
 *      case MEDIA_FOCUS_PLAY_WITH_KEEP:
 *      default:
 *      }
 *  }
 * @endcode
 */
typedef void (*media_focus_callback)(int suggestion, void* cookie);

/****************************************************************************
 * Policy Definitions
 ****************************************************************************/

#define MEDIA_POLICY_APPLY 1
#define MEDIA_POLICY_NOT_APPLY 0

#define MEDIA_POLICY_AUDIO_MODE "AudioMode"
#define MEDIA_POLICY_DEVICE_USE "UsingDevices"
#define MEDIA_POLICY_DEVICE_AVAILABLE "AvailableDevices"
#define MEDIA_POLICY_MUTE_MODE "MuteMode"
#define MEDIA_POLICY_MIC_MODE "MicMode"
#define MEDIA_POLICY_VOLUME "Volume"
#define MEDIA_POLICY_HFP_SAMPLERATE "HFPSampleRate"
#define MEDIA_POLICY_A2DP_OFFLOAD_MODE "A2dpOffloadMode"
#define MEDIA_POLICY_ANC_OFFLOAD_MODE "AncOffloadMode"

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
#define MEDIA_DEVICE_AUX_DIGITAL "digital"

#define MEDIA_DEVICE_IN_AUX_DIGITAL "digital_in"
#define MEDIA_DEVICE_OUT_AUX_DIGITAL "digital_out"

/**
 * @brief Callback to receive current criterion value.
 *
 * @param[in] number     Current numerical value.
 * @param[in] literal    Current literal value if have, NULL if not.
 */
typedef void (*media_policy_change_callback)(void* cookie,
    int number, const char* literal);

/****************************************************************************
 * Scenario Definitions
 ****************************************************************************/

/* Scenario types, for focus. */

#define MEDIA_SCENARIO_INCALL "SCO"
#define MEDIA_SCENARIO_RING "Ring"
#define MEDIA_SCENARIO_ALARM "Alarm"
#define MEDIA_SCENARIO_DRAIN "Enforced"
#define MEDIA_SCENARIO_NOTIFICATION "Notify" /* message notification */
#define MEDIA_SCENARIO_RECORD "Record"
#define MEDIA_SCENARIO_TTS "TTS" /* text-to-speech */
#define MEDIA_SCENARIO_ACCESSIBILITY "Health" /* health notification */
#define MEDIA_SCENARIO_SPORT "Sport"
#define MEDIA_SCENARIO_INFO "Info"
#define MEDIA_SCENARIO_MUSIC "Music"
#define MEDIA_SCENARIO_COMMUNICATION "Communication"

/****************************************************************************
 * Stream Definitions
 ****************************************************************************/

/* Stream types, for player and policy. */

#define MEDIA_STREAM_RING "Ring"
#define MEDIA_STREAM_ALARM "Alarm"
#define MEDIA_STREAM_SYSTEM_ENFORCED "Enforced"
#define MEDIA_STREAM_NOTIFICATION "Notify"
#define MEDIA_STREAM_RECORD "Record"
#define MEDIA_STREAM_TTS "TTS"
#define MEDIA_STREAM_ACCESSIBILITY "Health"
#define MEDIA_STREAM_SPORT "Sport"
#define MEDIA_STREAM_INFO "Info"
#define MEDIA_STREAM_MUSIC "Music"
#define MEDIA_STREAM_EMERGENCY "Emergency"
#define MEDIA_STREAM_CALLRING "CallRing"
#define MEDIA_STREAM_MEDIA "Media" /* video */
#define MEDIA_STREAM_A2DP_SNK "A2dpsnk" /* bt music */
#define MEDIA_STREAM_INCALL "SCO" /* @deprecated */
#define MEDIA_STREAM_COMMUNICATION "Intercom"

/****************************************************************************
 * Source Definitions
 ****************************************************************************/

/* Source types, for recorder. */

#define MEDIA_SOURCE_MIC "Capture"

/****************************************************************************
 * Metadata Definitions
 ****************************************************************************/

#define MEDIA_METAFLAG_STATE 0x1
#define MEDIA_METAFLAG_VOLUME 0x2
#define MEDIA_METAFLAG_POSITION 0x4
#define MEDIA_METAFLAG_DURATION 0x8
#define MEDIA_METAFLAG_TITLE 0x10
#define MEDIA_METAFLAG_ARTIST 0x20
#define MEDIA_METAFLAG_ALBUM 0x40

typedef struct media_metadata_s {
    int flags; /* Indicates available fields. */
    int state; /* Positive for active; Zero for inactive; Negative for errno */
    int volume;
    unsigned position;
    unsigned duration;
    char* title;
    char* artist;
    char* album;
} media_metadata_t;

/****************************************************************************
 * Async Callback Definitions
 ****************************************************************************/

/**
 * @brief Common async callback.
 *
 * @param[out] cookie   Private context.
 * @param[out] ret      Result of process, zero on success, negative errno on failure.
 */
typedef void (*media_uv_callback)(void* cookie, int ret);

/**
 * @brief Callback to get int integer.
 *
 * @param[out] cookie   Private context.
 * @param[out] ret      Result of process, zero on success, negative errno on failure.
 * @param[out] val      Value.
 */
typedef void (*media_uv_int_callback)(void* cookie, int ret, int val);

/**
 * @brief Callback to get unsinged integer.
 *
 * @param[out] cookie   Private context.
 * @param[out] ret      Result of process, zero on success, negative errno on failure.
 * @param[out] val      Value.
 */
typedef void (*media_uv_unsigned_callback)(void* cookie, int ret, unsigned val);

/**
 * @brief Callback to get float integer.
 *
 * @param[out] cookie   Private context.
 * @param[out] ret      Result of process, zero on success, negative errno on failure.
 * @param[out] val      Value.
 */
typedef void (*media_uv_float_callback)(void* cookie, int ret, float val);

/**
 * @brief Callback to get string.
 *
 * @param[out] cookie   Private context.
 * @param[out] ret      Result of process, zero on success, negative errno on failure.
 * @param[out] val      Value.
 */
typedef void (*media_uv_string_callback)(void* cookie, int ret, const char* val);

/**
 * @brief Callback to get object.
 *
 * @param[out] cookie   Private context.
 * @param[out] ret      Result of process, zero on success, negative errno on failure.
 * @param[out] obj      Object.
 */
typedef void (*media_uv_object_callback)(void* cookie, int ret, void* obj);

#ifdef __cplusplus
}
#endif

#endif /* FRAMEWORKS_MEDIA_INCLUDE_MEDIA_DEFS_H */
