/****************************************************************************
 * frameworks/media/server/media_stub.c
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

#include "media_proxy.h"
#include "media_server.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void media_stub_notify_finalize(void** cookie)
{
    if (cookie) {
        media_server_finalize(media_get_server(), *cookie);
        *cookie = NULL;
    }
}

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
    const char *target = NULL, *cmd = NULL, *arg = NULL;
    int32_t len = 0, flags = 0, id = 0, ret;
    char* response = NULL;

    media_parcel_read_int32(in, &id);

    switch (id) {
#ifdef CONFIG_LIB_PFW
    case MEDIA_ID_POLICY:
        media_parcel_read_scanf(in, "%s%s%s%i%i", &target, &cmd, &arg, &flags, &len);
        if (len > 0)
            response = zalloc(len);

        ret = media_policy_handler(media_get_policy(), target, cmd, arg, flags, response, len);
        media_parcel_append_printf(out, "%i%s", ret, response);
        break;
#endif

#ifdef CONFIG_MEDIA_FOCUS
    case MEDIA_ID_FOCUS:
        media_parcel_read_scanf(in, "%s%s%i", &target, &cmd, &len);
        if (len > 0)
            response = zalloc(len);

        ret = media_focus_handler(media_get_focus(), cookie, target, cmd, response, len);
        media_parcel_append_printf(out, "%i%s", ret, response);
        break;
#endif

#ifdef CONFIG_LIB_FFMPEG
    case MEDIA_ID_GRAPH:
        media_parcel_read_scanf(in, "%s%s%s%i", &target, &cmd, &arg, &len);
        if (len > 0)
            response = zalloc(len);

        ret = media_graph_handler(media_get_graph(), target, cmd, arg, response, len);
        media_parcel_append_printf(out, "%i%s", ret, response);
        break;

    case MEDIA_ID_PLAYER:
        media_parcel_read_scanf(in, "%s%s%s%i", &target, &cmd, &arg, &len);
        if (len > 0)
            response = zalloc(len);

        ret = media_player_handler(media_get_graph(), cookie, target, cmd, arg, response, len);
        media_parcel_append_printf(out, "%i%s", ret, response);
        break;

    case MEDIA_ID_RECORDER:
        media_parcel_read_scanf(in, "%s%s%s%i", &target, &cmd, &arg, &len);
        if (len > 0)
            response = zalloc(len);

        ret = media_recorder_handler(media_get_graph(), cookie, target, cmd, arg, response, len);
        media_parcel_append_printf(out, "%i%s", ret, response);
        break;

    case MEDIA_ID_SESSION:
        media_parcel_read_scanf(in, "%s%s%s%i", &target, &cmd, &arg, &len);
        if (len > 0)
            response = zalloc(len);

        ret = media_session_handler(media_get_session(), cookie, target, cmd, arg, response, len);
        media_parcel_append_printf(out, "%i%s", ret, response);
        break;

#endif // CONFIG_LIB_FFMPEG

    default:
        (void)target;
        (void)cmd;
        (void)arg;
        (void)len;
        (void)flags;
        (void)ret;
        media_parcel_append_printf(out, "%i", -ENOSYS);
        break;
    }

    free(response);
}
