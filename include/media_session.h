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
 * @brief Open a session controller.
 *
 * @param[in] params    NULL, Not used yet.
 * @return void*    Controller handle, NULL on error.
 *
 * @note The control messages sent from controller is always passed to
 * the most active controlee.
 *                                                +---------------+
 * +------------+                                 | Media Session |
 * |            | start, pause, stop -------------+---+           |
 * | Controller |                                 |   |           |
 * |            | on_event() <----- MEDIA_EVENT_* +-+ |           |
 * +------------+                                 | | |           |
 *                                                | | |           |
 * +------------+                                 | | |           |
 * |            | --------------------------------+-+ |           |
 * | Controllee |                                 |   |           |
 * |            | <-------------------------------+---+           |
 * +------------+                                 +---------------+
 */
void* media_session_open(const char* params);

/**
 * @brief Close the session controller.
 *
 * @param[in] handle    Controller handle
 * @return int  Zero on success; a negative errno value on failure.
 */
int media_session_close(void* handle);

/**
 * @brief Set event callback to receive message from controllee.
 *
 * @param[in] handle    Controller handle
 * @param[in] cookie    Callback argument for `on_event`.
 * @param[out] on_event Callback to receive events.
 * @return Zero on success; a negative errno value on failure.
 *
 * @code
 *  void user_on_event(void* cookie, int event, int result, cosnt char* extra) {
 *      switch (event) {
 *      case MEDIA_EVENT_CHANGED:
 *          // The most active controllee changed, you should clear
 *          // the metadata you queried, and query again (if you cares).
 *          break;
 *
 *      case MEDIA_EVENT_UPDATED:
 *          // The most active controllee updated its metadata, also
 *          // you should query again (if you cares).
 *          break;
 *
 *      case MEDIA_EVENT_START:
 *      case MEDIA_EVENT_PAUSE:
 *      case MEDIA_EVENT_STOP:
 *      case MEDIA_EVENT_PREV_SONG:
 *      case MEDIA_EVENT_NEXT_SONG:
 *      case MEDIA_EVENT_INCREASE_VOLUME:
 *      case MEDIA_EVENT_DECREASE_VOLUME:
 *          // These are the process result of your control message,
 *          // sent by controllee.
 *          break;
 *      }
 *  }
 * @endcode
 */
int media_session_set_event_callback(void* handle, void* cookie,
    media_event_callback on_event);

/**
 * @brief Request start.
 *
 * @param[in] handle    Controller handle.
 * @return int  Zero on success; a negative errno value on failure.
 */
int media_session_start(void* handle);

/**
 * @brief Request stop.
 *
 * @param[in] handle    Controller handle.
 * @return int  Zero on success; a negative errno value on failure.
 */
int media_session_stop(void* handle);

/**
 * @brief Request pause.
 *
 * @param[in] handle    Controller handle.
 * @return int  Zero on success; a negative errno value on failure.
 */
int media_session_pause(void* handle);

/**
 * @brief Request seek.
 *
 * @param[in] handle    Controller handle.
 * @param[in] position  The msec position from beginning.
 * @return int  Zero on success; a negative errno value on failure.
 *
 * @warning not implement yet.
 */
int media_session_seek(void* handle, unsigned position);

/**
 * @brief Request play previous song.
 *
 * @param[in] handle    Controller handle.
 * @return Zero on success; a negative errno value on failure.
 */
int media_session_prev_song(void* handle);

/**
 * @brief Request play next song.
 *
 * @param[in] handle    Controller handle.
 * @return Zero on success; a negative errno value on failure.
 */
int media_session_next_song(void* handle);

/**
 * @brief Request increase volume.
 *
 * @param[in] handle    Controller handle.
 * @return int  Zero on success; a negative errno value on failure.
 */
int media_session_increase_volume(void* handle);

/**
 * @brief Request decrease volume.
 *
 * @param[in] handle    Controller handle.
 * @return int  Zero on success; a negative errno value on failure.
 */
int media_session_decrease_volume(void* handle);

/**
 * @brief Rquest set volume.
 *
 * @param[in] handle    Controller handle.
 * @param[in] volume    Volume index.
 * @return Zero on success; a negative errno value on failure.
 *
 * @warning not implement yet.
 */
int media_session_set_volume(void* handle, int volume);

/**
 * @brief Query metadata from most active controllee.
 *
 * @param[in] handle    Controller handle.
 * @param[out] data     Pointer to receive metadata ptr.
 * @return Zero on success; a negative errno value on failure.
 *
 * @note Each controller handle has unique media_metadata_s;
 * This api only update the content, won't changing address of metadata.
 *
 * @code
 *  const media_metadata_t* data = NULL;
 *  ret = media_session_query(handle, &data);
 * @endcode
 */
