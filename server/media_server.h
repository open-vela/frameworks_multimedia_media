/****************************************************************************
 * frameworks/media/media_server.h
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

#ifndef __FRAMEWORKS_MEDIA_MEDIA_SERVER_H__
#define __FRAMEWORKS_MEDIA_MEDIA_SERVER_H__

#include <media_defs.h>
#include <media_focus.h>
#include <poll.h>
#include <stdbool.h>

#include "media_parcel.h"

#ifdef __cplusplus
extern "C" {
#endif

/****************************************************************************
 * Media Functions
 ****************************************************************************/

void* media_get_focus(void);
void* media_get_server(void);
#ifdef CONFIG_MEDIA_TRIGGER
void* media_get_trigger(void);
#endif

typedef struct MediadPlugin MediadPlugin;
MediadPlugin* media_plugin_get(const char* name);

/****************************************************************************
 * Stub Functions
 ****************************************************************************/

struct media_parcel;
struct media_server_conn;
void media_stub_notify_finalize(void** cookie);
void media_stub_notify_event(void* cookie, int event, int result, const char* extra);
int media_stub_reply(void* cookie, media_parcel* parcel);
int media_stub_onreceive(struct media_server_conn* conn, struct media_parcel* in, struct media_parcel* out);

int media_stub_set_stream_status(const char* name, bool active);
int media_stub_get_stream_name(const char* stream, char* name, int len);
int media_stub_process_command(const char* target, const char* cmd, const char* arg);

/****************************************************************************
 * Server Functions
 ****************************************************************************/
int media_server_notify(void* handle, void* cookie, media_parcel* parcel);
int media_server_reply(void* handle, void* cookie, media_parcel* parcel);
void media_server_finalize(void* handle, void* cookie);

void media_server_set_data(void* cookie, void* data);
void* media_server_get_data(void* cookie);
int media_server_get_tran_fd(void* cookie);
void media_server_clean_conn(void* cookie);

/****************************************************************************
 * Focus Functions
 ****************************************************************************/
typedef struct app_focus_id app_focus_id;

void media_focus_debug_stack_display(void);
int media_focus_debug_stack_return(app_focus_id* focus_list, int num);

/****************************************************************************
 * Policy Functions
 ****************************************************************************/

int media_policy_get_stream_name(const char* stream, char* name, int len);
int media_policy_set_stream_status(const char* name, bool active);
void media_policy_process_command(const char* target, const char* cmd, const char* arg);

#ifdef __cplusplus
}
#endif

#endif
