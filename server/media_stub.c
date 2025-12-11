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
#include <string.h>

#include "media_common.h"
#include "media_plugin.h"
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

static inline int media_plugin_command(MediadPlugin* plugin, struct media_server_conn* conn, const char* target,
    const char* cmd, const char* arg, int flags, char* res, int res_len)
{
    if (plugin->process_command)
        return plugin->process_command(plugin, conn, target, cmd, arg, flags, res, res_len);
    return -ENOSYS;
}

int media_stub_reply(void* cookie, media_parcel* parcel)
{
    return media_server_reply(media_get_server(), cookie, parcel);
}

int media_stub_onreceive(struct media_server_conn* conn, media_parcel* in, media_parcel* out)
{
    const char *target = NULL, *cmd = NULL, *arg = NULL;
    int32_t len = 0, flags = 0, id = 0, size = 0, ret = 0;
    char* response = NULL;

    media_parcel_read_int32(in, &id);

    switch (id) {
#ifdef CONFIG_LIB_PFW
    case MEDIA_ID_POLICY:
        media_parcel_read_scanf(in, "%s%s%s%i%i", &target, &cmd, &arg, &flags, &len);
        if (len > 0)
            response = zalloc(len);

        ret = media_plugin_command(media_plugin_get("media_policy"), conn, target, cmd, arg, flags, response, len);
        break;
#endif

#ifdef CONFIG_MEDIA_FOCUS
    case MEDIA_ID_FOCUS:
        media_parcel_read_scanf(in, "%s%s%s%i", &target, &cmd, &arg, &len);
        if (len > 0)
            response = zalloc(len);

        ret = media_plugin_command(media_plugin_get("media_focus"), conn, target, cmd, arg, 0, response, len);
        break;
#endif

#ifdef CONFIG_LIB_FFMPEG
    case MEDIA_ID_GRAPH:
        media_parcel_read_scanf(in, "%s%s%s%i", &target, &cmd, &arg, &len);
        if (len > 0)
            response = zalloc(len);

        ret = media_plugin_command(media_plugin_get("audio_graph"), NULL, target, cmd, arg, 0, response, len);
        break;

    case MEDIA_ID_PLAYER:
        media_parcel_read_scanf(in, "%s%s%s%i", &target, &cmd, &arg, &len);
        if (len > 0)
            response = zalloc(len);

        ret = media_plugin_command(media_plugin_get("media_player"), conn, target, cmd, arg, 0, response, len);
        break;

    case MEDIA_ID_RECORDER:
        media_parcel_read_scanf(in, "%s%s%s%i", &target, &cmd, &arg, &len);
        if (len > 0)
            response = zalloc(len);

        ret = media_plugin_command(media_plugin_get("media_recorder"), conn, target, cmd, arg, 0, response, len);
        break;

    case MEDIA_ID_SESSION:
        media_parcel_read_scanf(in, "%s%s%s%i", &target, &cmd, &arg, &len);
        if (len > 0)
            response = zalloc(len);

        ret = media_plugin_command(media_plugin_get("media_session"), conn, target, cmd, arg, 0, response, len);
        break;

#endif // CONFIG_LIB_FFMPEG

#ifdef CONFIG_MEDIA_TRIGGER
    case MEDIA_ID_TRIGGER:
        media_parcel_read_scanf(in, "%s%s%i%i", &cmd, &arg, &size, &len);
        if (len > 0)
            response = zalloc(len);

        ret = media_plugin_command(media_plugin_get("media_trigger"), conn, NULL, cmd, arg, size, response, len);
        break;
#endif

    default:
        (void)target;
        (void)cmd;
        (void)arg;
        (void)len;
        (void)size;
        (void)flags;
        ret = -ENOSYS;
        break;
    }

    if (out && ret != MEDIA_ERROR_DELAY_ACK)
        media_parcel_append_printf(out, "%i%s", ret, response);

    if (ret < 0) {
        MEDIA_INFO("%s:%s %s %s %" PRId32 " %s\n",
            media_id_get_name(id), target ? target : "_", cmd, arg ? arg : "_",
            ret, response ? response : "_");
    }

    free(response);

    return ret;
}

int media_stub_set_stream_status(const char* name, bool active)
{
#ifdef CONFIG_LIB_PFW
    const char* cmd = active ? "include" : "exclude";

    name = strchr(name, '@') + 1;
    return media_plugin_command(media_plugin_get("media_policy"), NULL, "ActiveStreams", cmd, name, 1, NULL, 0);
#else
    return -ENOSYS;
#endif
}

int media_stub_get_stream_name(const char* stream, char* name, int len)
{
#ifdef CONFIG_LIB_PFW
    return media_plugin_command(media_plugin_get("media_policy"), NULL, stream, "get_string", NULL, 0, name, len);
#else
    return -ENOSYS;
#endif
}

int media_stub_process_command(const char* target,
    const char* cmd, const char* arg)
{
#ifdef CONFIG_LIB_FFMPEG
    return media_plugin_command(media_plugin_get("audio_graph"), NULL, target, cmd, arg, 0, NULL, 0);
#else
    return -ENOSYS;
#endif
}
