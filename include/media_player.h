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
 * Open one player path.
 * @param[in] stream    Stream type @see MEDIA_STREAM_*
 * @return Pointer to created handle or NULL on failure.
 */
void* media_player_open(const char* stream);

/**
 * Close the player path
 * @param[in] handle       The player path to be destroyed.
 * @param[in] pending_stop whether pending command.
 *                         - 0: close immediately
 *                         - 1: pending command,
 *                              close automatically after playback complete
 * @return Zero on success; a negated errno value on failure.
 */
int media_player_close(void* handle, int pending_stop);

/**
 * Set event callback to the player path, the callback will be called
 * when state changed.
 * @param[in] handle    The player path
 * @param[in] cookie    User cookie, will bring back to user when do event_cb
 * @param[in] event_cb  Event callback
 * @return Zero on success; a negated errno value on failure.
 */
int media_player_set_event_callback(void* handle, void* cookie,
    media_event_callback event_cb);

/**
 * Prepare the player path.
 * @param[in] handle    The player path
 * @param[in] url       Data source to be played
 * @param[in] options   Extra options configure
 *                      - If url is not NULL: options is the url addtional description
 *                      - If url is NULL: options descript "buffer" mode, exmaple:
 *                        - "format=s16le,sample_rate=44100,channels=2"
 *                        - "format=unknown"
 *
 *                        then use media_player_write_data() write data
 * @return Zero on success; a negated errno value on failure.
 */
int media_player_prepare(void* handle, const char* url, const char* options);

/**
 * Reset the player path.
 * @param[in] handle The player path
 * @return Zero on success; a negated errno value on failure.
 */
int media_player_reset(void* handle);

/**
 * Write data to player path.
 * This need media_player_prepare() url set to NULL.
 * Note: this function is blocked
 * @param[in] handle    The player path
 * @param[in] data      Buffer will be played
 * @param[in] len       Buffer len
 * @return returned >= 0, Actly sent len; a negated errno value on failure.
 */
ssize_t media_player_write_data(void* handle, const void* data, size_t len);

/**
 * Get sockaddr for unblock mode write data
 * @param[in] handle    The player path
 * @param[in] addr      The sockaddr pointer
 * @return returned >= 0, Actly sent len; a negated errno value on failure.
 */
int media_player_get_sockaddr(void* handle, struct sockaddr_storage* addr);

/**
 * @brief Get socket fd for unblock mode write data.
 *
 * @code
 *  int  ret;
 *  void *handle;
 *  handle = media_player_open(MEDIA_STREAM_MUSIC);
 *  // set event callback
 *  // set prepare mode buffer
 *  ret = media_player_prepare(handle, NULL, NULL);
 *  int socketfd = media_player_get_socket(handle);
 * @endcode
 *
 * @param[in] handle    The player handle.
 * @return fd; a negated errno value on failure.
 */
int media_player_get_socket(void* handle);

/**
 * @brief Close socket fd when player finish read data.
 *
 * @param[in] handle    The player handle.
 */
void media_player_close_socket(void* handle);

/**
 * Start the player path to play
 * @param[in] handle    The player path
 * @return Zero on success; a negated errno value on failure.
 */
int media_player_start(void* handle);

/**
 * Stop the player path
 * @param[in] handle    The player path
 * @return Zero on success; a negated errno value on failure.
 */
int media_player_stop(void* handle);

/**
 * Pause the player path
 * @param[in] handle    The player path
 * @return Zero on success; a negated errno value on failure.
 */
int media_player_pause(void* handle);

/**
 * Seek to msec position from begining
 * @param[in] handle    The player path
 * @param[in] mesc      Which postion should seek from begining
 * @return Zero on success; a negated errno value on failure.
 */
int media_player_seek(void* handle, unsigned int msec);

/**
 * Set the player path looping play, defult is not looping
 * @param[in] handle    The player path
 * @param[in] loop      Loop count (-1: forever, 0: not loop)
 * @return Zero on success; a negated errno value on failure.
 */
int media_player_set_looping(void* handle, int loop);

