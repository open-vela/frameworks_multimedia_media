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
 * Public Controller Functions
 ****************************************************************************/

/**
 * Open a session path as controller.
 * @param[in] params    Stream type, use MEDIA_STREAM_*.
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
 * Seek to msec position from begining
 * @param[in] handle    The player path
 * @param[in] msec      Which postion should seek from begining
 * @return Zero on success; a negated errno value on failure.
 */
int media_session_seek(void* handle, unsigned int msec);

/**
 * Get current player state through sessoin path.
 * @param[in] handle    The session path.
 * @param[out] state    Current state.
 * @return Zero on success; a negated errno value on failure.
 */
int media_session_get_state(void* handle, int* state);

/**
 * Get playback position of the most active player through sessoin path.
 * @param[in] handle    The session path
 * @param[in] msec      Playback position (from begining)
 * @return Zero on success; a negated errno value on failure.
 */
int media_session_get_position(void* handle, unsigned int* msec);

/**
 * Get playback file duration of the most active player through sessoin path.
 * @param[in] handle    The session path
 * @param[in] msec      File duration
 * @return Zero on success; a negated errno value on failure.
 */
int media_session_get_duration(void* handle, unsigned int* msec);

/**
 * Set the most active player path volume through sessoin path.
 * @param[in] handle    The session path
 * @param[in] volume    Volume index of stream type, with range of 1 - 10
 * @return Zero on success; a negated errno value on failure.
 */
int media_session_set_volume(void* handle, int volume);

/**
 * Get the most active player path volume through sessoin path.
 * @param[in] handle    The session path
 * @param[in] volume    Volume index of stream type, with range of 1 - 10
 * @return Zero on success; a negated errno value on failure.
 */
int media_session_get_volume(void* handle, int* volume);

/**
 * Increase the most active player path volume through sessoin path.
 * @param[in] handle    The session path
 * @return Zero on success; a negated errno value on failure.
 */
int media_session_increase_volume(void* handle);

/**
 * Decrease the most active player path volume through sessoin path.
 * @param[in] handle    The session path
 * @return Zero on success; a negated errno value on failure.
 */
int media_session_decrease_volume(void* handle);

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

/****************************************************************************
 * Public Controllee Functions
 ****************************************************************************/

/**
 * Register a session path as player(controllee).
 * @param[in] cookie    User cookie, will bring back to user when do event_cb.
 * @param[in] event_cb  Event callback.
 * @return Pointer to created handle or NULL on failure.
 */
void* media_session_register(void* cookie, media_event_callback event_cb);

/**
 * Unregister the session path.
 * @param[in] handle    The session path to be destroyed.
 * @return Zero on success; a negated errno value on failure.
 */
int media_session_unregister(void* handle);

/**
 * Report event to media sessions as player(controllee).
 * @note Should only report event that media framework cannot know,
 * such as MEDIA_EVENT_DONENEXT.
 * @param[in] handle    The session path.
 * @param[in] event     Event, use MEDIA_EVENT_*.
 * @param[in] result    Exec result.
 * @param[in] extra     Extra message.
 * @return Zero on success; a negated errno value on failure.
 */
int media_session_notify(void* handle, int event,
    int result, const char* extra);

#ifdef CONFIG_LIBUV

/**
 * Open a session path as controller.
 * @param[in] loop      Loop handle of current thread.
 * @param[in] params    Not used yet.
 * @param[in] on_open   Open callback, called after open is done.
 * @param[in] cookie    Long-term callback context for:
 *         on_open, on_event, on_close.
 * @return void* Handle of Controller.
 */
void* media_uv_session_open(void* loop, char* params,
    media_uv_callback on_open, void* cookie);

/**
 * Close the session path.
 * @param[in] handle    The session path to be destroyed.
 * @param[in] on_close  Close callback, called after close is done.
 * @return Zero on success; a negated errno value on failure.
 */
int media_uv_session_close(void* handle, media_uv_callback on_close);

/**
 * Set event callback to the session path, the callback will be called
 * when state of the most active player changed.
 * @param[in] handle    The session path.
 * @param[in] on_event  Event callback.
 * @return Zero on success; a negated errno value on failure.
 */
int media_uv_session_listen(void* handle, media_event_callback on_event);

/**
 * Start a player through sessoin path.
 * @param[in] handle    The session path.
 * @param[in] on_start  Call after receiving result.
 * @param[in] cookie    One-time callback context.
 * @return Zero on success; a negated errno value on failure.
 */
int media_uv_session_start(void* handle, media_uv_callback on_start,
    void* cookie);

