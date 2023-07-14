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
#include <media_focus.h>
#include <poll.h>

/****************************************************************************
 * Media Definations
 ****************************************************************************/

#define MEDIA_ID_GRAPH 1
#define MEDIA_ID_POLICY 2
#define MEDIA_ID_PLAYER 3
#define MEDIA_ID_RECORDER 4
#define MEDIA_ID_SESSION 5
#define MEDIA_ID_FOCUS 6

#ifndef CONFIG_RPTUN_LOCAL_CPUNAME
#define CONFIG_RPTUN_LOCAL_CPUNAME CONFIG_MEDIA_SERVER_CPUNAME
#endif

#define MEDIA_SOCKADDR_NAME "md:%s"

#define MEDIA_IS_STATUS_CHANGE(x) ((x) < 200)

/****************************************************************************
 * Media Functions
 ****************************************************************************/

void* media_get_focus(void);
void* media_get_graph(void);
void* media_get_policy(void);
void* media_get_session(void);
void* media_get_server(void);

/****************************************************************************
 * Stub Functions
 ****************************************************************************/

struct media_parcel;
void media_stub_notify_finalize(void** cookie);
void media_stub_notify_event(void* cookie, int event,
    int result, const char* extra);
void media_stub_onreceive(void* cookie,
    struct media_parcel* in, struct media_parcel* out);

/****************************************************************************
 * Focus Functions
 ****************************************************************************/

typedef struct media_focus_id {
    int client_id;
    int stream_type;
    unsigned int thread_id;
    int focus_state;
    media_focus_callback callback_method;
    void* callback_argv;
} media_focus_id;

void* media_focus_create(void* file);
int media_focus_destroy(void* handle);
int media_focus_handler(void* handle, const char* name, const char* cmd,
    char* res, int res_len);
void media_focus_debug_stack_display(void);
int media_focus_debug_stack_return(media_focus_id* focus_list, int num);

/****************************************************************************
 * Graph Functions
 ****************************************************************************/

void* media_graph_create(void* file);
int media_graph_destroy(void* handle);
int media_graph_get_pollfds(void* handle, struct pollfd* fds,
    void** cookies, int count);
int media_graph_poll_available(void* handle, struct pollfd* fd, void* cookie);
int media_graph_run_once(void* handle);
int media_graph_handler(void* handle, const char* target, const char* cmd,
    const char* arg, char* res, int res_len);

int media_player_handler(void* handle, const char* target, const char* cmd,
    const char* arg, char* res, int res_len);
int media_recorder_handler(void* handle, const char* target, const char* cmd,
    const char* arg, char* res, int res_len);

/****************************************************************************
 * Session Functions
 ****************************************************************************/

void* media_session_create(void* file);
int media_session_destroy(void* session);
int media_session_handler(void* handle, const char* target, const char* cmd,
    const char* arg, char* res, int res_len);

/****************************************************************************
 * Policy Functions
 ****************************************************************************/

void* media_policy_create(void* file);
int media_policy_destroy(void* handle);
int media_policy_handler(void* handle, const char* name, const char* cmd,
    const char* value, int apply, char* res, int res_len);
int media_policy_get_stream_name(const char* stream, char* name, int len);
int media_policy_set_stream_status(const char* name, bool active);
void media_policy_process_command(const char* target, const char* cmd,
    const char* arg);

#ifdef __cplusplus
}
#endif

#endif /* FRAMEWORKS_MEDIA_INTERNAL_H */
