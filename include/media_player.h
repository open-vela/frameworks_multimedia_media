/****************************************************************************
 * frameworks/media/include/media_player.h
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

#ifndef FRAMEWORKS_MEDIA_INCLUDE_MEDIA_PLAYER_H
#define FRAMEWORKS_MEDIA_INCLUDE_MEDIA_PLAYER_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <media_event.h>
#include <media_stream.h>
#include <stddef.h>
#include <sys/socket.h>

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
 * @brief Open a player path with given stream type.
 *
 * @param[in] stream    MEDIA_STREAM_*.
 *                      Different stream types have different route logic.
 * @return void*    Player handle, NULL on failure.
 *
 * @code To simply play a song you should:
 *  // 1. create a instance.
 *  handle = media_player_open(MEDIA_STREAM_MUSIC);
 *
 *  // 2. prepare the resource you want to play.
 *  ret = media_player_prepare(handle, "/data/1.mp3", NULL);
 *
 *  // 3. start playing.
 *  ret = media_player_start(handle);
 *
 *  // 4. stop playing and clear the prepared song,
 *  //  should goto step2 if you still need this player instance.
 *  ret = media_player_stop(handle);
 *
 *  // 5. don't forget to destroy the instance.
 *  ret = media_player_close(handle, 0);
 * @endcode
 */
void* media_player_open(const char* stream);

/**
 * @brief Close the player path.
 *
 * @param[in] handle        Player handle.
 * @param[in] pending_stop  Whether pending stop before close:
 *                          - 0: stop immediately for closing;
 *                          - 1: stop till complete current playing track.
 * @return int  Zero on success; a negated errno value on failure.
 */
int media_player_close(void* handle, int pending_stop);

/**
 * @brief Set a event callback to listen to stream status change.
 *
 * @param[in] handle        Player handle.
 * @param[in] event_cookie  Callback argument.
 * @param[out] on_event     Callback to receive events about stream status change.
 * @return int  Zero on success; a negated errno value on failure.
 *
 * @code
 *  void user_on_event(void* cookie, int event, int result, const char* extra) {
 *      switch () {
 *      case MEDIA_EVENT_PREPARED:
 *      case MEDIA_EVENT_STARTED:
 *      case MEDIA_EVENT_PAUSED:
 *      case MEDIA_EVENT_STOPPED:
 *      case MEDIA_EVENT_SEEKED:
 *      case MEDIA_EVENT_COMPLETED:
 *      }
 *  }
 * @endcode
 */
int media_player_set_event_callback(void* handle, void* event_cookie,
    media_event_callback on_event);

/**
 * @brief Prepare resource to play.
 *
 * @param[in] handle    Player handle.
 * @param[in] url       Path of resource, there is 2 mode:
 *                      1. URL: `url` is Local file path or Network address;
 *                          media framework would read the resource and play.
 *                      2. BUFFER: `url` is NULL, so caller should continuously
 *                          provide buffers to media framework for playing;
 *                          there are 2 ways to provide buffers,
 *                              1. Use `media_player_write_data()`.
 *                              2. Use `media_player_get_socket()` and `write()`
 * @param[in] options   Extra options about the resource, usually it's key-value pairs
 *                      to describe format of resource.
 *                      (e.g. "format=s16le,sample_rate=44100,channels=2")
 * @return int  Zero on success; a negated errno value on failure.
 *
 * @note The api mentioned for buffer mode are all thread-safe.
 */
int media_player_prepare(void* handle, const char* url, const char* options);

/**
 * @brief Reset media with player type.
 * @param[in] handle The player path, return value of media_player_open
 * function.
 * @return Zero on success; a negated errno value on failure.
 */

/**
 * @brief Reset the player path.
 *
 * @param[in] handle    Player handle.
 * @return int  Zero on success; a negated errno value on failure.
 *
 * @note This api is similar to `media_player_stop`.
 */
int media_player_reset(void* handle);

/**
 * @brief Write data to media for playing.
 *
 * @param[in] handle    Player handle.
 * @param[in] data      Buffer address.
 * @param[in] len       Buffer length to write.
 * @return ssize_t  Sent length on success; a negated errno value on failure.
 *
 * @note This api is thread-safe, in buffer mode, you might need this
 * in your worker thread.
 */
ssize_t media_player_write_data(void* handle, const void* data, size_t len);

/**
 * @brief Get socket address info for buffer mode.
 *
 * @param[in] handle    Player handle
 * @param[out] addr     Socket address info
 * @return int  Zero on success; a negated errno value on failure.
 */
int media_player_get_sockaddr(void* handle, struct sockaddr_storage* addr);

/**
 * @brief Get socket fd for writing.
 *
 * @param[in] handle    Player handle.
 * @return int  Socket fd, negative errno on failure.
 *
 * @note This api is thread-safe, in buffer mode, you can get socket fd
 * for data transaction in your worker thread.
 */
