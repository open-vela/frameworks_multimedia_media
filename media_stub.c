/****************************************************************************
 * frameworks/media/media_stub.c
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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <errno.h>
#include <malloc.h>

#include "media_internal.h"
#include "media_server.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void media_stub_notify_event(void* cookie, int event,
    int result, const char* extra)
{
    media_parcel notify;

    media_parcel_init(&notify);
    media_parcel_append_printf(&notify, "%i%i%s", event, result, extra);
    media_server_notify(media_get_server(), cookie, &notify);
    media_parcel_deinit(&notify);
}

void media_stub_onreceive(void* cookie, media_parcel* in, media_parcel* out)
{
    const char *param_s1 = NULL, *param_s2 = NULL, *param_s3 = NULL;
    int32_t param_i1 = 0, param_i2 = 0, cmd = 0, ret;
    char* response = NULL;
    uint64_t param_u = 0;
    void* handle;

    media_parcel_read_int32(in, &cmd);

    switch (cmd) {
#ifdef CONFIG_PFW
    case MEDIA_POLICY_CONTROL:
        media_parcel_read_scanf(in, "%s%s%s%i%i", &param_s1, &param_s2,
            &param_s3, &param_i1, &param_i2);
        ret = media_policy_handler(media_get_policy(), param_s1, param_s2,
            param_s3, param_i1, &response, param_i2);
        media_parcel_append_printf(out, "%i%s", ret, response);
        break;
#endif

#ifdef CONFIG_LIB_FFMPEG
    case MEDIA_GRAPH_CONTROL:
        media_parcel_read_scanf(in, "%s%s%s%i", &param_s1, &param_s2,
            &param_s3, &param_i1);
        ret = media_graph_handler(media_get_graph(), param_s1, param_s2,
            param_s3, &response, param_i1);
        media_parcel_append_printf(out, "%i%s", ret, response);
        break;

    case MEDIA_PLAYER_CONTROL:
        media_parcel_read_scanf(in, "%l%s%s%s%i", &param_u, &param_s1,
            &param_s2, &param_s3, &param_i1);
        handle = param_u ? (void*)(uintptr_t)param_u : cookie;
        ret = media_player_handler(handle, param_s1, param_s2, param_s3,
            &response, param_i1);
        media_parcel_append_printf(out, "%i%s", ret, response);
        break;

    case MEDIA_RECORDER_CONTROL:
        media_parcel_read_scanf(in, "%l%s%s%s%i", &param_u, &param_s1,
            &param_s2, &param_s3, &param_i1);
        handle = param_u ? (void*)(uintptr_t)param_u : cookie;
        ret = media_recorder_handler(handle, param_s1, param_s2, param_s3,
            &response, param_i1);
        media_parcel_append_printf(out, "%i%s", ret, response);
        break;

    case MEDIA_SESSION_CONTROL:
        media_parcel_read_scanf(in, "%l%s%s%s%i", &param_u, &param_s1,
            &param_s2, &param_s3, &param_i1);
        handle = param_u ? (void*)(uintptr_t)param_u : cookie;
        ret = media_session_handler(handle, param_s1, param_s2, param_s3,
            &response, param_i1);
        media_parcel_append_printf(out, "%i%s", ret, response);
        break;

#endif // CONFIG_LIB_FFMPEG

    default:
        (void)handle;
        (void)param_u;
        (void)param_i2;
        media_parcel_append_printf(out, "%i", -ENOSYS);
        break;
    }

    free(response);
}
