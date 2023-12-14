/****************************************************************************
 * frameworks/media/include/media_recorder.h
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

#ifndef FRAMEWORKS_MEDIA_INCLUDE_MEDIA_RECORDER_H
#define FRAMEWORKS_MEDIA_INCLUDE_MEDIA_RECORDER_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <media_defs.h>
#include <stddef.h>
#include <sys/socket.h>

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C" {
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Public Funtions
 ****************************************************************************/

/**
 * @brief Open a recorder path with given source type.
 *
 * @param[in] params    MEDIA_SOURCE_*.
 *                      Usually MEDIA_SOURCE_MIC.
 * @return void*    Recorder handle on success; NULL on failure.
 *
 * @code To simply record file you should:
 *  // 1. create a instance.
 *  handle = media_recorder_open(MEDIA_SOURCE_MIC);
 *
 *  // 2. prepare the source you want to capture.
 *  ret = media_recorder_prepare(handle, "/data/1.opus", options);
 *
 *  // 3. start capturing.
 *  ret = media_recorder_start(handle);
 *
 *  // 4. stop capturing. if you still need this recorder instance, goto step2.
 *  ret = media_recorder_stop(handle);
 *
 *  // 5. don't forget to destroy the instance.
 *  ret = media_recorder_close(handle);
 * @endcode
 */
void* media_recorder_open(const char* params);

/**
 * @brief Close the recorder path.
 *
 * @param[in] handle    Recorder handle
 * @return int Zero on success; a negative errno value on failure.
 */
int media_recorder_close(void* handle);

/**
 * @brief Set event callback to the recorder, the callback will be called
 * when state changed or something user cares.
 *
 * @param[in] handle    The recorder handle
 * @param[in] cookie    User cookie, will be brought back to user
 *                      after calling event_cb and usually be modified
 * @param[in] on_event  Event callback
 * @return int Zero on success; a negative errno value on failure.
 */
int media_recorder_set_event_callback(void* handle, void* cookie,
    media_event_callback event_cb);

/**
 * @brief Prepare the recorder.
 *
 * @param[in] handle    Recorder handle
 * @param[in] url       Path of resource, there is 2 mode:
 *                      - URL: `url` is valid, represent Local file path;
 *                        media framework would open the path and record.
 *                      - BUFFER: `url` is NULL, so caller should continuously
 *                        send buffers to media framework for capturing;
 *                        there are 2 ways to send buffers,
 *                          - Use `media_recorder_read_data()`.
 *                          - Use `media_recorder_get_socket()` and `read()`
 * @param[in] options   Extra configuration
 *                      - format: encapsulation format, such as:opus wav
 *                      - sample_rate: target sampling rate
 *                      - ch_layout: target channel layout
 *                      - b: target bitrate，such as: "23900"
 *                      - vbr: 0: fixed code rate; 1: variable code rate
 *                      - level: encoding complexity. range：0-10. default:10
 *                      (e.g. "format=opusraw:sample_rate=16000:ch_layout=mono:b=32000:
 *                              vbr=0:level=1")
 * @return int Zero on success; a negative errno value on failure.
 */
int media_recorder_prepare(void* handle, const char* url, const char* options);

/**
 * @brief Reset the recorder path.
 *
 * @param[in] handle    Recorder handle.
 * @return int Zero on success; a negative errno value on failure.
 *
 * @note This api is similar to `media_recorder_stop`.
 */
int media_recorder_reset(void* handle);

/**
 * @brief Read recorderd data from recorder.
 *
 * @attention This need media_recorder_prepare() url set to NULL.
 * This function is blocked.
 *
 * @param[in] handle    Recorder handle.
 * @param[in] data      Buffer address.
 * @param[in] len       Buffer length to read.
 * @return Read length on success; a negative errno value on failure.
 *
 * @note This api is thread-safe, in buffer mode, you might need this
 * in your worker thread.
 */
ssize_t media_recorder_read_data(void* handle, void* data, size_t len);

/**
 * @brief Get socket address info for buffer mode.
 *
 * @param[in] handle    Recorder handle.
 * @param[in] addr      Socket address info.
 * @return int Zero on success; a negative errno value on failure.
 *
 * @note This api is thread-safe, in buffer mode, you can get socket fd
 * for data transaction in your worker thread.
 */
int media_recorder_get_sockaddr(void* handle, struct sockaddr_storage* addr);

/**
 * @brief Get socket fd for reading.
 *
 * @param[in] handle    Recorder handle.
 * @return int Socket fd on success; a negative errno value on failure.
 */
int media_recorder_get_socket(void* handle);

