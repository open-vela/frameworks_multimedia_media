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

/****************************************************************************
 * Name: media_player_open
 *
 * Description:
 *   Open one player path.
 *
 * Input Parameters:
 *   params - open params
 *
 * Returned Value:
 *   Pointer to created handle or NULL on failure.
 *
 ****************************************************************************/

void *media_player_open(const char *params);

/****************************************************************************
 * Name: media_player_close
 *
 * Description:
 *  Close the player path.
 *
 * Input Parameters:
 *   handle - The player path to be destroyed
 *   pending_stop - whether pending command.
 *                  0: close immediately
 *                  1: pending command, close automatically after playbacek complete
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_player_close(void *handle, int pending_stop);

/****************************************************************************
 * Name: media_player_set_event_callback
 *
 * Description:
 *  Set event callback to the player path, the callback will be called
 *  when state changed or something user cares.
 *
 * Input Parameters:
 *   handle - The player path
 *   cookie - User cookie, will bring back to user when do event_cb
 *   event_cb - Event callback
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_player_set_event_callback(void *handle, void *cookie,
                                    media_event_callback event_cb);

/****************************************************************************
 * Name: media_player_prepare
 *
 * Description:
 *  Prepare the player path.
 *
 * Input Parameters:
 *   handle - The player path
 *   url    - Data source to be played
 *   options - Extra options configure
 *            If url is not NULL: options is the url addtional description
 *            If url is NULL: options descript "buffer" mode,
 *                            e.g. format=s16le,sample_rate=44100,channels=2
 *                            e.g. format=unknown
 *                            then use media_player_write_data() write data
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_player_prepare(void *handle, const char *url, const char *options);

/****************************************************************************
 * Name: media_player_reset
 *
 * Description:
 *  Reset the player path.
 *
 * Input Parameters:
 *   handle - The player path
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_player_reset(void *handle);

/****************************************************************************
 * Name: media_player_write_data
 *
 * Description:
 *  Write data to player path.
 *  This need media_player_prepare() url set to NULL.
 *
 * Input Parameters:
 *   handle - The player path
 *   data   - Buffer will be played
 *   len    - Buffer len
 *
 * Returned Value:
 *   returned >= 0, Actly sent len; a negated errno value on failure.
 *
 ****************************************************************************/

ssize_t media_player_write_data(void *handle, const void *data, size_t len);

/****************************************************************************
 * Name: media_player_start
 *
 * Description:
 *  Start the player path to play
 *
 * Input Parameters:
 *   handle - The player path
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_player_start(void *handle);

/****************************************************************************
 * Name: media_player_stop
 *
 * Description:
 *  Stop the player path
 *
 * Input Parameters:
 *   handle - The player path
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_player_stop(void *handle);

/****************************************************************************
 * Name: media_player_pause
 *
 * Description:
 *  Pause the player path
 *
 * Input Parameters:
 *   handle - The player path
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_player_pause(void *handle);

/****************************************************************************
 * Name: media_player_seek
 *
 * Description:
 *  Seek to msec position from begining
 *
 * Input Parameters:
 *   handle - The player path
 *   mesc   - Which postion should seek from begining
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_player_seek(void *handle, unsigned int msec);

/****************************************************************************
 * Name: media_player_set_looping
 *
 * Description:
 *  Set the player path looping play, defult is not looping
 *
 * Input Parameters:
 *   handle - The player path
 *   loop   - Loop count (-1: forever, 0: not loop)
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_player_set_looping(void *handle, int loop);

/****************************************************************************
 * Name: media_player_is_playing
 *
 * Description:
 *  Get play path is playing or not
 *
 * Input Parameters:
 *   handle - The player path
 *
 * Returned Value:
 *   true 1 playing, false 0 not playing, a negated errno value on failure.
 *
 ****************************************************************************/

int media_player_is_playing(void *handle);

/****************************************************************************
 * Name: media_player_get_position
 *
 * Description:
 *  Get player playback position
 *
 * Input Parameters:
 *   handle - The player path
 *   mesc   - Playback postion (from begining)
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_player_get_position(void *handle, unsigned int *msec);

/****************************************************************************
 * Name: media_player_get_duration
 *
 * Description:
 *  Get plyabck file duration (Total play time)
 *
 * Input Parameters:
 *   handle - The player path
 *   mesc   - File duration
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_player_get_duration(void *handle, unsigned int *msec);

/****************************************************************************
 * Name: media_player_set_fadein
 *
 * Description:
 *  Set the player path fadein
 *
 * Input Parameters:
 *   handle - The player path
 *   msec   - Fadein used time in millisecond
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_player_set_fadein(void *handle, unsigned int msec);

/****************************************************************************
 * Name: media_player_set_fadeout
 *
 * Description:
 *  Set the player path fadeout
 *
 * Input Parameters:
 *   handle - The player path
 *   msec   - Fadeout used time in millisecond
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_player_set_fadeout(void *handle, unsigned int msec);

/****************************************************************************
 * Name: media_player_set_volume
 *
 * Description:
 *  Set the player path volume
 *
 * Input Parameters:
 *   handle - The player path
 *   volume - Volume with range of 0.0 - 1.0
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_player_set_volume(void *handle, float volume);

/****************************************************************************
 * Name: media_player_get_volume
 *
 * Description:
 *  Get the player path volume
 *
 * Input Parameters:
 *   handle - The player path
 *   volume - Volume with range of 0.0 - 1.0
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_player_get_volume(void *handle, float *volume);

#undef EXTERN
#ifdef __cplusplus
}
#endif
#endif /* FRAMEWORKS_MEDIA_INCLUDE_MEDIA_PLAYER_H */