int media_player_get_socket(void* handle);

/**
 * @brief Close socket fd.
 *
 * @param[in] handle    Player handle.
 *
 * @note This api is thread-safe, in buffer mode, you can finalize data
 * transaction by this api in your worker thread.
 */
void media_player_close_socket(void* handle);

/**
 * @brief Start/resume playing the resource.
 *
 * @param[in] handle    Player handle.
 * @return int  Zero on success; a negated errno value on failure.
 *
 * @note This api would generates MEDIA_EVENT_STARTED event.
 */
int media_player_start(void* handle);

/**
 * @brief Stop and clear the resource.
 *
 * @param[in] handle    Player handle.
 * @return int  Zero on success; a negated errno value on failure.
 *
 * @note This api would generates MEDIA_EVENT_STOPPED event.
 */
int media_player_stop(void* handle);

/**
 * @brief Pause.
 *
 * @param[in] handle    Player handle.
 * @return int  Zero on success; a negated errno value on failure.
 *
 * @note This api would generates MEDIA_EVENT_PAUSED event.
 */
int media_player_pause(void* handle);

/**
 * @brief Seek to msec position from begining.
 *
 * @param[in] handle    Player handle.
 * @param[in] position  Position with msec unit.
 * @return int  Zero on success; a negated errno value on failure.
 *
 * @note This api would generates MEDIA_EVENT_SEEKED event.
 */
int media_player_seek(void* handle, unsigned int position);

/**
 * @brief Set loop times.
 *
 * @param[in] handle    Player handle.
 * @param[in] loop      Loop times, -1 for infinite loop.
 * @return int  Zero on success; a negated errno value on failure.
 */
int media_player_set_looping(void* handle, int loop);

/**
 * @brief Check playing status.
 *
 * @param[in] handle    Player handle.
 * @return int  Positive on playing, zero on inactive, negative on error.
 *
 * @note If you monitor the MEDIA_EVENT_STARTED event and result from
 * event callback, this api is not needed.
 */
int media_player_is_playing(void* handle);

/**
 * @brief Gert current msec position of resource.
 *
 * @param[in] handle    Player handle.
 * @param[in] position  Msec position.
 * @return int  Zero on success; a negated errno value on failure.
 */
int media_player_get_position(void* handle, unsigned int* position);

/**
 * @brief Gert msec duration of current resource.
 *
 * @param[in] handle    Player handle.
 * @param[in] duration  Position in msec.
 * @return int  Zero on success; a negated errno value on failure.
 */
int media_player_get_duration(void* handle, unsigned int* duration);

/**
 * @brief Set volume.
 *
 * @param[in] handle    Player handle.
 * @param[in] volume    Volume in range [0.0, 1.0].
 * @return int  Zero on success; a negated errno value on failure.
 *
 * @warning You should always use `meida_policy_set_stream_volume`
 * if possible; if you use this api to adjust your volume, you
 * can not align with stream volume policy configuration.
 */
int media_player_set_volume(void* handle, float volume);

/**
 * @brief Get volume.
 *
 * @param[in] handle    Player handle.
 * @param[out] volume   Volume in range [0.0, 1.0].
 * @return int  Zero on success; a negated errno value on failure.
 */
int media_player_get_volume(void* handle, float* volume);

/**
 * @brief Set properties.
 *
 * @param[in] handle      Current player path
 * @param[in] target      Target filter
 * @param[in] key         Key
 * @param[in] value       Value
 * @return int  Zero on success; a negated errno value on failure.
 */
int media_player_set_property(void* handle, const char* target,
    const char* key, const char* value);

/**
 * @brief Get properties.
 *
 * @param[in] handle      Current player path
 * @param[in] target      Target filter
 * @param[in] key         Key
 * @param[out] value      Buffer of value
 * @param[in] value_len   Buffer length of value
 * @return int  Zero on success; a negated errno value on failure.
 */
int media_player_get_property(void* handle, const char* target,
    const char* key, char* value, int value_len);

