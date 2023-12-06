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

#include "media_metadata.h"
#include "media_proxy.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct MediaSessionPriv {
    MEDIA_COMMON_FIELDS
    void* cookie;
    media_event_callback event;
    media_metadata_t data;
    bool need_query;
} MediaSessionPriv;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void media_release_cb(void* cookie)
{
    MediaSessionPriv* priv = cookie;

    free(priv->cpu);
    free(priv);
}

static void media_event_cb(void* cookie, media_parcel* msg)
{
    MediaSessionPriv* priv = cookie;
    const char* extra;
    int32_t event;
    int32_t ret;

    media_parcel_read_scanf(msg, "%i%i%s", &event, &ret, &extra);
    if (event == MEDIA_EVENT_CHANGED || event == MEDIA_EVENT_UPDATED)
        priv->need_query = true;

    if (priv->event)
        priv->event(priv->cookie, event, ret, extra);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void* media_session_open(const char* params)
{
    MediaSessionPriv* priv;

    priv = zalloc(sizeof(MediaSessionPriv));
    if (!priv)
        return NULL;

    if (media_proxy(MEDIA_ID_SESSION, priv, NULL, "open", params, 0, NULL, 0, false) < 0)
        goto err;

    media_metadata_init(&priv->data);
    media_proxy_set_release_cb(priv->proxy, media_release_cb, priv);
    priv->need_query = true;
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

    media_metadata_deinit(&priv->data);
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

int media_session_query(void* handle, const media_metadata_t** data)
{
    MediaSessionPriv* priv = handle;
    char tmp[256];
    int ret;

    if (!priv)
        return -EINVAL;

    if (priv->need_query) {
        ret = media_proxy_once(handle, NULL, "query", NULL, 0, tmp, sizeof(tmp));
        if (ret < 0)
            return ret;

        media_metadata_reinit(&priv->data);
        ret = media_metadata_unserialize(&priv->data, tmp);
        if (ret < 0) {
            media_metadata_deinit(&priv->data);
            return ret;
        }

        if (priv->event)
            priv->need_query = false;
    }

    if (data)
        *data = &priv->data;

    return 0;
}

int media_session_get_state(void* handle, int* state)
{
    MediaSessionPriv* priv = handle;
    int ret;

    if (!state)
        return -EINVAL;

    ret = media_session_query(handle, NULL);
    if (ret < 0)
        return ret;

    if (!(priv->data.flags & MEDIA_METAFLAG_STATE))
        return -EAGAIN;

    *state = priv->data.state;
    return ret;
}

int media_session_get_position(void* handle, unsigned int* position)
{
    MediaSessionPriv* priv = handle;
    int ret;

    if (!position)
        return -EINVAL;

    ret = media_session_query(handle, NULL);
    if (ret < 0)
        return ret;

    if (!(priv->data.flags & MEDIA_METAFLAG_POSITION))
        return -EAGAIN;

    *position = priv->data.position;
    return ret;
}

int media_session_get_duration(void* handle, unsigned int* duration)
{
    MediaSessionPriv* priv = handle;
    int ret;

    if (!duration)
        return -EINVAL;

    ret = media_session_query(handle, NULL);
    if (ret < 0)
        return ret;

    if (!(priv->data.flags & MEDIA_METAFLAG_DURATION))
        return -EAGAIN;

    *duration = priv->data.duration;
    return ret;
}

int media_session_get_volume(void* handle, int* volume)
{
    MediaSessionPriv* priv = handle;
    int ret;

    if (!volume)
        return -EINVAL;

    ret = media_session_query(handle, NULL);
    if (ret < 0)
        return ret;

    if (!(priv->data.flags & MEDIA_METAFLAG_VOLUME))
        return -EAGAIN;

    *volume = priv->data.volume;
    return ret;
}

int media_session_set_volume(void* handle, int volume)
{
    /* TODO: to implement this api, we need:
     * MEDIA_EVENT_SET_VOLUME
     * MEDIA_METAFLAG_VOLUME_RANGE
     */
    return -ENOSYS;
}

int media_session_increase_volume(void* handle)
{
    return media_proxy_once(handle, NULL, "volumeup", NULL, 0, NULL, 0);
}

int media_session_decrease_volume(void* handle)
{
    return media_proxy_once(handle, NULL, "volumedown", NULL, 0, NULL, 0);
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

    snprintf(tmp, sizeof(tmp), "%d:%d", event, result);
    return media_proxy_once(handle, extra, "event", tmp, 0, NULL, 0);
}

int media_session_update(void* handle, const media_metadata_t* data)
{
    char tmp[256];

    if (!handle || !data)
        return -EINVAL;

    /* TODO:
     * should remove `utils/media_proxy.c`, let each `client/.c` marshal control message;
     * should remove `server/media_stub.c`, let each `server/.c` unmarshal control message;
     * then here we can append needed parameters to parcel, which makes parcel smaller.
     */
    media_metadata_serialize(data, tmp, sizeof(tmp));
    return media_proxy_once(handle, NULL, "update", tmp, 0, NULL, 0);
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
