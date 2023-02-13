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
 * Open one recorder path.
 * @param[in] params    open params
 * @return Zero on success; a negated errno value on failure.
 */
void* media_recorder_open(const char* params);

/**
 * Close the recorder path.
 * @param[in] handle    The recorder path
 * @return Zero on success; a negated errno value on failure.
 */
int media_recorder_close(void* handle);

/**
 * Set event callback to the recorder path, the callback will be called
 * when state changed or something user cares.
 * @param[in] handle    The recorder path
 * @param[in] cookie    User cookie, will bring back to user when do event_cb
 * @param[in] event_cb  Event callback
 * @return Zero on success; a negated errno value on failure.
 */
int media_recorder_set_event_callback(void* handle, void* cookie,
    media_event_callback event_cb);

/**
 * Prepare the recorder path.
 * @param[in] handle    Current recorder path
 * @param[in] url       Data dest recorderd to
 * @param[in] options   Extra configure
 *                      - If url is not NULL:options is the url addtional description
 *                      - If url is NULL: options descript "buffer" mode:
 *                        - "format=s16le,sample_rate=44100,channels=2"
 *                        - "format=unknown"
 *
 *                        then use media_recorder_read_data() read data
 * @return Zero on success; a negated errno value on failure.
 */
int media_recorder_prepare(void* handle, const char* url, const char* options);

/**
 * Reset the recorder path.
 * @param[in] handle    The recorder path
 * @return Zero on success; a negated errno value on failure.
 */
int media_recorder_reset(void* handle);

/**
 * Read recorderd data from the recorder path.
 * This need media_recorder_prepare() url set to NULL.
 * Note: this function is blocked
 * @param[in] handle    Current recorder path
 * @param[in] data      Buffer will be recorderd to
 * @param[in] len       Buffer len
 * @return returned >= 0, Actly got len; a negated errno value on failure.
 */
ssize_t media_recorder_read_data(void* handle, void* data, size_t len);

/**
 * Get sockaddr for unblock mode read data
 * @param[in] handle    The player path
 * @param[in] addr      The sockaddr pointer
 * @return returned >= 0, Actly sent len; a negated errno value on failure.
 */
int media_recorder_get_sockaddr(void* handle, struct sockaddr_storage* addr);

/**
 * Get socket fd for unblock mode read data
 * @param[in] handle    The player path
 * @return fd; a negated errno value on failure.
 */
int media_recorder_get_socket(void* handle);

/**
 * Start the recorder path.
 * @param[in] handle    Current recorder path
 * @return Zero on success; a negated errno value on failure.
 */
int media_recorder_start(void* handle);

/**
 * Pause the recorder path.
 * @param[in] handle    Current recorder path
 * @return Zero on success; a negated errno value on failure.
 */
int media_recorder_pause(void* handle);

/**
 * Stop the recorder path.
 * @param[in] handle    Current recorder path
 * @return Zero on success; a negated errno value on failure.
 */
int media_recorder_stop(void* handle);

/**
 * Set properties of the recorder path.
 * @param[in] handle      Current recorder path
 * @param[in] target      Target filter
 * @param[in] key         Key
 * @param[in] value       Value
 * @return Zero on success; a negated errno value on failure.
 */
int media_recorder_set_property(void* handle, const char* target, const char* key, const char* value);

/**
 * Get properties of the recorder path.
 * @param[in] handle      Current recorder path
 * @param[in] target      Target filter
 * @param[in] key         Key
 * @param[in] value       Buffer of value
 * @param[in] value_len   Buffer length of value
 * @return Zero on success; a negated errno value on failure.
 */
int media_recorder_get_property(void* handle, const char* target, const char* key, char* value, int value_len);

#undef EXTERN
#ifdef __cplusplus
}
#endif
#endif /* FRAMEWORKS_MEDIA_INCLUDE_MEDIA_RECORDER_H */