#ifdef CONFIG_LIBUV
/**
 * @brief Open an async player with given stream type.
 *
 * @param[in] loop          Loop handle of current thread.
 * @param[in] stream        MEDIA_STREAM_* .
 *                          Different stream types have different route logic.
 * @param[out] on_open      Open callback, called after open is done.
 * @param[in] cookie        Long-term callback context for:
 *                          on_open, on_event, on_connection, on_close.
 * @return void*    Handle of player, NULL on error.
 *
 * @note Even if `on_open` has not been called, you can call other
 * handle-based async APIs, and they will be queued inside the
 * player handle as requests, and processed in order after
 * `open` is completed.
 *
 * @code example1.
 *  // 1. open a player instance.
 *  ctx->handle = media_uv_player_open(loop, MEDIA_STREAM_MUSIC, NULL, ctx);
 *
 *  // 2. prepare a song (delay till open is done).
 *  media_uv_player_prepare(ctx->handle, "/data/1.mp3", NULL, NULL);
 *
 *  // 3. start playing (delay till prepare is done).
 *  media_uv_player_start(ctx->handle, NULL, NULL);
 * @endcode example1.
 *
 * @code example2.
 *  void user_on_open(void* cookie, int ret) {
 *      UserContext* ctx = cookie;
 *
 *      if (ret < 0)
 *          media_uv_player_close(ctx->handle, user_on_close);
 *      else
 *          media_uv_player_prepare(ctx->handle, "/data/1.mp3",
 *              user_on_prepare, ctx);
 *  }
 *
 *  ctx->handle = media_uv_player_open(loop, MEDIA_STREAM_MUSIC,
 *      user_on_open, ctx);
 * @endcode example2.
 */
void* media_uv_player_open(void* loop, const char* stream,
    media_uv_callback on_open, void* cookie);

/**
 * @brief Listen to status change event by setting callback.
 *
 * @param[in] handle    Async player handle.
 * @param[out] on_event Event callback, call after receiving notification.
 * @return int  Zero on success, negative errno on failure.
 */
int media_uv_player_listen(void* handle, media_event_callback on_event);

/**
 * @brief Close the async player.
 *
 * @param[in] handle    Async player handle.
 * @param[in] pending   Pending close or not.
 * @param[out] on_close Release callback, called after releasing internal resources.
 * @return int  Zero on success, negative errno on illegal handle.
 *
 * @note It's safe to call this api before `on_open` of `media_uv_player_open`
 * is called, but other api calls would be canceled, there `on_xxx` callback
 * would be called with ret == -ECANCELD.
 */
int media_uv_player_close(void* handle, int pending,
    media_uv_callback on_close);

/**
 * @brief Prepare resource for playing.
 *
 * @param[in] handle            Async player handle.
 * @param[in] url               Path of resources, details @see media_player_prpare.
 * @param[in] options           Resource options, @see media_player_prpare.
 * @param[out] on_connection    Callback to receive uv_pipe_t in buffer mode.
 * @param[out] on_prepare       Callback to receive result.
 * @param[in] cookie            Callback argument for `on_prepare`.
 * @return int Zero on success, negative errno on failure.
 *
 * @note For buffer mode, see `media_player_prepare` for more informations.
 */
int media_uv_player_prepare(void* handle, const char* url, const char* options,
    media_uv_object_callback on_connection, media_uv_callback on_prepare, void* cookie);

/**
 * @brief Reset player.
 *
 * @param[in] handle    Async player handle.
 * @param[out] on_reset Call after receiving result.
 * @param[in] cookie    Callback argument for `on_reset`.
 * @return int  Zero on success; a negated errno value on failure.
 */
int media_uv_player_reset(void* handle, media_uv_callback on_reset, void* cookie);

/**
 * @brief  Play or resume the prepared source with auto focus request.
 *
 * @param[in] handle    Async player handle.
 * @param[in] scenario  MEDIA_SCENARIO_* .
 *                      Different scenario have different focus priority.
 * @param[out] on_play  Callback to acknowledge result of request/start.
 * @param[in] cookie    Callback argument for `on_play`.
 * @return int  Zero on success, negative errno on failure.
 */
int media_uv_player_start_auto(void* handle, const char* scenario,
    media_uv_callback on_start, void* cookie);

/**
 * @brief Play/resume the prepared resouce.
 *
 * @param[in] handle    Async player handle.
 * @param[out] on_start Call after receiving result.
 * @param[in] cookie    Callback argument for `on_start`.
 * @return int  Zero on success, negative errno on failure.
 */
int media_uv_player_start(void* handle, media_uv_callback on_start, void* cookie);

/**
 * @brief Pause the playing.
 *
 * @param[in] handle    Async player handle.
 * @param[out] on_pause Call after receiving result.
 * @param[in] cookie    Callback argument for `on_pause`.
 * @return int  Zero on success, negative errno on failure.
 */
int media_uv_player_pause(void* handle, media_uv_callback on_pause, void* cookie);

/**
 * @brief Stop the playing, clear the prepared resource file.
 *
 * @param[in] handle    Async player handle.
 * @param[out] on_stop  Call after receiving result.
 * @param[in] cookie    Callback argument for `on_stop`.
 * @return int  Zero on success, negative errno on failure.
 */
int media_uv_player_stop(void* handle, media_uv_callback on_stop, void* cookie);

