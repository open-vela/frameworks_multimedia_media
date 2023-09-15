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

#include <media_event.h>
#include <stddef.h>
#include <sys/socket.h>

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C" {
#else
#define EXTERN extern
#endif

/**
 * @brief Open one recorder according to the incomming path.
 *
 * @code
 *  // Example
 *  void *handle = media_recorder_open("cap");
 * @endcode
 *
 * @param[in] params    open path params, usually "cap".
 * @return Recorder handle on success; NULL on failure.
 */
void* media_recorder_open(const char* params);

/**
 * @brief Close one recorder according to the incoming handle.
 *
 * @code
 *  // Example
 *  void *handle = media_recorder_open("cap");
 *  int ret = media_recorder_close(handle);
 * @endcode
 *
 * @param[in] handle    The recorder handle
 * @return Zero on success; a negative errno value on failure.
 */
int media_recorder_close(void* handle);

/**
 * @brief Set event callback to the recorder, the callback will be called
 * when state changed or something user cares.
 *
 * @code
 *  struct player_state_s
 *  {
 *      sem_t sem;
 *      int   last_event;
 *      int   result;
 *      void *player_handle;
 *  };
 *  void callback(void* cookie, int event, int ret, const char *data){
 *      struct player_state_s* player_state = (struct player_state_s*) cookie;
 *      player_state->last_event = event;
 *      player_state->result = ret;
 *      sem_post(&player_state->sem);
 *  }
 *
 *  // You can modify or create as you wish.
 *  struct player_state_s* player_state = *state;
 *  void *handle = media_recorder_open("cap");
 *  int ret = media_recorder_set_event_callback(handle, player_state,
 *                                              callback);
 *  printf("%d", player_state->last_event);
 * @endcode
 *
 * @param[in] handle    The recorder handle
 * @param[in] cookie    User cookie, will be brought back to user
 *                      after calling event_cb and usually be modified
 * @param[in] event_cb  Event callback
 * @return Zero on success; a negative errno value on failure.
 */
int media_recorder_set_event_callback(void* handle, void* cookie,
    media_event_callback event_cb);

/**
 * @brief Prepare the recorder.
 *
 * @code
 *  void *handle = media_recorder_open("cap");
 *  // set event callback here.
 *  int ret = media_recorder_prepare(handle, "/music/example.wav", NULL);
 * @endcode
 *
 * @param[in] handle    Current recorder handle
 * @param[in] url       Valid data dest recorderd to, usually in wav format
 * @param[in] options   Extra configuration
 *                        - format: encapsulation format, 如:opus wav
 *                        - sample_rate: target sampling rate
 *                        - channel_layout: target channel layout
 *                        - bitrate: target code rate，如: "23900"
 *                        - vbr: 0: fixed code rate; 1: variable code rate
 *                        - level: encoding complexity. range：0-10. default:10
 *
 *
 *                      - If url is not NULL:
 *                           options is the url addtional description
 *                      - If url is NULL: options describes "buffer" mode:
 *                        - "format=s16le,sample_rate=44100,channels=2"
 *                        - "format=unknown"
 *
 *                        then use media_recorder_read_data() read data
 * @return Zero on success; a negative errno value on failure.
 */
int media_recorder_prepare(void* handle, const char* url, const char* options);

/**
 * @brief Reset the recorder, clear the origin record and record new one.
 *
 * @code
 *  void *handle = media_recorder_open("cap");
 *  // set event callback here.
 *  int ret = media_recorder_prepare(handle, "/music/example.wav", NULL);
 *  ret = media_recorder_start(handle);
 *  int ret = media_recorder_reset(handle);
 * @endcode
 *
 * @param[in] handle    The recorder handle
 * @return Zero on success; a negative errno value on failure.
 */
int media_recorder_reset(void* handle);