/**
 * @brief Close socket fd when recorder finish recving data.
 *
 * @param[in] handle    Recorder handle.
 *
 * @note This api is thread-safe, in buffer mode, you can finalize data
 * transaction by this api in your worker thread.
 */
void media_recorder_close_socket(void* handle);

/**
 * @brief Start/resume the recording the resource.
 *
 * @param[in] handle    Recorder handle.
 * @return int Zero on success; a negative errno value on failure.
 *
 * @note This api would generates MEDIA_EVENT_STARTED event.
 */
int media_recorder_start(void* handle);

/**
 * @brief Pause the recorder after capturing start.
 *
 * @param[in] handle    Recorder handle.
 * @return int Zero on success; a negative errno value on failure.
 *
 * @note This api would generates MEDIA_EVENT_PAUSED event.
 */
int media_recorder_pause(void* handle);

/**
 * @brief Stop the recorder.
 *
 * @param[in] handle    Recorder handle.
 * @return int Zero on success; a negative errno value on failure.
 *
 * @note This api would generates MEDIA_EVENT_STOPPED event.
 */
int media_recorder_stop(void* handle);

/**
 * @brief Set properties of the recorder path.
 *
 * @param[in] handle      Current recorder path.
 * @param[in] target      Target filter.
 * @param[in] key         Key to set.
 * @param[in] value       Value to be set.
 * @return int Zero on success; a negative errno value on failure.
 */
int media_recorder_set_property(void* handle, const char* target, const char* key, const char* value);

/**
 * @brief Get properties of the recorder path.
 *
 * @param[in] handle      Current recorder path.
 * @param[in] target      Target filter.
 * @param[in] key         Key to set.
 * @param[in] value       Buffer of value.
 * @param[in] value_len   Buffer length of value.
 * @return int Zero on success; a negated errno value on failure.
 */
int media_recorder_get_property(void* handle, const char* target, const char* key, char* value, int value_len);

/**
 * @brief Take picture from camera.
 *
 * @attention Camera only.
 *
 * @param[in] params    Camera open path params.
 * @param[in] filename  The store path for new picture.
 * @param[in] number    The number of pictures taken.
 * @param[in] event_cb  The callback to handle state feedback.
 * @param[in] cookie    The private data of user.
 * @return int Zero on success; a negative errno value on failure.
 */
int media_recorder_take_picture(char* params, char* filename, size_t number, media_event_callback event_cb, void* cookie);

/**
 * @brief Start taking picture, including open, set_event_callback, prepare, and start operations.
 *
 * @attention Camera only.
 *
 * @param[in] params    Open params.
 * @param[in] filename  The store path for new picture.
 * @param[in] number    The number of taking picture.
 * @param[in] event_cb  The callback to handle state feedback.
 * @param[in] cookie    The private data of user.
 * @return void* Handle on success; a NULL pointer on failure.
 */
void* media_recorder_start_picture(char* params, char* filename, size_t number, media_event_callback event_cb, void* cookie);

/**
 * @brief Close the recorder when taking picture finished.
 *
 * @attention Camera only.
 *
 * @param[in] handle    The recoder handle returned in media_recorder_start_picture()
 * @return int Zero on success; a negated errno value on failure.
 *
 * @note This func should be used together with media_recorder_start_picture()
 * to close the recorder. This func should be called in the event_callback func
 * of the media_recorder_start_picture() when user want to take pic asynclly.
 */
int media_recorder_finish_picture(void* handle);

#ifdef CONFIG_LIBUV
/**
 * @brief Open an async recorder.
 *
 * @param[in] loop          Loop handle of current thread.
 * @param[in] source        Source type.
 * @param[in] on_open       Open callback, called after open is done.
 * @param[in] cookie        Long-term callback context for:
 *                          on_open, on_event, on_close.
 * @return void* Handle of recorder.
 *
 * @note Even if `on_open` has not been called, you can call other
 * handle-based async APIs, and they will be queued inside the
 * recorder handle as requests, and processed in order after
 * `open` is completed.
 */
void* media_uv_recorder_open(void* loop, const char* source,
    media_uv_callback on_open, void* cookie);

/**
 * @brief Listen to status change event by setting callback.
 *
 * @param[in] handle    Async recorder handle.
 * @param[in] on_event  Event callback, call after receiving notification.
 * @return int Zero on success, negative errno on failure.
 */
int media_uv_recorder_listen(void* handle, media_event_callback on_event);

/**
 * @brief Close the async recorder.
 *
 * @param[in] handle    Async recorder handle.
 * @param[in] on_close  Release callback, called after releasing internal resources.
 * @return int Zero on success, negative errno on illegal handle.
 */
