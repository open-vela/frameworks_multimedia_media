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

#ifdef __cplusplus
extern "C" {
#endif

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <poll.h>
#include <media_event.h>

/****************************************************************************
 * Media Definations
 ****************************************************************************/

#define MEDIA_PROCESS_COMMAND        1
#define MEDIA_DUMP                   2
#define MEDIA_PLAYER_OPEN            3
#define MEDIA_PLAYER_CLOSE           4
#define MEDIA_PLAYER_SET_CALLBACK    5
#define MEDIA_PLAYER_PREPARE         6
#define MEDIA_PLAYER_RESET           7
#define MEDIA_PLAYER_START           8
#define MEDIA_PLAYER_STOP            9
#define MEDIA_PLAYER_PAUSE           10
#define MEDIA_PLAYER_SEEK            11
#define MEDIA_PLAYER_LOOP            12
#define MEDIA_PLAYER_ISPLAYING       13
#define MEDIA_PLAYER_POSITION        14
#define MEDIA_PLAYER_DURATION        15
#define MEDIA_PLAYER_FADEIN          16
#define MEDIA_PLAYER_FADEOUT         17
#define MEDIA_PLAYER_SET_VOLUME      18
#define MEDIA_PLAYER_GET_VOLUME      19
#define MEDIA_RECORDER_OPEN          20
#define MEDIA_RECORDER_CLOSE         21
#define MEDIA_RECORDER_SET_CALLBACK  22
#define MEDIA_RECORDER_PREPARE       23
#define MEDIA_RECORDER_RESET         24
#define MEDIA_RECORDER_START         25
#define MEDIA_RECORDER_STOP          26

/****************************************************************************
 * Media Functions
 ****************************************************************************/

void *media_get_graph(void);
void *media_get_policy(void);
void *media_get_server(void);

/****************************************************************************
 * Stub Functions
 ****************************************************************************/

struct media_parcel;
void media_stub_onreceive(void *cookie,
                          struct media_parcel *in, struct media_parcel *out);

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

void *media_policy_create(void *file);
int media_policy_destroy(void *handle);
int media_policy_set_int_(const char *name, int value, int apply);
int media_policy_get_int_(const char *name, int *value);
int media_policy_set_string_(const char *name, const char *value, int apply);
int media_policy_get_string_(const char *name, char *value, size_t len);
int media_policy_increase_(const char *name, int apply);
int media_policy_decrease_(const char *name, int apply);

#ifdef __cplusplus
}
#endif

#endif /* FRAMEWORKS_MEDIA_INTERNAL_H */