/**
 * @brief Check if the media player is currently playing.
 *
 * @code
 *  // Example
 *  void *handle;
 *  int ret;
 *  handle = media_player_open("Music");
 *  // set event callback
 *  ret = media_player_prepare(handle, "/music/1.mp3", NULL);
 *  ret = media_player_start(handle);
 *  ret = media_player_is_playing(handle);
 *  syslog(LOG_INFO, "Player: playing %d.\n", ret);
 * @endcode
 *
 * @param[in] handle    The player handle.
 * @return 1 on playing, 0 on not-playing, else on error.
 */
int media_player_is_playing(void* handle);

/**
 * @brief Get the playing duration after the music starts playing.
 *
 * @code
 *  void *handle;
 *  int ret;
 *  unsigned int position;
 *  handle = media_player_open(MEDIA_STREAM_MUSIC);
 *  // set event callback
 *  ret = media_player_prepare(handle, "/music/1.mp3", NULL);
 *  ret = media_player_start(handle);
 *  ret = media_player_get_position(handle, &position);
 * @endcode
 *
 * @param[in] handle    The player handle.
 * @param[in] mesc      Playback postion (from begining)
 * @return Zero on success; a negated errno value on failure.
 */
int media_player_get_position(void* handle, unsigned int* msec);

/**
 * @brief Get playback file duration (Total play time).
 *
 * @code
 *  void *handle;
 *  int ret;
 *  unsigned int duration;
 *  handle = media_player_open(MEDIA_STREAM_MUSIC);
 *  // set event callback
 *  ret = media_player_prepare(handle, "/music/1.mp3", NULL);
 *  ret = media_player_start(handle);
 *  ret = media_player_get_duration(handle, &duration);
 * @endcode
 *
 * @param[in] handle    The player handle.
 * @param[in] mesc      Store playing time of the media.
 * @param[in] mesc      File duration
 * @return Zero on success; a negated errno value on failure.
 */
int media_player_get_duration(void* handle, unsigned int* msec);

/**
 * @brief Set the player path volume.
 *
 * @code
 *  void *handle;
 *  int ret;
 *  handle = media_player_open(MEDIA_STREAM_MUSIC);
 *  ret = media_player_set_volume(handle, 0.2);
 * @endcode
 *
 * @param[in] handle    The player handle.
 * @param[in] volume    Volume with range of 0.0 - 1.0
 * @return Zero on success; a negated errno value on failure.
 */
int media_player_set_volume(void* handle, float volume);

/**
 * @brief Get the player handle volume.
 *
 * @code
 *  void *handle;
 *  int ret;
 *  float volume;
 *  handle = media_player_open(MEDIA_STREAM_MUSIC);
 *  // set event callback
 *  ret = media_player_prepare(handle, "/music/1.mp3", NULL);
 *  ret = media_player_start(handle);
 *  ret = media_player_get_volume(player, &volume);
 * @endcode
 *
 * @param[in] handle    The player path
 * @param[in] volume    Volume with range of 0.0 - 1.0
 * @return Zero on success; a negated errno value on failure.
 */
int media_player_get_volume(void* handle, float* volume);

/**
 * @brief Set properties of the player path.
 *
 * @param[in] handle      Current player path
 * @param[in] target      Target filter
 * @param[in] key         Key
 * @param[in] value       Value
 * @return Zero on success; a negated errno value on failure.
 */
int media_player_set_property(void* handle, const char* target, const char* key, const char* value);

/**
 * @brief Get properties of the player path.
 *
 * @param[in] handle      Current player path
 * @param[in] target      Target filter
 * @param[in] key         Key
 * @param[in] value       Buffer of value
 * @param[in] value_len   Buffer length of value
 * @return Zero on success; a negated errno value on failure.
 */
int media_player_get_property(void* handle, const char* target, const char* key, char* value, int value_len);

#undef EXTERN
#ifdef __cplusplus
}
#endif
#endif /* FRAMEWORKS_MEDIA_INCLUDE_MEDIA_PLAYER_H */