int media_uv_recorder_close(void* handle, media_uv_callback on_close);

/**
 * @brief Prepare destination file.
 *
 * @param[in] handle        Async recorder handle.
 * @param[in] url           Path of destination.
 * @param[in] options       Destination options, @see media_recorder_prpare.
 * @param[in] on_connection
 * @param[in] on_prepare    Call after receiving result, will give an uv_pipe_t
 *                          to write data in buffer mode.
 * @param[in] cookie        One-time callback context.
 * @return int Zero on success, negative errno on failure.
 */
int media_uv_recorder_prepare(void* handle, const char* url, const char* options,
    media_uv_object_callback on_connection, media_uv_callback on_prepare, void* cookie);

/**
 * @brief Start or resume the capturing with auto focus request.
 *
 * @param[in] handle     Async recorder handle.
 * @param[in] scenario   MEDIA_SCENARIO_* in media_defs.h.
 * @param[in] on_start   Call after receiving result.
 * @param[in] cookie     One-time callback context.
 * @return int Zero on success, negative errno on failure.
 */
int media_uv_recorder_start_auto(void* handle, const char* stream,
    media_uv_callback on_start, void* cookie);

/**
 * @brief Start or resume the capturing.
 *
 * @param[in] handle    Async recorder handle.
 * @param[in] on_start  Call after receiving result.
 * @param[in] cookie    One-time callback context.
 * @return int Zero on success, negative errno on failure.
 */
int media_uv_recorder_start(void* handle, media_uv_callback on_start, void* cookie);

/**
 * @brief Pause the capturing.
 *
 * @param[in] handle    Async recorder handle.
 * @param[in] on_pause  Call after receiving result.
 * @param[in] cookie    One-time callback context.
 * @return int Zero on success, negative errno on failure.
 */
int media_uv_recorder_pause(void* handle, media_uv_callback on_pause, void* cookie);

/**
 * @brief Stop the capturing, finish the destination file.
 *
 * @param[in] handle    Async recorder handle.
 * @param[in] on_stop   Call after receiving result.
 * @param[in] cookie    One-time callback context.
 * @return int Zero on success, negative errno on failure.
 */
int media_uv_recorder_stop(void* handle, media_uv_callback on_stop, void* cookie);

/**
 * @brief Set properties of the recorder.
 *
 * @param[in] handle    Async recorder handle.
 * @param[in] target    Target filter.
 * @param[in] key       Key.
 * @param[in] value     Value.
 * @param[in] cb        Call after receiving result.
 * @param[in] cookie    One-time callback context.
 * @return int Zero on success; a negative errno value on failure.
 */
int media_uv_recorder_set_property(void* handle, const char* target, const char* key,
    const char* value, media_uv_callback cb, void* cookie);
/**
 * @brief Get properties of the recorder.
 *
 * @param[in] handle      Async recorder handle.
 * @param[in] target      Target filter.
 * @param[in] key         Key.
 * @param[in] value       Buffer of value.
 * @param[in] value_len   Buffer length of value.
 * @param[in] cb          Call after receiving result.
 * @param[in] cookie      One-time callback context.
 * @return int Zero on success; a negated errno value on failure.
 */
int media_uv_recorder_get_property(void* handle, const char* target, const char* key,
    media_uv_string_callback cb, void* cookie);
/**
 * @brief Reset the recorder, clear the origin record and record new one.
 *
 * @param[in] handle    Async recorder handle.
 * @param[in] cb        Call after receiving result.
 * @param[in] cookie    One-time callback context.
 * @return int Zero on success; a negative errno value on failure.
 */
int media_uv_recorder_reset(void* handle, media_uv_callback on_reset, void* cookie);

/**
 * @brief Take picture from camera.
 *
 * @attention Camera only.
 *
 * @param[in] loop        Loop handle of current thread.
 * @param[in] params      Camera open path params.
 * @param[in] filename    The store path for new picture.
 * @param[in] number      The number of pictures taken.
 * @param[in] on_complete The callback to handle result.
 * @param[in] cookie      The private data of user.
 * @return Zero on success; a negative errno value on failure.
 */
int media_uv_recorder_take_picture(void* loop, char* params, char* filename,
    size_t number, media_uv_callback on_complete, void* cookie);

#endif /* CONFIG_LIBUV */

#undef EXTERN
#ifdef __cplusplus
}
#endif
#endif /* FRAMEWORKS_MEDIA_INCLUDE_MEDIA_RECORDER_H */