/**
 * @brief Set player volume.
 *
 * @param[in] handle        Async player handle.
 * @param[in] volume        Volume in [0.0, 1.0].
 * @param[out] on_volume    Call after receiving result.
 * @param[in] cookie        Callback argument for `on_volume`.
 * @return int Zero on success, negative errno on failure.
 *
 * @warning You should always use `meida_policy_set_stream_volume`
 * if possible; if you use this api to adjust your volume, you
 * can not align with stream volume policy configuration.
 */
int media_uv_player_set_volume(void* handle, float volume,
    media_uv_callback on_volume, void* cookie);

/**
 * @brief Get the player handle volume.
 *
 * @param[in] handle        Async player handle.
 * @param[in] volume        Volume with range of 0.0 - 1.0
 * @param[out] on_volume    Call after receiving result.
 * @param[in] cookie        Callback argument for `on_volume`.
 * @return int  Zero on success; a negated errno value on failure.
 */
int media_uv_player_get_volume(void* handle,
    media_uv_float_callback on_volume, void* cookie);

/**
 * @brief Get current playing status.
 *
 * @param[in] handle        Async player handle.
 * @param[out] on_playing   Call after receiving result.
 * @param[in] cookie        Callback argument for `on_playing`.
 * @return int  Zero on success, negative errno on failure.
 */
int media_uv_player_get_playing(void* handle,
    media_uv_int_callback on_playing, void* cookie);

/**
 * @brief Get current playing position.
 *
 * @param[in] handle        Async player handle.
 * @param[out] on_position  Call after receiving result.
 * @param[in] cookie        Callback argument for `on_position`.
 * @return int  Zero on success, negative errno on failure.
 */
int media_uv_player_get_position(void* handle,
    media_uv_unsigned_callback on_position, void* cookie);

/**
 * @brief Get duration of current playing resource.
 *
 * @param[in] handle        Async player handle.
 * @param[out] on_duration  Call after receiving result.
 * @param[in] cookie        Callback argument for `on_duration`.
 * @return int  Zero on success, negative errno on failure.
 */
int media_uv_player_get_duration(void* handle,
    media_uv_unsigned_callback on_duration, void* cookie);

/**
 * @brief Set the loop times.
 *
 * @param[in] handle        Async player handle.
 * @param[in] loop          Loop times, -1 for infinite loop.
 * @param[out] on_looping   Call after receiving result.
 * @param[in] cookie        Callback argument for `on_looping`.
 * @return int  Zero on success; a negated errno value on failure.
 */
int media_uv_player_set_looping(void* handle, int loop,
    media_uv_callback on_looping, void* cookie);

/**
 * @brief Seek to msec position from begining.
 *
 * @param[in] handle    Async player handle.
 * @param[in] position  Position in msec from begining.
 * @param[out] on_seek  Call after receiving result.
 * @param[in] cookie    Callback argument for `on_seek`.
 * @return int  Zero on success; a negated errno value on failure.
 */
int media_uv_player_seek(void* handle, unsigned int position,
    media_uv_callback on_seek, void* cookie);

/**
 * @brief Set properties of the player handle.
 *
 * @param[in] handle        Async player handle.
 * @param[in] target        Target filter
 * @param[in] key           Key
 * @param[in] value         Value
 * @param[out] on_setprop   Call after receiving result.
 * @param[in] cookie        Callback argument for `on_setprop`.
 * @return int  Zero on success; a negated errno value on failure.
 */
int media_uv_player_set_property(void* handle, const char* target, const char* key,
    const char* value, media_uv_callback on_setprop, void* cookie);

/**
 * @brief Get properties of the player handle.
 *
 * @param[in] handle        Async player handle.
 * @param[in] target        Target filter
 * @param[in] key           Key
 * @param[out] on_getprop   Call after receiving result.
 * @param[in] cookie        Callback argument for `on_getprop`.
 * @return Zero on success; a negated errno value on failure.
 */
int media_uv_player_get_property(void* handle, const char* target, const char* key,
    media_uv_string_callback on_getprop, void* cookie);

/**
 * @brief Query metadata of the player handle.
 *
 * @param[in] handle    Async player handle.
 * @param[out] on_query Callback to receive metadata ptr.
 * @param[in] cookie    Callback argument for `on_query`.
 * @return Zero on success; a negated errno value on failure.
 *
 * @note Each player handle has unique media_metadata_s;
 * This api only update the content, won't changing address of metadata.
 *
 * @code
 *  void on_query(void* cookie, int ret, void* object) {
 *      const media_metadata_t* data = object;
 *      // ...
 *  }
 *
 *  ret = media_uv_player_query(handle, on_query, cookie);
 * @endcode
 */
int media_uv_player_query(void* handle, media_uv_object_callback on_query, void* cookie);
#endif /* CONFIG_LIBUV */

#undef EXTERN
#ifdef __cplusplus
}
#endif
#endif /* FRAMEWORKS_MEDIA_INCLUDE_MEDIA_PLAYER_H */
