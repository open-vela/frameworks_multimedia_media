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

#include <media_event.h>
#include <poll.h>

/****************************************************************************
 * Media Definations
 ****************************************************************************/

#define MEDIA_GRAPH_CONTROL 1
#define MEDIA_POLICY_CONTROL 2
#define MEDIA_PLAYER_CONTROL 3
#define MEDIA_PLAYER_SET_CALLBACK 4
#define MEDIA_RECORDER_CONTROL 5
#define MEDIA_RECORDER_SET_CALLBACK 6

#define MEDIA_PLAYER_NAME "amovie_async"
#define MEDIA_RECORDER_NAME "amoviesink_async"

#ifndef CONFIG_RPTUN_LOCAL_CPUNAME
#define CONFIG_RPTUN_LOCAL_CPUNAME "local"
#endif

#define MEDIA_SOCKADDR_NAME "md:%s"

/****************************************************************************
 * Media Functions
 ****************************************************************************/

void* media_get_graph(void);
void* media_get_policy(void);
void* media_get_server(void);

/****************************************************************************
 * Stub Functions
 ****************************************************************************/

struct media_parcel;
void media_stub_onreceive(void* cookie,
    struct media_parcel* in, struct media_parcel* out);

/****************************************************************************
 * Graph Functions
 ****************************************************************************/

void* media_graph_create(void* file);
int media_graph_destroy(void* handle);
int media_graph_get_pollfds(void* handle, struct pollfd* fds,
    void** cookies, int count);
int media_graph_poll_available(void* handle, struct pollfd* fd, void* cookie);
int media_graph_run_once(void* handle);
int media_graph_control(void* handle, const char* target, const char* cmd,
    const char* arg, char** res, int res_len);

/****************************************************************************
 * Player Functions
 ****************************************************************************/

int media_player_control(void* handle, const char* target, const char* cmd,
    const char* arg, char** res, int res_len);
int media_player_set_event_callback_(void* handle, void* cookie,
    media_event_callback event_cb);

/****************************************************************************
 * Recorder Functions
 ****************************************************************************/

int media_recorder_control(void* handle, const char* target, const char* cmd,
    const char* arg, char** res, int res_len);
int media_recorder_set_event_callback_(void* handle, void* cookie,
    media_event_callback event_cb);

/****************************************************************************
 * Policy Functions
 ****************************************************************************/

void* media_policy_create(void* file);
int media_policy_destroy(void* handle);
int media_policy_control(void* handle, const char* name, const char* cmd,
    const char* value, int apply, char** res, int res_len);
int media_policy_get_stream_name(void* handle, const char* stream,
    char* name, int len);
int media_policy_set_stream_status(void* handle, const char* name,
    const char* input, bool active);
void media_policy_process_command(const char* target, const char* cmd,
    const char* arg);

#ifdef __cplusplus
}
#endif

#endif /* FRAMEWORKS_MEDIA_INTERNAL_H */
