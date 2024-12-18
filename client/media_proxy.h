/****************************************************************************
 * frameworks/media/client/media_proxy.h
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

#ifndef __FRAMEWORKS_MEDIA_UTILS_MEDIA_PROXY_H__
#define __FRAMEWORKS_MEDIA_UTILS_MEDIA_PROXY_H__

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdbool.h>

#include "media_parcel.h"

#ifdef __cplusplus
extern "C" {
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Common fields of rpc handle. */
#define MEDIA_COMMON_FIELDS \
    int type;               \
    void* proxy;            \
    char* cpu;

/****************************************************************************
 * Public Types
 ****************************************************************************/

typedef void (*media_proxy_event_cb)(void* cookie, media_parcel* parcel);
typedef void (*media_proxy_release_cb)(void* cookie);

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void* media_proxy_connect(const char* cpu);
int media_proxy_disconnect(void* handle);

int media_proxy_send(void* handle, media_parcel* in);
int media_proxy_send_with_ack(void* handle, media_parcel* in, media_parcel* out);
int media_proxy_send_recieve(void* handle, const char* in_fmt, const char* out_fmt, ...);

int media_proxy_set_event_cb(void* handle, const char* cpu,
    void* event_cb, void* cookie);
int media_proxy_set_release_cb(void* handle, void* release_cb, void* cookie);

/**
 * @brief Transact a command to server through existed long connection.
 *
 * @param handle    Instance handle.
 * @param target
 * @param cmd
 * @param arg
 * @param apply     For Policy: wether apply to hw,
 *                  For Session: event sent to player client.
 * @param res       Response address.
 * @param res_len   Response max lenghth.
 * @return int      Zero on success; a negated errno value on failure.
 */
int media_proxy_once(void* handle, const char* target, const char* cmd,
    const char* arg, int apply, char* res, int res_len);

/**
 * @brief Transact a command to server.
 *
 * @param id        Type ID.
 * @param handle    Instance handle.
 * @param target
 * @param cmd
 * @param arg
 * @param apply     For Policy: wether apply to hw,
 *                  For Session: event sent to player client.
 * @param res       Response address.
 * @param res_len   Response max lenghth.
 * @return int      Zero on success; a negated errno value on failure.
 */
int media_proxy(int id, void* handle, const char* target, const char* cmd,
    const char* arg, int apply, char* res, int res_len);

void media_default_release_cb(void* handle);

#ifdef __cplusplus
}
#endif

#endif
