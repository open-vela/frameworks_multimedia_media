/****************************************************************************
 * frameworks/media/include/media_session.h
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

#ifndef FRAMEWORKS_MEDIA_INCLUDE_MEDIA_SESSION_H
#define FRAMEWORKS_MEDIA_INCLUDE_MEDIA_SESSION_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <media_event.h>
#include <stddef.h>

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C" {
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/**
 * Open one session path.
 * @param[in] params    Open params.
 * @return Pointer to created handle or NULL on failure.
 */
void* media_session_open(const char* params);

/**
 * Close the session path.
 * @param[in] handle    The session path to be destroyed.
 * @return Zero on success; a negated errno value on failure.
 */
int media_session_close(void* handle);

/**
 * Set event callback to the session path, the callback will be called
 * when state of the most active player changed.
 * @param[in] handle    The session path.
 * @param[in] cookie    User cookie, will bring back to user when do event_cb.
 * @param[in] event_cb  Event callback.
 * @return Zero on success; a negated errno value on failure.
 */
int media_session_set_event_callback(void* handle, void* cookie,
    media_event_callback event_cb);

/**
 * Start a player through sessoin path.
 * @param[in] handle    The session path.
 * @return Zero on success; a negated errno value on failure.
 */
int media_session_start(void* handle);

/**
 * Stop the most active player through sessoin path.
 * @param[in] handle    The session path.
 * @return Zero on success; a negated errno value on failure.
 */
int media_session_stop(void* handle);

/**
 * Pause the most active player through sessoin path.
 * @param[in] handle    The session path.
 * @return Zero on success; a negated errno value on failure.
 */
int media_session_pause(void* handle);

/**
 * Get playback position of the most active player through sessoin path.
 * @param[in] handle    The session path
 * @param[in] mesc      Playback position (from begining)
 * @return Zero on success; a negated errno value on failure.
 */
int media_session_get_position(void* handle, unsigned int* msec);

/**
 * Get playback file duration of the most active player through sessoin path.
 * @param[in] handle    The session path
 * @param[in] mesc      File duration
 * @return Zero on success; a negated errno value on failure.
 */
int media_session_get_duration(void* handle, unsigned int* msec);

/**
 * Set the most active player path volume through sessoin path.
 * @param[in] handle    The session path
 * @param[in] mesc      Volume with range of 0.0 - 1.0
 * @return Zero on success; a negated errno value on failure.
 */
int media_session_set_volume(void* handle, float volume);

/**
 * Get the most active player path volume through sessoin path.
 * @param[in] handle    The session path
 * @param[in] mesc      Volume with range of 0.0 - 1.0
 * @return Zero on success; a negated errno value on failure.
 */
int media_session_get_volume(void* handle, float* volume);

/**
 * Play previous song in player list through sessoin path.
 * @param[in] handle    The session path.
 * @return Zero on success; a negated errno value on failure.
 * @note media framework has no player list, such function shall be
 * implemented by player.
 */
int media_session_prev_song(void* handle);

/**
 * Play next song in player list through sessoin path.
 * @param[in] handle    The session path.
 * @return Zero on success; a negated errno value on failure.
 * @note media framework has no player list, such function shall be
 * implemented by player.
 */
int media_session_next_song(void* handle);

#undef EXTERN
#ifdef __cplusplus
}
#endif
#endif /* FRAMEWORKS_MEDIA_INCLUDE_MEDIA_SESSION_H */