int media_session_query(void* handle, const media_metadata_t** data);

/**
 * @brief Get all status.
 *
 * @param[in] handle    Controller handle.
 * @param[out] state    Current state.
 * @return Zero on success; a negative errno value on failure.
 *
 * @note use `media_session_query` instead if you want full message.
 */
int media_session_get_state(void* handle, int* state);

/**
 * @brief Get msec position.
 *
 * @param[in] handle    Controller handle.
 * @param[out] position Current msec position.
 * @return Zero on success; a negative errno value on failure.
 *
 * @note use `media_session_query` instead if you want full message.
 */
int media_session_get_position(void* handle, unsigned* position);

/**
 * @brief Get msec duration.
 *
 * @param[in] handle    Controller handle.
 * @param[out] duration Current msec duration.
 * @return Zero on success; a negative errno value on failure.
 *
 * @note use `media_session_query` instead if you want full message.
 */
int media_session_get_duration(void* handle, unsigned* duration);

/**
 * @brief Get current volume index.
 *
 * @param[in] handle    Controller handle.
 * @param[out] volume   Volume index.
 * @return Zero on success; a negative errno value on failure.
 *
 * @note use `media_session_query` instead if you want full message.
 */
int media_session_get_volume(void* handle, int* volume);

/**
 * @brief Register as a session controllee.
 *
 * @param[in] cookie    Callback arguemnt of `on_event`.
 * @param[out] on_event Event callback.
 * @return void*    Controllee handle, NULL on failure.
 *
 * @note Only the most active controllee would receive control message
 * and notify result from&to controllers;
 * If there are many controllees and you want to become the most active one,
 * just call update your metadata and set state > 0.
 *
 *                                                +---------------+
 * +------------+                                 | Media Session |
 * |            | --------------------------------+---+           |
 * | Controller |                                 |   |           |
 * |            | <-------------------------------+-+ |           |
 * +------------+                                 | | |           |
 *                                                | | |           |
 * +------------+                                 | | |           |
 * |            | notify, update -----------------+-+ |           |
 * | Controllee |                                 |   |           |
 * |            | on_event() <----- MEDIA_EVENT_* +---+           |
 * +------------+                                 +---------------+
 */
void* media_session_register(void* cookie, media_event_callback on_event);

/**
 * @brief Unregister the session controllee.
 *
 * @param[in] handle    Controllee handle.
 * @return int  Zero on success; a negative errno value on failure.
 */
int media_session_unregister(void* handle);

/**
 * @brief Notify the result of control message.
 *
 * After receive MEDIA_EVENT_* from `on_event`, as controllee you should
 * do something to handle the control message, after you acknowledge the
 * control message, you should call this api to send response to the controller.
 *
 * @param[in] handle    Controllee handle.
 * @param[in] event     MEDIA_EVENT_*
 * @param[in] result    Result, 0 on succes, negative errno on error.
 * @param[in] extra     Extra message.
 * @return int  Zero on success; a negative errno value on failure.
 */
int media_session_notify(void* handle, int event, int result, const char* extra);

/**
 * @brief Update metadata to session.
 *
 * @param[in] handle    Controllee handle.
 * @param[in] data      Metadata to update.
 * @return int  Zero on success; a negative errno value on failure.
 */
int media_session_update(void* handle, const media_metadata_t* data);

#ifdef CONFIG_LIBUV
/**
 * @brief Open an async session controller.
 *
 * @param[in] loop      Loop handle of current thread.
 * @param[in] params    Not used yet.
 * @param[out] on_open  Open callback, called after open is done.
 * @param[in] cookie    Long-term callback context for:
 *                      on_open, on_event, on_close.
 * @return void*    Async controller handle.
 *
 * @note This is async version of `media_session_open`, see this
 * API for more information about media session mechanism.
 *
 * @note Even if `on_open` has not been called, you can call other
 * handle-based async APIs, and they will be queued inside the
 * player handle as requests, and processed in order after
 * `open` is completed.
 *
 * @code example1.
 *  // 1. open a session controller instance.
 *  ctx->handle = media_uv_session_open(loop, NULL, NULL, ctx);
 *
 *  // 2. request playing (delay till open is done).
 *  media_uv_session_start(ctx->handle, NULL, NULL);
 * @endcode example1.
 *
 * @code example2.
 *  void user_on_open(void* cookie, int ret) {
 *      UserContext* ctx = cookie;
 *
 *      if (ret < 0)
 *          media_uv_player_close(ctx->handle, user_on_close);
 *      else
 *          media_uv_session_start(ctx->handle, user_on_start, ctx);
 *  }
 *
 *  ctx->handle = media_uv_session_open(loop, MEDIA_STREAM_MUSIC,
 *      user_on_open, ctx);
 * @endcode example2.
 */
