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

/**
 * @brief Callback to receive parcel.
 *
 * @param cookie    Long-term context for handle.
 * @param cookie0   Short-term context for request.
 * @param cookie1   Extra short-term context.
 */
typedef void (*media_uv_parcel_callback)(
    void* cookie, void* cookie0, void* cookie1, media_parcel* parcel);

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
 * @param cookie        Long-term context, for on_connect, on_event, on_release.
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
 * @brief Async send parcel control message.
 *
 * @param handle        Handle.
 * @param on_receive    Callback to handle response message.
 * @param cookie0       Callback context of on_receive.
 * @param cookie1       Extra callback context of on_receive.
 * @param parcel
 * @return int          Zero on success, negative errno on failure.
 */
int media_uv_send(void* handle, media_uv_parcel_callback on_receive,
    void* cookie0, void* cookie1, const media_parcel* parcel);

#endif /* FRAMEWORKS_MEDIA_CLIENT_MEDIA_UV_H */
