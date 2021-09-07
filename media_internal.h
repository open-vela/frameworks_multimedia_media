/****************************************************************************
 * frameworks/media/media_internal.h
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

#ifndef FRAMEWORKS_MEDIA_MEDIA_INTERNAL_H
#define FRAMEWORKS_MEDIA_MEDIA_INTERNAL_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <media_event.h>

/****************************************************************************
 * Server Functions
 ****************************************************************************/

void *media_server_get_graph_(void);
void media_server_dump_(const char *options);

/****************************************************************************
 * Player Functions
 ****************************************************************************/

void *media_player_open_(const char *name);
int media_player_close_(void *handle);
int media_player_set_event_callback_(void *handle, void *cookie,
                                    media_event_callback event_cb);
int media_player_prepare_(void *handle, const char *url, const char *options);
int media_player_reset_(void *handle);
int media_player_start_(void *handle);
int media_player_stop_(void *handle);
int media_player_pause_(void *handle);
int media_player_set_looping_(void *handle, int loop);
int media_player_is_playing_(void *handle);
int media_player_seek_(void *handle, unsigned int msec);
int media_player_get_position_(void *handle, unsigned int *msec);
int media_player_get_duration_(void *handle, unsigned int *msec);
int media_player_set_fadein_(void *handle, unsigned int msec);
int media_player_set_fadeout_(void *handle, unsigned int msec);
int media_player_set_volume_(void *handle, float volume);
int media_player_get_volume_(void *handle, float *volume);

/****************************************************************************
 * Recorder Functions
 ****************************************************************************/

void *media_recorder_open_(const char *name);
int media_recorder_close_(void *handle);
int media_recorder_set_event_callback_(void *handle, void *cookie,
                                      media_event_callback event_cb);
int media_recorder_prepare_(void *handle, const char *url, const char *options);
int media_recorder_reset_(void *handle);
int media_recorder_start_(void *handle);
int media_recorder_stop_(void *handle);

/****************************************************************************
 * Policy Functions
 ****************************************************************************/

#endif /* FRAMEWORKS_MEDIA_INTERNAL_H */
