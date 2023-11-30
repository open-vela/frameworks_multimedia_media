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

#include "media_proxy.h"
#include "media_uv.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MEDIA_IS_STATUS_CHANGE(x) ((x) < 200)

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct MediaSessionPriv {
    void* proxy;
    void* cookie;
    media_event_callback on_event;
    media_uv_callback on_open;
    media_uv_callback on_close;
} MediaSessionPriv;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void media_uv_session_close_cb(void* cookie, int ret)
{
    MediaSessionPriv* priv = cookie;

    if (priv->on_close)
        priv->on_close(priv->cookie, ret);

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

static void media_uv_session_open_cb(void* cookie, int ret)
{
    MediaSessionPriv* priv = cookie;

    if (ret < 0)
        media_uv_reconnect(priv->proxy);
    else {
        syslog(LOG_INFO, "[%s][%d] result:%d handle:%p\n",
            __func__, __LINE__, ret, priv);
        if (priv->on_open)
            priv->on_open(priv->cookie, ret);
    }
}

static int media_uv_mession_send(void* handle, const char* target,
    const char* cmd, const char* arg, int res_len,
    media_uv_parcel_callback parser, void* cb, void* cookie)
{
    MediaSessionPriv* priv = handle;
    media_parcel parcel;
    int ret;

    /* Parse response only if the command support
       response and user cares. */
    if (!cb)
        parser = NULL;

    media_parcel_init(&parcel);
    ret = media_parcel_append_printf(&parcel, "%i%s%s%s%i",
        MEDIA_ID_SESSION, target, cmd, arg, res_len);
    if (ret < 0)
        return ret;

    ret = media_uv_send(priv->proxy, parser, cb, cookie, &parcel);
    media_parcel_deinit(&parcel);
    return ret;
}

static void media_uv_session_connect_cb(void* cookie, int ret)
{
    MediaSessionPriv* priv = cookie;

    if (ret >= 0)
        ret = media_uv_mession_send(priv, NULL, "open", NULL, 0,
            media_uv_session_receive_cb, media_uv_session_open_cb,
            priv->cookie);
    if (ret < 0 && priv->on_open)
        priv->on_open(priv->cookie, ret);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void* media_uv_session_open(void* loop, char* params,
    media_uv_callback on_open, void* cookie)
{
    MediaSessionPriv* priv;

    priv = zalloc(sizeof(MediaSessionPriv));
    if (!priv)
        return NULL;

    priv->cookie = cookie;
    priv->on_open = on_open;

    priv->proxy = media_uv_connect(loop, CONFIG_MEDIA_SERVER_CPUNAME,
        media_uv_session_connect_cb, priv);
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
    ret = media_uv_mession_send(priv, NULL, "close", NULL, 0,
        NULL, NULL, NULL);
    if (ret < 0)
        return ret;

    if (ret >= 0)
        ret = media_uv_disconnect(priv->proxy, media_uv_session_close_cb);

    return ret;
}

int media_uv_session_listen(void* handle, media_event_callback on_event)
{
    MediaSessionPriv* priv = handle;
    int ret;

    if (!priv)
        return -EINVAL;

    priv->on_event = on_event;
    ret = media_uv_mession_send(priv, NULL, "set_event",
        NULL, 0, NULL, NULL, NULL);
    if (ret < 0)
        return ret;

    return media_uv_listen(priv->proxy, media_uv_session_event_cb);
}

int media_uv_session_start(void* handle, media_uv_callback on_start, void* cookie)
{
    if (!handle)
        return -EINVAL;

    return media_uv_mession_send(handle, NULL, "start", NULL, 0,
        media_uv_session_receive_cb, on_start, cookie);
}

int media_uv_session_pause(void* handle, media_uv_callback on_pause, void* cookie)
{
    if (!handle)
        return -EINVAL;

    return media_uv_mession_send(handle, NULL, "pause", NULL, 0,
        media_uv_session_receive_cb, on_pause, cookie);
}

int media_uv_session_seek(void* handle, unsigned int msec,
    media_uv_callback on_seek, void* cookie)
{
    char tmp[64];

    if (!handle)
        return -EINVAL;

    snprintf(tmp, sizeof(tmp), "%u", msec);
    return media_uv_mession_send(handle, NULL, "seek", tmp, 0,
        media_uv_session_receive_cb, on_seek, cookie);
}

int media_uv_session_stop(void* handle, media_uv_callback on_stop, void* cookie)
{
    if (!handle)
        return -EINVAL;

    return media_uv_mession_send(handle, NULL, "stop", NULL, 0,
        media_uv_session_receive_cb, on_stop, cookie);
}

int media_uv_session_get_state(void* handle,
    media_uv_int_callback on_state, void* cookie)
{
    return -ENOSYS;
}

int media_uv_session_get_position(void* handle,
    media_uv_int_callback on_position, void* cookie)
{
    return -ENOSYS;
}

int media_uv_session_get_duration(void* handle,
    media_uv_int_callback on_duriation, void* cookie)
{
    return -ENOSYS;
}

int media_uv_session_set_volume(void* handle, unsigned int* volume,
    media_uv_callback on_set_volume, void* cookie)
{
    return -ENOSYS;
}

int media_uv_session_get_volume(void* handle,
    media_uv_int_callback on_get_volume, void* cookie)
{
    return -ENOSYS;
}

int media_uv_session_increase_volume(void* handle,
    media_uv_callback on_increase, void* cookie)
{
    return -ENOSYS;
}

int media_uv_session_decrease_volume(void* handle,
    media_uv_callback on_decrease, void* cookie)
{
    return -ENOSYS;
}

int media_uv_session_next_song(void* handle,
    media_uv_callback on_next_song, void* cookie)
{
    if (!handle)
        return -EINVAL;

    return media_uv_mession_send(handle, NULL, "next", NULL, 0,
        media_uv_session_receive_cb, on_next_song, cookie);
}

int media_uv_session_prev_song(void* handle,
    media_uv_callback on_pre_song, void* cookie)
{
    if (!handle)
        return -EINVAL;

    return media_uv_mession_send(handle, NULL, "prev", NULL, 0,
        media_uv_session_receive_cb, on_pre_song, cookie);
}