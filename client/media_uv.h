/****************************************************************************
 * frameworks/media/client/media_uv.h
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

#ifndef FRAMEWORKS_MEDIA_CLIENT_MEDIA_UV_H
#define FRAMEWORKS_MEDIA_CLIENT_MEDIA_UV_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <media_event.h>

#include "media_parcel.h"

/****************************************************************************
 * Public Type
 ****************************************************************************/

typedef void (*media_uv_parcel_callback)(void* cookie, media_parcel* parcel);

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/**
 * @brief Build a long-term rpc connection async.
 *
 * @param loop          Current uv loop handle.
 * @param cpus          Server cpu list to connect.
 * @param on_connect    Call after connect is done; you must send first
 *                      control message here with ack=true, otherwise further
 *                      control meesages would not be send. @see media_uv_send.
 * @param cookie        Context for all callback.
 * @return void*        Handle.
 */
void* media_uv_connect(void* loop, const char* cpus,
    media_uv_callback on_connect, void* cookie);

/**
 * @brief Disconnect the connection.
 *
 * @param handle        Handle.
 * @param on_release    Call after disconnect and socket receive EOF.
 * @note @won't auto release even connect is failed.
 */
int media_uv_disconnect(void* handle, media_uv_callback on_release);

/**
 * @brief Reconnect to the next server cpu.
 *
 * @param handle    Handle.
 * @return int      Zero on success, negative errno on failure.
 */
int media_uv_reconnect(void* handle);

/**
 * @brief Create listener connection.
 *
 * @param handle    Handle.
 * @param on_event  Call after receiving event notification from server.
 * @return int      Zero on success, negative errno on failure.
 */
int media_uv_listen(void* handle, media_uv_parcel_callback on_event);

/**
 * @brief Async send control message.
 *
 * @param handle        Handle.
 * @param on_receive    Callback to handle response message.
 * @param cookie        Callback context of on_receive.
 * @param fmt           Format string.
 * @param ...
 * @return int          Zero on success, negative errno on failure.
 * @note You must send first control message in `on_connect` method, other control
 * messages would be pending till response of first control message accept in
 * `on_receive` method.
 */
int media_uv_send(void* handle, media_uv_parcel_callback on_receive, void* cookie,
    const char* fmt, ...);

#endif /* FRAMEWORKS_MEDIA_CLIENT_MEDIA_UV_H */
