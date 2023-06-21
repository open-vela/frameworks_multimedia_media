/****************************************************************************
 * frameworks/media/meida_client.h
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

#ifndef __FRAMEWORKS_MEDIA_MEDIA_CLIENT_H__
#define __FRAMEWORKS_MEDIA_MEDIA_CLIENT_H__

#include "media_parcel.h"

#ifdef __cplusplus
extern "C" {
#endif

void* media_client_connect(const char* cpu);
int media_client_disconnect(void* handle);

int media_client_send(void* handle, media_parcel* in);
int media_client_send_with_ack(void* handle, media_parcel* in, media_parcel* out);
int media_client_send_recieve(void* handle, const char* in_fmt, const char* out_fmt, ...);

typedef void (*media_client_event_cb)(void* cookie, media_parcel* parcel);
int media_client_set_event_cb(void* handle, const char* cpu,
    void* event_cb, void* cookie);

typedef void (*media_client_release_cb)(void* cookie);
int media_client_set_release_cb(void* handle, void* release_cb, void* cookie);

#ifdef __cplusplus
}
#endif

#endif
