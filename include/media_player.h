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

#include <stddef.h>
#include <media_event.h>

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/**
 * Open one player path.
 * @param[in] params    open params
 * @return Pointer to created handle or NULL on failure.
 */
void *media_player_open(const char *params);

/**
 * Close the player path
 * @param[in] handle       The player path to be destroyed.
 * @param[in] pending_stop whether pending command.
 *                         - 0: close immediately
 *                         - 1: pending command,
 *                              close automatically after playbacek complete
 * @return Zero on success; a negated errno value on failure.
 */
int media_player_close(void *handle, int pending_stop);

/**
 * Set event callback to the player path, the callback will be called
 * when state changed or something user cares.
 * @param[in] handle    The player path
 * @param[in] cookie    User cookie, will bring back to user when do event_cb
 * @param[in] event_cb  Event callback
 * @return Zero on success; a negated errno value on failure.
 */
int media_player_set_event_callback(void *handle, void *cookie,
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
int media_player_prepare(void *handle, const char *url, const char *options);

/**
 * Reset the player path.
 * @param[in] handle The player path
 * @return Zero on success; a negated errno value on failure.
 */
int media_player_reset(void *handle);

/**
 * Write data to player path.
 * This need media_player_prepare() url set to NULL.
 * @param[in] handle    The player path
 * @param[in] data      Buffer will be played
 * @param[in] len       Buffer len
 * @return returned >= 0, Actly sent len; a negated errno value on failure.
 */
ssize_t media_player_write_data(void *handle, const void *data, size_t len);

/**
 * Start the player path to play
 * @param[in] handle    The player path
 * @return Zero on success; a negated errno value on failure.
 */
int media_player_start(void *handle);

/**
 * Stop the player path
 * @param[in] handle    The player path
 * @return Zero on success; a negated errno value on failure.
 */
int media_player_stop(void *handle);

/**
 * Pause the player path
 * @param[in] handle    The player path
 * @return Zero on success; a negated errno value on failure.
 */
int media_player_pause(void *handle);

/**
 * Seek to msec position from begining
 * @param[in] handle    The player path
 * @param[in] mesc      Which postion should seek from begining
 * @return Zero on success; a negated errno value on failure.
 */
int media_player_seek(void *handle, unsigned int msec);

/**
 * Set the player path looping play, defult is not looping
 * @param[in] handle    The player path
 * @param[in] loop      Loop count (-1: forever, 0: not loop)
 * @return Zero on success; a negated errno value on failure.
 */
int media_player_set_looping(void *handle, int loop);

/**
 * Get play path is playing or not
 * @param[in] handle    The player path
 * @return Zero on success; a negated errno value on failure.
 */
int media_player_is_playing(void *handle);

/**
 * Get player playback position
 * @param[in] handle    The player path
 * @param[in] mesc      Playback postion (from begining)
 * @return Zero on success; a negated errno value on failure.
 */
int media_player_get_position(void *handle, unsigned int *msec);

/**
 * Get plyabck file duration (Total play time)
 * @param[in] handle    The player path
 * @param[in] mesc      File duration
 * @return Zero on success; a negated errno value on failure.
 */
int media_player_get_duration(void *handle, unsigned int *msec);

/**
 * Set the player path fadein
 * @param[in] handle    The player path
 * @param[in] mesc      Fadein used time in millisecond
 * @return Zero on success; a negated errno value on failure.
 */
int media_player_set_fadein(void *handle, unsigned int msec);

/**
 * Set the player path fadeout
 * @param[in] handle    The player path
 * @param[in] mesc      Fadeout used time in millisecond
 * @return Zero on success; a negated errno value on failure.
 */
int media_player_set_fadeout(void *handle, unsigned int msec);

/**
 * Set the player path volume
 * @param[in] handle    The player path
 * @param[in] mesc      Volume with range of 0.0 - 1.0
 * @return Zero on success; a negated errno value on failure.
 */
int media_player_set_volume(void *handle, float volume);

/**
 * Get the player path volume
 * @param[in] handle    The player path
 * @param[in] mesc      Volume with range of 0.0 - 1.0
 * @return Zero on success; a negated errno value on failure.
 */
int media_player_get_volume(void *handle, float *volume);

#undef EXTERN
#ifdef __cplusplus
}
#endif
#endif /* FRAMEWORKS_MEDIA_INCLUDE_MEDIA_PLAYER_H */
