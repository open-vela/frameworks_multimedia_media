/****************************************************************************
 * frameworks/media/client/media_uv_session.c
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
#include <media_session.h>
#include <stdio.h>

#include "media_log.h"
#include "media_metadata.h"
#include "media_proxy.h"
#include "media_uv.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct MediaSessionPriv {
    void* proxy;
    void* cookie;
    media_uv_callback on_open;
    media_uv_callback on_close;
    media_event_callback on_event;
    media_metadata_t data;
    bool need_query;
} MediaSessionPriv;

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static void media_uv_session_close_cb(void* cookie, int ret);
static void media_uv_session_event_cb(void* cookie, void* cookie0,
    void* cookie1, media_parcel* parcel);
static void media_uv_session_receive_cb(void* cookie,
    void* cookie0, void* cookie1, media_parcel* parcel);
static int media_uv_session_send(void* handle, const char* target,
    const char* cmd, const char* arg, int res_len,
    media_uv_parcel_callback parser, void* cb, void* cookie);

/* Controller functions. */

static void media_uv_controller_receive_metadata_cb(void* cookie,
    void* cookie0, void* cookie1, media_parcel* parcel);
static void media_uv_controller_open_cb(void* cookie, int ret);
static void media_uv_controller_connect_cb(void* cookie, int ret);

/* Controllee functions. */

static void media_uv_controllee_register_cb(void* cookie, int ret);
static void media_uv_controllee_listen_cb(void* cookie, int ret);
static void media_uv_controllee_ping_cb(void* cookie, int ret);
static void media_uv_controllee_connect_cb(void* cookie, int ret);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void media_uv_session_close_cb(void* cookie, int ret)
{
    MediaSessionPriv* priv = cookie;

    if (priv->on_close)
        priv->on_close(priv->cookie, ret);

    media_metadata_deinit(&priv->data);
    free(priv);
}

static void media_uv_session_event_cb(void* cookie, void* cookie0,
    void* cookie1, media_parcel* parcel)
{
    int32_t event = MEDIA_EVENT_NOP, result = -ECANCELED;
    MediaSessionPriv* priv = cookie;
    const char* response = NULL;

    if (parcel)
        media_parcel_read_scanf(parcel, "%i%i%s", &event,
            &result, &response);

    if (event == MEDIA_EVENT_CHANGED || event == MEDIA_EVENT_UPDATED)
        priv->need_query = true;

    priv->on_event(priv->cookie, event, result, response);
}

static void media_uv_session_receive_cb(void* cookie,
    void* cookie0, void* cookie1, media_parcel* parcel)
{
    media_uv_callback cb = cookie0;
    int32_t result = -ECANCELED;

    if (parcel)
        media_parcel_read_scanf(parcel, "%i%s", &result, NULL);

    cb(cookie1, result);
}

static int media_uv_session_send(void* handle, const char* target,
    const char* cmd, const char* arg, int res_len,
    media_uv_parcel_callback parser, void* cb, void* cookie)
{
    MediaSessionPriv* priv = handle;
    media_parcel parcel;
    int ret;

    media_parcel_init(&parcel);
    ret = media_parcel_append_printf(&parcel, "%i%s%s%s%i",
        MEDIA_ID_SESSION, target, cmd, arg, res_len);
    if (ret < 0)
        return ret;

    ret = media_uv_send(priv->proxy, parser, cb, cookie, &parcel);
    media_parcel_deinit(&parcel);
    return ret;
}

/****************************************************************************
 * Controller Functions
 ****************************************************************************/

static void media_uv_controller_receive_metadata_cb(void* cookie,
    void* cookie0, void* cookie1, media_parcel* parcel)
{
    media_uv_object_callback cb = cookie0;
    MediaSessionPriv* priv = cookie;
    int32_t result = -ECANCELED;
    const char* response = NULL;
    media_metadata_t diff;

    if (parcel)
        media_parcel_read_scanf(parcel, "%i%s", &result, &response);

    if (result < 0)
        cb(cookie1, result, NULL);

    /* Update metadata and notify controller user. */
    media_metadata_init(&diff);
    media_metadata_unserialize(&diff, response);
    media_metadata_update(&priv->data, &diff);
    cb(cookie1, 0, &priv->data);
}

static void media_uv_controller_open_cb(void* cookie, int ret)
{
    MediaSessionPriv* priv = cookie;

    if (ret < 0)
        media_uv_reconnect(priv->proxy);
    else {
        MEDIA_INFO("result:%d handle:%p\n", ret, priv);
        if (priv->on_open)
            priv->on_open(priv->cookie, ret);
    }
}

