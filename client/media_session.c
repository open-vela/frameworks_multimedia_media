/****************************************************************************
 * frameworks/media/client/media_session.c
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
#include <media_policy.h>
#include <media_session.h>
#include <stdio.h>

#include "media_proxy.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MEDIA_IS_STATUS_CHANGE(x) ((x) < 200)

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct MediaSessionPriv {
    MEDIA_COMMON_FIELDS
    void* cookie;
    media_event_callback event;
    char* stream_type;
} MediaSessionPriv;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void media_release_cb(void* cookie)
{
    MediaSessionPriv* priv = cookie;

    free(priv->stream_type);
    free(priv->cpu);
    free(priv);
}

static void media_event_cb(void* cookie, media_parcel* msg)
{
    MediaSessionPriv* priv = cookie;
    const char* extra;
    int32_t event;
    int32_t ret;

    if (priv->event) {
        media_parcel_read_scanf(msg, "%i%i%s", &event, &ret, &extra);
        priv->event(priv->cookie, event, ret, extra);
    }
}

static int media_get_proper_stream(void* handle, char* stream_type, int len)
{
    MediaSessionPriv* priv = handle;
    int ret;

    if (!handle)
        return -EINVAL;

    ret = media_proxy(MEDIA_ID_FOCUS,
        NULL, NULL, "peek", NULL, 0, stream_type, len, false);
    if (ret <= 0)
        ret = snprintf(stream_type, len, "%s", priv->stream_type);

    return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void* media_session_open(const char* params)
{
    MediaSessionPriv* priv;

    if (!params)
        return NULL;

    priv = zalloc(sizeof(MediaSessionPriv));
    if (!priv)
        return NULL;

    if (media_proxy(MEDIA_ID_SESSION, priv, NULL, "open", params, 0, NULL, 0, false) < 0)
        goto err;

    priv->stream_type = strdup(params);
    if (!priv->stream_type)
        goto err;

    media_proxy_set_release_cb(priv->proxy, media_release_cb, priv);
    return priv;

err:
    media_release_cb(priv);
    return NULL;
}

int media_session_set_event_callback(void* handle, void* cookie,
    media_event_callback event_cb)
{
    MediaSessionPriv* priv = handle;
    int ret;

    if (!priv)
        return -EINVAL;

    ret = media_proxy_set_event_cb(priv->proxy, priv->cpu, media_event_cb, priv);
    if (ret < 0)
        return ret;

    ret = media_proxy_once(handle, NULL, "set_event", NULL, 0, NULL, 0);
    if (ret < 0)
        return ret;

    priv->event = event_cb;
    priv->cookie = cookie;

    return ret;
}

int media_session_close(void* handle)
{
    MediaSessionPriv* priv = handle;
    int ret;

    if (!handle)
        return -EINVAL;

    ret = media_proxy_once(priv, NULL, "close", NULL, 0, NULL, 0);
    if (ret < 0)
        return ret;

    return media_proxy_disconnect(priv->proxy);
}

int media_session_start(void* handle)
{
    return media_proxy_once(handle, NULL, "start", NULL, 0, NULL, 0);
}

int media_session_pause(void* handle)
{
    return media_proxy_once(handle, NULL, "pause", NULL, 0, NULL, 0);
}

int media_session_seek(void* handle, unsigned int msec)
{
    char tmp[32];

    snprintf(tmp, sizeof(tmp), "%u", msec);
    return media_proxy_once(handle, NULL, "seek", tmp, 0, NULL, 0);
}

int media_session_stop(void* handle)
{
    return media_proxy_once(handle, NULL, "stop", NULL, 0, NULL, 0);
}

int media_session_get_state(void* handle, int* state)
{
    return -ENOSYS;
}

int media_session_get_position(void* handle, unsigned int* msec)
{
    return -ENOSYS;
}

int media_session_get_duration(void* handle, unsigned int* msec)
{
    return -ENOSYS;
}

int media_session_set_volume(void* handle, int volume)
{
    char tmp[32];
    int ret;

    ret = media_get_proper_stream(handle, tmp, sizeof(tmp));
    if (ret < 0)
        return ret;

    return media_policy_set_stream_volume(tmp, volume);
}

int media_session_get_volume(void* handle, int* volume)
{
    char tmp[32];
    int ret;

    ret = media_get_proper_stream(handle, tmp, sizeof(tmp));
    if (ret < 0)
        return ret;

    return media_policy_get_stream_volume(tmp, volume);
}

int media_session_increase_volume(void* handle)
{
    char tmp[32];
    int ret;

    ret = media_get_proper_stream(handle, tmp, sizeof(tmp));
    if (ret < 0)
        return ret;

    return media_policy_increase_stream_volume(tmp);
}

int media_session_decrease_volume(void* handle)
{
    char tmp[32];
    int ret;

    ret = media_get_proper_stream(handle, tmp, sizeof(tmp));
    if (ret < 0)
        return ret;

    return media_policy_decrease_stream_volume(tmp);
}

int media_session_next_song(void* handle)
{
    return media_proxy_once(handle, NULL, "next", NULL, 0, NULL, 0);
}

int media_session_prev_song(void* handle)
{
    return media_proxy_once(handle, NULL, "prev", NULL, 0, NULL, 0);
}

void* media_session_register(void* cookie, media_event_callback event_cb)
{
    MediaSessionPriv* priv;

    priv = zalloc(sizeof(MediaSessionPriv));
    if (!priv)
        return NULL;

    if (media_proxy(MEDIA_ID_SESSION, priv, NULL, "register", NULL, 0, NULL, 0, 0) < 0)
        goto err;

    if (media_proxy_set_event_cb(priv->proxy, priv->cpu, media_event_cb, priv) < 0)
        goto err;

    priv->event = event_cb;
    priv->cookie = cookie;
    media_proxy_set_release_cb(priv->proxy, media_release_cb, priv);
    return priv;

err:
    media_release_cb(priv);
    return NULL;
}

int media_session_notify(void* handle, int event, int result, const char* extra)
{
    char tmp[64];

    if (!MEDIA_IS_STATUS_CHANGE(event))
        return -EINVAL;

    snprintf(tmp, sizeof(tmp), "%d:%d", event, result);
    return media_proxy_once(handle, extra, "event", tmp, 0, NULL, 0);
}

int media_session_unregister(void* handle)
{
    MediaSessionPriv* priv = handle;
    int ret;

    if (!handle)
        return -EINVAL;

    ret = media_proxy_once(priv, NULL, "unregister", NULL, 0, NULL, 0);
    if (ret < 0)
        return ret;

    return media_proxy_disconnect(priv->proxy);
}