/**
 * @brief Read recorderd data from the recorder.
 *
 * @attention This need media_recorder_prepare() url set to NULL.
 * This function is blocked.
 *
 * @code
 *  int buf_len = 10;
 *  void buf[buf_len];
 *  void *handle = media_recorder_open("cap");
 *  // set event callback here.
 *  int ret = media_recorder_prepare(handle, NULL, NULL);
 *  ret = media_recorder_start(handle);
 *  int len = media_recorder_read_data(handle, buf, buf_len);
 * @endcode
 *
 * @param[in] handle    Current recorder path
 * @param[in] data      Buffer will be recorderd to
 * @param[in] len       Buffer len
 * @return Length of read data; a negative errno value on failure.
 */
ssize_t media_recorder_read_data(void* handle, void* data, size_t len);

/**
 * @brief Get sockaddr for unblock mode read data
 *
 * @param[in] handle    The recorder handle
 * @param[in] addr      The addr pointer to store sockaddr
 * @return Zero on success; a negative errno value on failure.
 */
int media_recorder_get_sockaddr(void* handle, struct sockaddr_storage* addr);

/**
 * @brief Get socket fd for non-block mode read data
 *
 * @param[in] handle    The recorder handle
 * @return fd; a negative errno value on failure.
 */
int media_recorder_get_socket(void* handle);

/**
 * @brief Close socket fd when recorder finish recving data.
 * @param[in] handle    The recorder handle
 */
void media_recorder_close_socket(void* handle);

/**
 * @brief Start the recorder.
 *
 * @code
 *  void *handle = media_recorder_open("cap");
 *  // set event callback here.
 *  int ret = media_recorder_prepare(handle, "/music/example.wav", NULL);
 *  ret = media_recorder_start(handle);
 * @endcode
 *
 * @param[in] handle    Current recorder handle
 * @return Zero on success; a negative errno value on failure.
 */
int media_recorder_start(void* handle);

/**
 * @brief Pause the recorder.
 *
 * @code
 *  void *handle = media_recorder_open("cap");
 *  // set event callback here.
 *  int ret = media_recorder_prepare(handle, "/music/example.wav", NULL);
 *  ret = media_recorder_start(handle);
 *  ret = media_recorder_pause(handle);
 * @endcode
 *
 * @param[in] handle    Current recorder handle
 * @return Zero on success; a negative errno value on failure.
 */
int media_recorder_pause(void* handle);

/**
 * @brief Stop the recorder.
 *
 * @code
 *  void *handle = media_recorder_open("cap");
 *  // set event callback here.
 *  int ret = media_recorder_prepare(handle, "/music/example.wav", NULL);
 *  ret = media_recorder_start(handle);
 *  sleep(10); // record for 10 s.
 *  ret = media_recorder_stop(handle);
 * @endcode
 *
 * @param[in] handle    Current recorder handle
 * @return Zero on success; a negative errno value on failure.
 */
int media_recorder_stop(void* handle);

/**
 * @brief Set properties of the recorder path.
 *
 * @param[in] handle      Current recorder path
 * @param[in] target      Target filter
 * @param[in] key         Key
 * @param[in] value       Value
 * @return Zero on success; a negative errno value on failure.
 */
int media_recorder_set_property(void* handle, const char* target, const char* key, const char* value);

/**
 * @brief Get properties of the recorder path.
 *
 * @param[in] handle      Current recorder path
 * @param[in] target      Target filter
 * @param[in] key         Key
 * @param[in] value       Buffer of value
 * @param[in] value_len   Buffer length of value
 * @return Zero on success; a negated errno value on failure.
 */
int media_recorder_get_property(void* handle, const char* target, const char* key, char* value, int value_len);

/**
 * @brief Take picture from camera.
 *
 * @attention Camera only.
 *
 * @param[in] params    Camera open path params
 * @param[in] filename  The store path for new picture
 * @param[in] number    The number of pictures taken
 * @param[in] event_cb  The callback to handle state feedback
 * @param[in] cookie    The private data of user
 * @return Zero on success; a negative errno value on failure.
 */
int media_recorder_take_picture(char* params, char* filename, size_t number, media_event_callback event_cb, void* cookie);

#undef EXTERN
#ifdef __cplusplus
}
#endif
#endif /* FRAMEWORKS_MEDIA_INCLUDE_MEDIA_RECORDER_H */