static void media_uv_controller_connect_cb(void* cookie, int ret)
{
    MediaSessionPriv* priv = cookie;

    if (ret >= 0)
        ret = media_uv_session_send(priv, NULL, "open", NULL, 0,
            media_uv_session_receive_cb, media_uv_controller_open_cb, priv);

    if (ret < 0 && priv->on_open)
        priv->on_open(priv->cookie, ret);
}

void* media_uv_session_open(void* loop, char* params,
    media_uv_callback on_open, void* cookie)
{
    MediaSessionPriv* priv;

    priv = zalloc(sizeof(MediaSessionPriv));
    if (!priv)
        return NULL;

    priv->cookie = cookie;
    priv->on_open = on_open;
    media_metadata_init(&priv->data);
    priv->need_query = true;
    priv->proxy = media_uv_connect(loop, CONFIG_MEDIA_SERVER_CPUNAME,
        media_uv_controller_connect_cb, priv);
    if (!priv->proxy) {
        free(priv);
        return NULL;
    }

    return priv;
}

int media_uv_session_close(void* handle, media_uv_callback on_close)
{
    MediaSessionPriv* priv = handle;
    int ret;

    if (!handle)
        return -EINVAL;

    priv->on_close = on_close;
    ret = media_uv_session_send(priv, NULL, "close", NULL, 0,
        NULL, NULL, NULL);
    if (ret < 0)
        return ret;

    return media_uv_disconnect(priv->proxy, media_uv_session_close_cb);
}

int media_uv_session_listen(void* handle, media_event_callback on_event)
{
    MediaSessionPriv* priv = handle;
    int ret;

    if (!priv)
        return -EINVAL;

    priv->on_event = on_event;
    ret = media_uv_session_send(priv, NULL, "set_event",
        NULL, 0, NULL, NULL, NULL);
    if (ret < 0)
        return ret;

    return media_uv_listen(priv->proxy, NULL, media_uv_session_event_cb);
}

int media_uv_session_start(void* handle, media_uv_callback on_start, void* cookie)
{
    if (!handle)
        return -EINVAL;

    return media_uv_session_send(handle, NULL, "start", NULL, 0,
        media_uv_session_receive_cb, on_start, cookie);
}

int media_uv_session_pause(void* handle, media_uv_callback on_pause, void* cookie)
{
    if (!handle)
        return -EINVAL;

    return media_uv_session_send(handle, NULL, "pause", NULL, 0,
        media_uv_session_receive_cb, on_pause, cookie);
}

int media_uv_session_seek(void* handle, unsigned int msec,
    media_uv_callback on_seek, void* cookie)
{
    char tmp[64];

    if (!handle)
        return -EINVAL;

    snprintf(tmp, sizeof(tmp), "%u", msec);
    return media_uv_session_send(handle, NULL, "seek", tmp, 0,
        media_uv_session_receive_cb, on_seek, cookie);
}

int media_uv_session_stop(void* handle, media_uv_callback on_stop, void* cookie)
{
    if (!handle)
        return -EINVAL;

    return media_uv_session_send(handle, NULL, "stop", NULL, 0,
        media_uv_session_receive_cb, on_stop, cookie);
}

int media_uv_session_query(void* handle,
    media_uv_object_callback on_query, void* cookie)
{
    MediaSessionPriv* priv = handle;
    int ret;

    if (!priv || !on_query)
        return -EINVAL;

    if (!priv->need_query) {
        on_query(priv->cookie, 0, &priv->data);
        return 0;
    }

    ret = media_uv_session_send(handle, NULL, "query", NULL, 256,
        media_uv_controller_receive_metadata_cb, on_query, cookie);
    if (ret < 0)
        return ret;

    if (priv->on_event)
        priv->need_query = false;

    return ret;
}

int media_uv_session_get_state(void* handle,
    media_uv_int_callback on_state, void* cookie)
{
    /* XXX: Can impl based on media_uv_controller_receive_metadata_cb, but might not need. */
    return -ENOSYS;
}

int media_uv_session_get_position(void* handle,
    media_uv_unsigned_callback on_position, void* cookie)
{
    /* XXX: Can impl based on media_uv_controller_receive_metadata_cb, but might not need. */
    return -ENOSYS;
}

int media_uv_session_get_duration(void* handle,
    media_uv_unsigned_callback on_duration, void* cookie)
{
    /* XXX: Can impl based on media_uv_controller_receive_metadata_cb, but might not need. */
    return -ENOSYS;
}

int media_uv_session_get_volume(void* handle,
    media_uv_int_callback on_volume, void* cookie)
{
    /* XXX: Can impl based on media_uv_controller_receive_metadata_cb, but might not need. */
    return -ENOSYS;
}