void* media_uv_session_open(void* loop, char* params,
    media_uv_callback on_open, void* cookie);

/**
 * @brief Close the async controller handle.
 *
 * @param[in] handle    Async controller handle. to be destroyed.
 * @param[out] on_close Close callback, called after close is done.
 * @return Zero on success; a negative errno value on failure.
 *
 * @note It's safe to call this api before `on_open` of `media_uv_session_open`
 * is called, but other api calls would be canceled, there `on_xxx` callback
 * would be called with ret == -ECANCELD.
 */
int media_uv_session_close(void* handle, media_uv_callback on_close);

/**
 * @brief Listen to the events from controllee.
 *
 * @param[in] handle    Async Controller handle.
 * @param[out] on_event Event callback.
 * @return int  Zero on success; a negative errno value on failure.
 *
 * @note This is async version of `media_session_set_event_callback`,
 * see this API for more information about media session mechanism.
 */
int media_uv_session_listen(void* handle, media_event_callback on_event);

/**
 * @brief Request start.
 *
 * @param[in] handle    Async Controller handle.
 * @param[out] on_start Call after receiving result.
 * @param[in] cookie    Callback argument of `on_start`
 * @return Zero on success; a negative errno value on failure.
 */
int media_uv_session_start(void* handle, media_uv_callback on_start,
    void* cookie);

/**
 * @brief Request stop.
 *
 * @param[in] handle    Async Controller handle.
 * @param[out] on_stop  Call after receiving result.
 * @param[in] cookie    Callback argument of `on_stop`
 * @return Zero on success; a negative errno value on failure.
 */
int media_uv_session_stop(void* handle, media_uv_callback on_stop,
    void* cookie);

/**
 * @brief Request pause.
 *
 * @param[in] handle    Async Controller handle.
 * @param[out] on_pause Call after receiving result.
 * @param[in] cookie    Callback argument of `on_pause`
 * @return Zero on success; a negative errno value on failure.
 */
int media_uv_session_pause(void* handle, media_uv_callback on_pause, void* cookie);

/**
 * @brief Request seek.
 *
 * @param[in] handle    The player path
 * @param[in] position  The msec position from beginning.
 * @param[out] on_seek  Call after receiving result.
 * @param[in] cookie    Callback argument of `on_seek`
 * @return Zero on success; a negative errno value on failure.
 */
int media_uv_session_seek(void* handle, unsigned position,
    media_uv_callback on_seek, void* cookie);

/**
 * @brief Request play previous song.
 *
 * @param[in] handle    Async Controller handle.
 * @param[out] on_prev  Call after receiving result.
 * @param[in] cookie    Callback argument of `on_prev`
 * @return Zero on success; a negative errno value on failure.
 */
int media_uv_session_prev_song(void* handle,
    media_uv_callback on_pre_song, void* cookie);

/**
 * @brief Request play next song.
 *
 * @param[in] handle    Async Controller handle.
 * @param[out] on_next  Call after receiving result.
 * @param[in] cookie    Callback argument of `on_next`
 * @return Zero on success; a negative errno value on failure.
 */
int media_uv_session_next_song(void* handle,
    media_uv_callback on_next, void* cookie);

/**
 * @brief Request increase volume.
 *
 * @param[in] handle        Async controller handle.
 * @param[out] on_increase  Call after receiving result.
 * @param[in] cookie        Callback argument of `on_increase`
 * @return Zero on success; a negative errno value on failure.
 */
int media_uv_session_increase_volume(void* handle,
    media_uv_callback on_increase, void* cookie);

/**
 * @brief Request decrease volume.
 *
 * @param[in] handle        Async controller handle.
 * @param[out] on_decrease  Call after receiving result.
 * @param[in] cookie        Callback argument of `on_decrease`
 * @return Zero on success; a negative errno value on failure.
 */
int media_uv_session_decrease_volume(void* handle,
    media_uv_callback on_decrease, void* cookie);

/**
 * @brief Request set volume.
 *
 * @param[in] handle        Async controller handle.
 * @param[in] Volume        Volume index.
 * @param[out] on_volume    Call after receiving result.
 * @param[in] cookie        Callback argument of `on_volume`
 * @return Zero on success; a negative errno value on failure.
 *
 * @warning Not impletement yet.
 */
int media_uv_session_set_volume(void* handle, int volume,
    media_uv_callback on_set_volume, void* cookie);

