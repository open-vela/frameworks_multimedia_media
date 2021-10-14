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

#include <poll.h>
#include <media_event.h>

/****************************************************************************
 * Media Functions
 ****************************************************************************/

void *media_get_graph(void);

/****************************************************************************
 * Graph Functions
 ****************************************************************************/

void *media_graph_create(void *file);
int media_graph_destroy(void *handle);
int media_graph_get_pollfds(void *handle, struct pollfd *fds,
                            void **cookies, int count);
int media_graph_poll_available(void *handle, struct pollfd *fd, void *cookie);
int media_graph_process_command(void *handle, const char *target,
                                const char *cmd, const char *arg,
                                char *res, int res_len);
void media_graph_dump(void *handle, const char *options);

/****************************************************************************
 * Player Functions
 ****************************************************************************/

void *media_player_open_(void *graph, const char *name);
int media_player_close_(void *handle, int pending_stop);
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

void *media_recorder_open_(void *graph, const char *name);
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