int media_uv_session_set_volume(void* handle, int volume,
    media_uv_callback on_volume, void* cookie)
{
    return -ENOSYS; /* TODO: impl by event. */
}

int media_uv_session_increase_volume(void* handle,
    media_uv_callback on_increase, void* cookie)
{
    if (!handle)
        return -EINVAL;

    return media_uv_session_send(handle, NULL, "volumeup", NULL, 0,
        media_uv_session_receive_cb, on_increase, cookie);
}

int media_uv_session_decrease_volume(void* handle,
    media_uv_callback on_decrease, void* cookie)
{
    if (!handle)
        return -EINVAL;

    return media_uv_session_send(handle, NULL, "volumedown", NULL, 0,
        media_uv_session_receive_cb, on_decrease, cookie);
}

int media_uv_session_next_song(void* handle,
    media_uv_callback on_next_song, void* cookie)
{
    if (!handle)
        return -EINVAL;

    return media_uv_session_send(handle, NULL, "next", NULL, 0,
        media_uv_session_receive_cb, on_next_song, cookie);
}

int media_uv_session_prev_song(void* handle,
    media_uv_callback on_pre_song, void* cookie)
{
    if (!handle)
        return -EINVAL;

    return media_uv_session_send(handle, NULL, "prev", NULL, 0,
        media_uv_session_receive_cb, on_pre_song, cookie);
}

/****************************************************************************
 * Controllee Functions
 ****************************************************************************/

static void media_uv_controllee_register_cb(void* cookie, int ret)
{
    MediaSessionPriv* priv = cookie;

    priv->on_event(priv->cookie, MEDIA_EVENT_NOP, ret, NULL);
}

static void media_uv_controllee_listen_cb(void* cookie, int ret)
{
    MediaSessionPriv* priv = cookie;

    /* Only register after acknowledge listener created. */
    if (ret >= 0)
        ret = media_uv_session_send(priv, NULL, "register", NULL, 0,
            media_uv_session_receive_cb, media_uv_controllee_register_cb, priv);

    if (ret < 0)
        priv->on_event(priv->cookie, MEDIA_EVENT_NOP, ret, NULL);
}

static void media_uv_controllee_ping_cb(void* cookie, int ret)
{
    MediaSessionPriv* priv = cookie;

    if (ret < 0)
        media_uv_reconnect(priv->proxy);
    else
        media_uv_listen(priv->proxy,
            media_uv_controllee_listen_cb, media_uv_session_event_cb);
}

static void media_uv_controllee_connect_cb(void* cookie, int ret)
{
    MediaSessionPriv* priv = cookie;

    if (ret >= 0)
        ret = media_uv_session_send(priv, NULL, "ping", NULL, 0,
            media_uv_session_receive_cb, media_uv_controllee_ping_cb, priv);

    if (ret < 0)
        priv->on_event(priv->cookie, MEDIA_EVENT_NOP, ret, NULL);
}

void* media_uv_session_register(void* loop, const char* params,
    media_event_callback on_event, void* cookie)
{
    MediaSessionPriv* priv;

    if (!on_event)
        return NULL;

    priv = zalloc(sizeof(MediaSessionPriv));
    if (!priv)
        return NULL;

    priv->cookie = cookie;
    priv->on_event = on_event;

    priv->proxy = media_uv_connect(loop, CONFIG_MEDIA_SERVER_CPUNAME,
        media_uv_controllee_connect_cb, priv);
    if (!priv->proxy) {
        free(priv);
        return NULL;
    }

    return priv;
}

int media_uv_session_unregister(void* handle, media_uv_callback on_release)
{
    MediaSessionPriv* priv = handle;
    int ret;

    if (!handle)
        return -EINVAL;

    priv->on_close = on_release;
    ret = media_uv_session_send(handle, NULL, "unregister", NULL, 0, NULL, NULL, NULL);
    if (ret < 0)
        return ret;

    return media_uv_disconnect(priv->proxy, media_uv_session_close_cb);
}

int media_uv_session_notify(void* handle, int event, int result, const char* extra,
    media_uv_callback on_notify, void* cookie)
{
    char tmp[64];

    if (!handle)
        return -EINVAL;

    snprintf(tmp, sizeof(tmp), "%d:%d", event, result);
    return media_uv_session_send(handle, extra, "event", tmp, 0,
        media_uv_session_receive_cb, on_notify, cookie);
}

int media_uv_session_update(void* handle, const media_metadata_t* data,
    media_uv_callback on_update, void* cookie)
{
    char tmp[256];

    if (!handle || !data)
        return -EINVAL;

    media_metadata_serialize(data, tmp, sizeof(tmp));
    return media_uv_session_send(handle, NULL, "update", tmp, 0,
        media_uv_session_receive_cb, on_update, cookie);
}