/**
 * @brief Query full status.
 *
 * @param[in] handle    Async controller handle.
 * @param[out] on_query Callback to receive metadata ptr.
 * @param[in] cookie    Callback argument.
 * @return Zero on success; a negative errno value on failure.
 *
 * @note Each controller handle has unique media_metadata_s;
 * This api only update the content, won't changing address of metadata.
 *
 * @code
 *  void on_query(void* cookie, int ret, void* object) {
 *      const media_metadata_t* data = object;
 *      // ...
 *  }
 *
 *  ret = media_uv_session_query(handle, on_query, cookie);
 * @endcode
 */
int media_uv_session_query(void* handle,
    media_uv_object_callback on_query, void* cookie);

/**
 * @brief Get current state.
 *
 * @param[in] handle    Async Controller handle.
 * @param[out] on_state Call after receiving result.
 * @param[in] cookie    Callback argument of `on_state`
 * @return Zero on success; a negative errno value on failure.
 *
 * @warning not implemented yet, use `media_uv_session_query` instead.
 */
int media_uv_session_get_state(void* handle,
    media_uv_int_callback on_state, void* cookie);

/**
 * @brief Get current position.
 *
 * @param[in] handle        Async controller handle.
 * @param[out] on_position  Call after receiving result.
 * @param[in] cookie        Callback argument of `on_position`
 * @return Zero on success; a negative errno value on failure.
 *
 * @warning not implemented yet, use `media_uv_session_query` instead.
 */
int media_uv_session_get_position(void* handle,
    media_uv_unsigned_callback on_position, void* cookie);

/**
 * @brief Get current duration.
 *
 * @param[in] handle        Async controller handle.
 * @param[out] on_duration  Call after receiving result.
 * @param[in] cookie        Callback argument of `on_duration`
 * @return Zero on success; a negative errno value on failure.
 *
 * @warning not implemented yet, use `media_uv_session_query` instead.
 */
int media_uv_session_get_duration(void* handle,
    media_uv_unsigned_callback on_duration, void* cookie);

/**
 * @brief Get current volume.
 *
 * @param[in] handle        Async controller handle.
 * @param[out] on_volume    Call after receiving result.
 * @param[in] cookie        Callback argument of `on_volume`
 * @return Zero on success; a negative errno value on failure.
 *
 * @warning not implemented yet, use `media_uv_session_query` instead.
 */
int media_uv_session_get_volume(void* handle,
    media_uv_int_callback on_get_volume, void* cookie);

/**
 * @brief Register as a session controllee to receive control message.
 *
 * @param[in] loop      Loop handle of current thread.
 * @param[in] params    NULL, not used yet.
 * @param[out] on_event Callback to receive control message.
 * @param[in] cookie    Callback argument.
 * @return void*    Async controlee handle, NULL on error.
 *
 * @note This is async version of `media_session_register`, see this
 * API for more information about media session mechanism.
 */
void* media_uv_session_register(void* loop, const char* params,
    media_event_callback on_event, void* cookie);

/**
 * @brief Unregister self.
 *
 * @param[in] handle        Async controllee handle..
 * @param[out] on_release   Callabck to release caller's own resources.
 * @return int  Zero on success, negative errno on failure.
 */
int media_uv_session_unregister(void* handle, media_uv_callback on_release);

/**
 * @brief  Notify the result of control message.
 *
 * After receive MEDIA_EVENT_* from `on_event`, as controllee you should
 * do something to handle the control message, after you acknowledge the
 * control message, you should call this api to send response to the controller.
 *
 * @param[in] handle    Async controllee handle.
 * @param[in] event     Event to notify.
 * @param[in] result    Result of event, usually zero on success, negative errno on failure.
 * @param[in] extra     Extra string message of event, NULL if not need.
 * @param[out] on_notify Callback to acknowledge notify is done.
 * @param[in] cookie    Callback argument.
 * @return int  Zero on success, negative errno on failure.
 */
int media_uv_session_notify(void* handle, int event, int result, const char* extra,
    media_uv_callback on_notify, void* cookie);

/**
 * @brief Update metadata to session.
 *
 * @param[in] handle        Async controllee handle.
 * @param[in] data          Metadata to update.
 * @param[out] on_update    Callback to acknowledge update is done.
 * @param[in] cookie        Callback argument.
 * @return int  Zero on success, negative errno on failure.
 */
int media_uv_session_update(void* handle, const media_metadata_t* data,
    media_uv_callback on_update, void* cookie);
#endif /* CONFIG_LIBUV */

#undef EXTERN
#ifdef __cplusplus
}
#endif
#endif /* FRAMEWORKS_MEDIA_INCLUDE_MEDIA_SESSION_H */
