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

#include <poll.h>

#include "media_parcel.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef void (*media_server_onreceive)(void *cookie, media_parcel *in, media_parcel *out);
void *media_server_create(void *cb);
int media_server_destroy(void *handle);

int media_server_get_pollfds(void *handle, struct pollfd *fds, void **conns, int count);
int media_server_poll_available(void *handle, struct pollfd *fd, void *conn);

int media_server_notify(void *handle, void *cookie, media_parcel *parcel);

#ifdef __cplusplus
}
#endif

#endif