/**
 * Stop the most active player through sessoin path.
 * @param[in] handle    The session path.
 * @param[in] on_stop   Call after receiving result.
 * @param[in] cookie    One-time callback context.
 * @return Zero on success; a negated errno value on failure.
 */
int media_uv_session_stop(void* handle, media_uv_callback on_stop,
    void* cookie);

/**
 * Pause the most active player through sessoin path.
 * @param[in] handle    The session path.
 * @param[in] on_pause  Call after receiving result.
 * @param[in] cookie    One-time callback context.
 * @return Zero on success; a negated errno value on failure.
 */
int media_uv_session_pause(void* handle, media_uv_callback on_pause, void* cookie);

/**
 * Seek to msec position from begining
 * @param[in] handle    The player path
 * @param[in] msec      Which postion should seek from begining
 * @param[in] on_seek   Call after receiving result.
 * @param[in] cookie    One-time callback context.
 * @return Zero on success; a negated errno value on failure.
 */
int media_uv_session_seek(void* handle, unsigned int msec,
    media_uv_callback on_seek, void* cookie);

/**
 * Get current player state through sessoin path.
 * @param[in] handle    The session path.
 * @param[in] on_state  Call after receiving result.
 * @param[in] cookie    One-time callback context.
 * @return Zero on success; a negated errno value on failure.
 */
int media_uv_session_get_state(void* handle,
    media_uv_int_callback on_state, void* cookie);

/**
 * Get playback position of the most active player through sessoin path.
 * @param[in] handle       The session path
 * @param[in] on_position  Call after receiving result.
 * @param[in] cookie       One-time callback context.
 * @return Zero on success; a negated errno value on failure.
 */
int media_uv_session_get_position(void* handle,
    media_uv_int_callback on_position, void* cookie);

/**
 * Get playback file duration of the most active player through sessoin path.
 * @param[in] handle        The session path
 * @param[in] on_duriation  Call after receiving result.
 * @param[in] cookie        One-time callback context.
 * @return Zero on success; a negated errno value on failure.
 */
int media_uv_session_get_duration(void* handle,
    media_uv_int_callback on_duriation, void* cookie);

/**
 * Set the most active player path volume through sessoin path.
 * @param[in] handle        The session path
 * @param[in] Volume        Volume index of stream type, with range of 1 - 10
 * @param[in] on_set_volume Call after receiving result.
 * @param[in] cookie        One-time callback context.
 * @return Zero on success; a negated errno value on failure.
 */
int media_uv_session_set_volume(void* handle, unsigned int* volume,
    media_uv_callback on_set_volume, void* cookie);

/**
 * Get the most active player path volume through sessoin path.
 * @param[in] handle        The session path
 * @param[in] on_get_volume Call after receiving result.
 * @param[in] cookie        One-time callback context.
 * @return Zero on success; a negated errno value on failure.
 */
int media_uv_session_get_volume(void* handle,
    media_uv_int_callback on_get_volume, void* cookie);

/**
 * Increase the most active player path volume through sessoin path.
 * @param[in] handle      The session path
 * @param[in] on_increase Call after receiving result.
 * @param[in] cookie      One-time callback context.
 * @return Zero on success; a negated errno value on failure.
 */
int media_uv_session_increase_volume(void* handle, media_uv_callback on_increase,
    void* cookie);

/**
 * Decrease the most active player path volume through sessoin path.
 * @param[in] handle      The session path
 * @param[in] on_decrease Call after receiving result.
 * @param[in] cookie      One-time callback context.
 * @return Zero on success; a negated errno value on failure.
 */
int media_uv_session_decrease_volume(void* handle, media_uv_callback on_decrease,
    void* cookie);

/**
 * Play previous song in player list through sessoin path.
 * @param[in] handle      The session path.
 * @param[in] on_pre_song Call after receiving result.
 * @param[in] cookie      One-time callback context.
 * @return Zero on success; a negated errno value on failure.
 * @note media framework has no player list, such function shall be
 * implemented by player.
 */
int media_uv_session_prev_song(void* handle,
    media_uv_callback on_pre_song, void* cookie);

/**
 * Play next song in player list through sessoin path.
 * @param[in] handle       The session path.
 * @param[in] on_next_song Call after receiving result.
 * @param[in] cookie       One-time callback context.
 * @return Zero on success; a negated errno value on failure.
 * @note media framework has no player list, such function shall be
 * implemented by player.
 */
int media_uv_session_next_song(void* handle,
    media_uv_callback on_next_song, void* cookie);

#endif /* CONFIG_LIBUV */
#undef EXTERN
#ifdef __cplusplus
}
#endif
#endif /* FRAMEWORKS_MEDIA_INCLUDE_MEDIA_SESSION_H */
