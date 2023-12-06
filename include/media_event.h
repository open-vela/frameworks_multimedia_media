/****************************************************************************
 * frameworks/media/include/media_event.h
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

#ifndef FRAMEWORKS_MEDIA_INCLUDE_MEDIA_EVENT_H
#define FRAMEWORKS_MEDIA_INCLUDE_MEDIA_EVENT_H

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
 * Metadata Definitions
 ****************************************************************************/

#define MEDIA_METAFLAG_STATE 1
#define MEDIA_METAFLAG_VOLUME 2
#define MEDIA_METAFLAG_POSITION 4
#define MEDIA_METAFLAG_DURATION 8
#define MEDIA_METAFLAG_TITLE 16
#define MEDIA_METAFLAG_ARTIST 32

typedef struct media_metadata_s {
    int flags; /* Indicates available fields. */
    int state; /* Positive for active; Zero for inactive; Negative for errno */
    int volume;
    unsigned position;
    unsigned duration;
    char* title;
    char* artist;
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

#endif /* FRAMEWORKS_MEDIA_INCLUDE_MEDIA_EVENT_H */
