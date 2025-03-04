/****************************************************************************
 * frameworks/media/media_plugin.h
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
#ifndef FRAMEWORKS_MEDIA_PLUGIN_H_
#define FRAMEWORKS_MEDIA_PLUGIN_H_

struct pollfd;
struct media_server_conn;

typedef struct media_plugin_t {
    const char* name;
    int priv_size;
    void* priv;
    int (*init)(struct media_plugin_t* ctx);
    int (*uninit)(struct media_plugin_t* ctx);
    int (*get)(struct media_plugin_t* ctx, struct pollfd* fds, void** cookies, int count);
    int (*available)(struct media_plugin_t* ctx, struct pollfd* fds, void* cookies);
    int (*run_once)(struct media_plugin_t* ctx);
    int (*process_command)(struct media_plugin_t* ctx, struct media_server_conn* conn, const char* target,
        const char* cmd, const char* arg, int flags, char* res, int res_len);
} media_plugin_t;

int mediad_plugin_init(media_plugin_t* ctx);
void mediad_plugin_uinit(media_plugin_t* ctx);

#endif // FRAMEWORKS_MEDIA_PLUGIN_H_
