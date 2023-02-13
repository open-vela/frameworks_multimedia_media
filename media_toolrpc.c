/****************************************************************************
 * frameworks/media/media_toolrpc.c
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

#include <assert.h>
#include <errno.h>
#include <malloc.h>
#include <media_api.h>
#include <stdio.h>
#include <sys/un.h>

#ifdef MEDIA_NO_RPC

#include "media_internal.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct MediaProxyPriv {
    void* handle;
    int socket;
    struct sockaddr_un addr;
} MediaProxyPriv;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int media_prepare(void* handle, bool player,
    const char* url, const char* options)
{
    MediaProxyPriv* priv = handle;
    const char* head = "listen=1:";
    char* opt = NULL;
    char tmp[32];
    int ret;

    if (!url) {
        opt = malloc(strlen(head) + (options ? strlen(options) : 0) + 1);
        if (!opt)
            return -ENOMEM;

        strcpy(opt, head);
        if (options)
            strcat(opt, options);
        options = opt;

        priv->addr.sun_family = AF_UNIX;
        snprintf(priv->addr.sun_path, UNIX_PATH_MAX, "media%p", priv->handle);
        snprintf(tmp, sizeof(tmp), "unix:%s", priv->addr.sun_path);
        url = tmp;
    }

    if (player)
        ret = media_player_prepare_(priv->handle, url, options);
    else
        ret = media_recorder_prepare_(priv->handle, url, options);

    free(opt);
    return ret;
}

static ssize_t media_process_data(void* handle, bool player,
    void* data, size_t len)
{
    MediaProxyPriv* priv = handle;
    int ret;

    if (!data || !len) {
        if (priv->socket <= 0)
            return -EINVAL;

        close(priv->socket);
        priv->socket = 0;
        return 0;
    }

    if (priv->socket <= 0) {
        ret = socket(AF_UNIX, SOCK_STREAM, 0);
        if (ret <= 0)
            return ret;

        priv->socket = ret;

        ret = connect(priv->socket, (const struct sockaddr*)&priv->addr,
            sizeof(priv->addr));
        if (ret < 0) {
            close(priv->socket);
            priv->socket = 0;
            return ret;
        }
    }

    if (player)
        return send(priv->socket, data, len, 0);
    else
        return recv(priv->socket, data, len, 0);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void* media_player_open(const char* params)
{
    MediaProxyPriv* priv;

    priv = zalloc(sizeof(MediaProxyPriv));
    if (!priv)
        return NULL;

    priv->handle = media_player_open_(media_get_graph(), NULL);
    if (!priv->handle) {
        free(priv);
        return NULL;
    }

    return priv;
}

int media_player_close(void* handle, int pending_stop)
{
    MediaProxyPriv* priv = handle;
    int ret;

    if (!priv)
        return -EINVAL;

    if (priv->socket > 0)
        close(priv->socket);

    ret = media_player_close_(priv->handle, pending_stop);
    if (ret >= 0)
        free(priv);

    return ret;
}

int media_player_set_event_callback(void* handle, void* cookie,
    media_event_callback event_cb)
{
    MediaProxyPriv* priv = handle;

    if (!priv)
        return -EINVAL;

    return media_player_set_event_callback_(priv->handle, cookie, event_cb);
}

int media_player_prepare(void* handle, const char* url, const char* options)
{
    return media_prepare(handle, true, url, options);
}

int media_player_reset(void* handle)
{
    MediaProxyPriv* priv = handle;

    if (!priv)
        return -EINVAL;

    return media_player_reset_(priv->handle);
}

ssize_t media_player_write_data(void* handle, const void* data, size_t len)
{
    return media_process_data(handle, true, (void*)data, len);
}

int media_player_start(void* handle)
{
    MediaProxyPriv* priv = handle;

    if (!priv)
        return -EINVAL;

    return media_player_start_(priv->handle);
}

int media_player_stop(void* handle)
{
    MediaProxyPriv* priv = handle;

    if (!priv)
        return -EINVAL;

    if (priv->socket > 0) {
        close(priv->socket);
        priv->socket = 0;
    }

    return media_player_stop_(priv->handle);
}

int media_player_pause(void* handle)
{
    MediaProxyPriv* priv = handle;

    if (!priv)
        return -EINVAL;

    return media_player_pause_(priv->handle);
}

int media_player_seek(void* handle, unsigned int msec)
{
    MediaProxyPriv* priv = handle;

    if (!priv)
        return -EINVAL;

    return media_player_seek_(priv->handle, msec);
}

int media_player_set_looping(void* handle, int loop)
{
    MediaProxyPriv* priv = handle;

    if (!priv)
        return -EINVAL;

    return media_player_set_looping_(priv->handle, loop);
}

int media_player_is_playing(void* handle)
{
    MediaProxyPriv* priv = handle;

    if (!priv)
        return -EINVAL;

    return media_player_is_playing_(priv->handle);
}

int media_player_get_position(void* handle, unsigned int* msec)
{
    MediaProxyPriv* priv = handle;

    if (!priv)
        return -EINVAL;

    return media_player_get_position_(priv->handle, msec);
}

int media_player_get_duration(void* handle, unsigned int* msec)
{
    MediaProxyPriv* priv = handle;

    if (!priv)
        return -EINVAL;

    return media_player_get_duration_(priv->handle, msec);
}

int media_player_set_volume(void* handle, float volume)
{
    MediaProxyPriv* priv = handle;

    if (!priv)
        return -EINVAL;

    return media_player_set_volume_(priv->handle, volume);
}

int media_player_get_volume(void* handle, float* volume)
{
    MediaProxyPriv* priv = handle;

    if (!priv)
        return -EINVAL;

    return media_player_get_volume_(priv->handle, volume);
}

void* media_recorder_open(const char* params)
{
    MediaProxyPriv* priv;

    priv = zalloc(sizeof(MediaProxyPriv));
    if (!priv)
        return NULL;

    priv->handle = media_recorder_open_(media_get_graph(), NULL);
    if (!priv->handle) {
        free(priv);
        return NULL;
    }

    return priv;
}

int media_recorder_close(void* handle)
{
    MediaProxyPriv* priv = handle;
    int ret;

    if (!priv)
        return -EINVAL;

    if (priv->socket > 0)
        close(priv->socket);

    ret = media_recorder_close_(priv->handle);
    if (ret >= 0)
        free(priv);

    return ret;
}

int media_recorder_set_event_callback(void* handle, void* cookie,
    media_event_callback event_cb)
{
    MediaProxyPriv* priv = handle;

    if (!priv)
        return -EINVAL;

    return media_recorder_set_event_callback_(priv->handle,
        cookie, event_cb);
}

int media_recorder_prepare(void* handle, const char* url, const char* options)
{
    return media_prepare(handle, false, url, options);
}

int media_recorder_reset(void* handle)
{
    MediaProxyPriv* priv = handle;

    if (!priv)
        return -EINVAL;

    return media_recorder_reset_(priv->handle);
}

ssize_t media_recorder_read_data(void* handle, void* data, size_t len)
{
    return media_process_data(handle, false, data, len);
}

int media_recorder_start(void* handle)
{
    MediaProxyPriv* priv = handle;

    if (!priv)
        return -EINVAL;

    return media_recorder_start_(priv->handle);
}

int media_recorder_stop(void* handle)
{
    MediaProxyPriv* priv = handle;

    if (!priv)
        return -EINVAL;

    if (priv->socket > 0) {
        close(priv->socket);
        priv->socket = 0;
    }

    return media_recorder_stop_(priv->handle);
}

int media_process_command(const char* target, const char* cmd,
    const char* arg, char* res, int res_len)
{
    return media_graph_process_command(media_get_graph(), target,
        cmd, arg, res, res_len);
}

void media_dump(const char* options)
{
    media_graph_dump(media_get_graph(), options);
}

int media_policy_set_int(const char* name, int value, int apply)
{
    return media_policy_set_int_(name, value, apply);
}

int media_policy_get_int(const char* name, int* value)
{
    return media_policy_get_int_(name, value);
}

int media_policy_set_string(const char* name, const char* value, int apply)
{
    return media_policy_set_string_(name, value, apply);
}

int media_policy_get_string(const char* name, char* value, int len)
{
    return media_policy_get_string_(name, value, len);
}

int media_policy_include(const char* name, const char* values, int apply)
{
    return media_policy_include_(name, values, apply);
}

int media_policy_exclude(const char* name, const char* values, int apply)
{
    return media_policy_exclude_(name, values, apply);
}

int media_policy_increase(const char* name, int apply)
{
    return media_policy_increase_(name, apply);
}

int media_policy_decrease(const char* name, int apply)
{
    return media_policy_decrease_(name, apply);
}

#endif /* MEDIA_NO_RPC */
