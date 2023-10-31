/****************************************************************************
 * frameworks/media/client/media_uv_graph.c
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
#include <media_player.h>
#include <media_recorder.h>
#include <stdio.h>
#include <syslog.h>

#include "media_proxy.h"
#include "media_uv.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

#define MEDIA_STREAM_FIELDS               \
    int id;                               \
    char* name; /* stream/source name. */ \
    void* proxy;                          \
    void* cookie;                         \
    media_uv_callback on_open;            \
    media_uv_callback on_close;           \
    media_event_callback on_event;

typedef struct MediaStreamPriv {
    MEDIA_STREAM_FIELDS
} MediaStreamPriv;

typedef struct MediaPlayerPriv {
    MEDIA_STREAM_FIELDS
} MediaPlayerPriv;

typedef struct MediaRecorderPriv {
    MEDIA_STREAM_FIELDS
} MediaRecorderPriv;

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static void media_uv_stream_open_cb(void* cookie, int ret);
static void media_uv_stream_connect_cb(void* cookie, int ret);
static void media_uv_stream_release_cb(void* cookie, int ret);
static void media_uv_stream_event_cb(void* cookie,
    void* cookie0, void* cookie1, media_parcel* parcel);
static void media_uv_stream_receive_cb(void* cookie,
    void* cookie0, void* cookie1, media_parcel* parcel);
static void media_uv_stream_receive_int_cb(void* cookie,
    void* cookie0, void* cookie1, media_parcel* parcel);
static void media_uv_stream_receive_unsigned_cb(void* cookie,
    void* cookie0, void* cookie1, media_parcel* parcel);
static int media_uv_stream_send(void* stream, const char* target,
    const char* cmd, const char* arg, int res_len,
    media_uv_parcel_callback parser, void* cb, void* cookie);

/****************************************************************************
 * Common Functions
 ****************************************************************************/

static void media_uv_stream_open_cb(void* cookie, int ret)
{
    MediaStreamPriv* priv = cookie;

    if (ret < 0)
        media_uv_reconnect(priv->proxy);
    else {
        syslog(LOG_INFO, "open:%s result:%d handle:%p\n", priv->name, ret, priv);
        if (priv->on_open)
            priv->on_open(priv->cookie, ret);
    }
}

static void media_uv_stream_connect_cb(void* cookie, int ret)
{
    MediaStreamPriv* priv = cookie;

    if (ret >= 0)
        media_uv_stream_send(priv, NULL, "open", priv->name, 0,
            media_uv_stream_receive_cb, media_uv_stream_open_cb, priv);
    else if (priv->on_open)
        priv->on_open(priv->cookie, ret);
}

static void media_uv_stream_release_cb(void* cookie, int ret)
{
    MediaStreamPriv* priv = cookie;

    if (priv->on_close)
        priv->on_close(priv->cookie, ret);

    free(priv);
}

static void media_uv_stream_event_cb(void* cookie, void* cookie0, void* cookie1,
    media_parcel* parcel)
{
    int32_t event = MEDIA_EVENT_NOP, result = -ECANCELED;
    MediaStreamPriv* priv = cookie;
    const char* response = NULL;

    if (parcel)
        media_parcel_read_scanf(parcel, "%i%i%s", &event, &result, &response);

    priv->on_event(priv->cookie, event, result, response);
}

static void media_uv_stream_receive_cb(void* cookie,
    void* cookie0, void* cookie1, media_parcel* parcel)
{
    media_uv_callback cb = cookie0;
    int32_t result = -ECANCELED;

    if (parcel)
        media_parcel_read_scanf(parcel, "%i%s", &result, NULL);

    cb(cookie1, result);
}

static void media_uv_stream_receive_int_cb(void* cookie,
    void* cookie0, void* cookie1, media_parcel* parcel)
{
    media_uv_int_callback cb = cookie0;
    const char* response = NULL;
    int32_t result = -ECANCELED;
    int value = 0;

    if (parcel) {
        media_parcel_read_scanf(parcel, "%i%s", &result, &response);
        if (response)
            value = strtol(response, NULL, 0);
    }

    cb(cookie1, result, value);
}

static void media_uv_stream_receive_unsigned_cb(void* cookie,
    void* cookie0, void* cookie1, media_parcel* parcel)
{
    media_uv_unsigned_callback cb = cookie0;
    const char* response = NULL;
    int32_t result = -ECANCELED;
    unsigned value = 0;

    if (parcel) {
        media_parcel_read_scanf(parcel, "%i%s", &result, &response);
        if (response)
            value = strtoul(response, NULL, 0);
    }

    cb(cookie1, result, value);
}

static int media_uv_stream_send(void* stream, const char* target,
    const char* cmd, const char* arg, int res_len,
    media_uv_parcel_callback parser, void* cb, void* cookie)
{
    MediaStreamPriv* priv = stream;
    media_parcel parcel;
    int ret;

    /* Parse response only if the command support response and user cares. */
    if (!cb)
        parser = NULL;

    media_parcel_init(&parcel);
    ret = media_parcel_append_printf(&parcel, "%i%s%s%s%i",
        priv->id, target, cmd, arg, res_len);
    if (ret < 0)
        return ret;

    ret = media_uv_send(priv->proxy, parser, cb, cookie, &parcel);
    media_parcel_deinit(&parcel);
    return ret;
}

/****************************************************************************
 * Player Functions
 ****************************************************************************/

void* media_uv_player_open(void* loop, const char* stream,
    media_uv_callback on_open, void* cookie)
{
    MediaPlayerPriv* priv;

    if (stream && stream[0] != '\0') {
        priv = zalloc(sizeof(MediaPlayerPriv) + strlen(stream) + 1);
        if (!priv)
            return NULL;

        priv->name = (char*)(priv + 1);
        strcpy(priv->name, stream);
    } else {
        priv = zalloc(sizeof(MediaPlayerPriv));
        if (!priv)
            return NULL;
    }

    priv->cookie = cookie;
    priv->on_open = on_open;
    priv->id = MEDIA_ID_PLAYER;
    priv->proxy = media_uv_connect(loop, CONFIG_MEDIA_SERVER_CPUNAME,
        media_uv_stream_connect_cb, priv);
    if (!priv->proxy) {
        media_uv_stream_release_cb(priv, 0);
        return NULL;
    }

    return priv;
}

int media_uv_player_close(void* handle, int pending, media_uv_callback on_close)
{
    MediaPlayerPriv* priv = handle;
    char tmp[32];
    int ret;

    if (!priv)
        return -EINVAL;

    priv->on_close = on_close;
    snprintf(tmp, sizeof(tmp), "%d", pending);
    ret = media_uv_stream_send(priv, NULL, "close", tmp, 0, NULL, NULL, NULL);
    if (ret < 0)
        return ret;

    return media_uv_disconnect(priv->proxy, media_uv_stream_release_cb);
}

int media_uv_player_listen(void* handle, media_event_callback on_event)
{
    MediaPlayerPriv* priv = handle;
    int ret;

    if (!priv || !on_event)
        return -EINVAL;

    priv->on_event = on_event;
    ret = media_uv_stream_send(priv, NULL, "set_event", NULL, 0, NULL, NULL, NULL);
    if (ret < 0)
        return ret;

    return media_uv_listen(priv->proxy, media_uv_stream_event_cb);
}

int media_uv_player_prepare(void* handle, const char* url, const char* options,
    media_uv_callback cb, void* cookie)
{
    int ret = 0;

    if (!handle)
        return -EINVAL;

    if (!url || !url[0])
        return -ENOSYS; /* TODO: need buffer mode support. */

    if (options && options[0] != '\0')
        ret = media_uv_stream_send(handle, NULL, "set_options", options, 0, NULL, NULL, NULL);

    if (ret >= 0)
        ret = media_uv_stream_send(handle, NULL, "prepare", url, 0,
            media_uv_stream_receive_cb, cb, cookie);

    return ret;
}

int media_uv_player_start(void* handle, media_uv_callback cb, void* cookie)
{
    if (!handle)
        return -EINVAL;

    return media_uv_stream_send(handle, NULL, "start", NULL, 0,
        media_uv_stream_receive_cb, cb, cookie);
}

int media_uv_player_pause(void* handle, media_uv_callback cb, void* cookie)
{
    if (!handle)
        return -EINVAL;

    return media_uv_stream_send(handle, NULL, "pause", NULL, 0,
        media_uv_stream_receive_cb, cb, cookie);
}

int media_uv_player_stop(void* handle, media_uv_callback cb, void* cookie)
{
    if (!handle)
        return -EINVAL;

    return media_uv_stream_send(handle, NULL, "stop", NULL, 0,
        media_uv_stream_receive_cb, cb, cookie);
}

int media_uv_player_set_volume(void* handle, float volume,
    media_uv_callback cb, void* cookie)
{
    char tmp[32];

    if (!handle)
        return -EINVAL;

    snprintf(tmp, sizeof(tmp), "%f", volume);
    return media_uv_stream_send(handle, "volume", "volume", tmp, 0,
        media_uv_stream_receive_cb, cb, cookie);
}

int media_uv_player_get_playing(void* handle,
    media_uv_int_callback cb, void* cookie)
{
    if (!handle)
        return -EINVAL;

    return media_uv_stream_send(handle, NULL, "get_playing", NULL, 32,
        media_uv_stream_receive_int_cb, cb, cookie);
}

int media_uv_player_get_position(void* handle,
    media_uv_unsigned_callback cb, void* cookie)
{
    if (!handle)
        return -EINVAL;

    return media_uv_stream_send(handle, NULL, "get_position", NULL, 32,
        media_uv_stream_receive_unsigned_cb, cb, cookie);
}

int media_uv_player_get_duration(void* handle,
    media_uv_unsigned_callback cb, void* cookie)
{
    if (!handle)
        return -EINVAL;

    return media_uv_stream_send(handle, NULL, "get_duration", NULL, 32,
        media_uv_stream_receive_unsigned_cb, cb, cookie);
}

/****************************************************************************
 * Recorder Functions
 ****************************************************************************/

void* media_uv_recorder_open(void* loop, const char* source,
    media_uv_callback on_open, void* cookie)
{
    MediaRecorderPriv* priv;

    if (source && source[0] != '\0') {
        priv = zalloc(sizeof(MediaRecorderPriv) + strlen(source) + 1);
        if (!priv)
            return NULL;

        priv->name = (char*)(priv + 1);
        strcpy(priv->name, source);
    } else {
        priv = zalloc(sizeof(MediaRecorderPriv));
        if (!priv)
            return NULL;
    }

    priv->cookie = cookie;
    priv->on_open = on_open;
    priv->id = MEDIA_ID_RECORDER;
    priv->proxy = media_uv_connect(loop, CONFIG_MEDIA_SERVER_CPUNAME,
        media_uv_stream_connect_cb, priv);
    if (!priv->proxy) {
        media_uv_stream_release_cb(priv, 0);
        return NULL;
    }

    return priv;
}

int media_uv_recorder_close(void* handle, media_uv_callback on_close)
{
    MediaRecorderPriv* priv = handle;
    int ret;

    if (!priv)
        return -EINVAL;

    priv->on_close = on_close;
    ret = media_uv_stream_send(priv, NULL, "close", NULL, 0, NULL, NULL, NULL);
    if (ret < 0)
        return ret;

    return media_uv_disconnect(priv->proxy, media_uv_stream_release_cb);
}

int media_uv_recorder_listen(void* handle, media_event_callback on_event)
{
    MediaRecorderPriv* priv = handle;
    int ret;

    if (!priv || !on_event)
        return -EINVAL;

    priv->on_event = on_event;
    ret = media_uv_stream_send(priv, NULL, "set_event", NULL, 0, NULL, NULL, NULL);
    if (ret < 0)
        return ret;

    return media_uv_listen(priv->proxy, media_uv_stream_event_cb);
}

int media_uv_recorder_prepare(void* handle, const char* url, const char* options,
    media_uv_callback cb, void* cookie)
{
    int ret = 0;

    if (!handle)
        return -EINVAL;

    if (!url || !url[0])
        return -ENOSYS; /* TODO: need buffer mode support. */

    if (options && options[0] != '\0')
        ret = media_uv_stream_send(handle, NULL, "set_options", options, 0, NULL, NULL, NULL);

    if (ret >= 0)
        ret = media_uv_stream_send(handle, NULL, "prepare", url, 0,
            media_uv_stream_receive_cb, cb, cookie);

    return ret;
}

int media_uv_recorder_start(void* handle, media_uv_callback cb, void* cookie)
{
    if (!handle)
        return -EINVAL;

    return media_uv_stream_send(handle, NULL, "start", NULL, 0,
        media_uv_stream_receive_cb, cb, cookie);
}

int media_uv_recorder_pause(void* handle, media_uv_callback cb, void* cookie)
{
    if (!handle)
        return -EINVAL;

    return media_uv_stream_send(handle, NULL, "pause", NULL, 0,
        media_uv_stream_receive_cb, cb, cookie);
}

int media_uv_recorder_stop(void* handle, media_uv_callback cb, void* cookie)
{
    if (!handle)
        return -EINVAL;

    return media_uv_stream_send(handle, NULL, "stop", NULL, 0,
        media_uv_stream_receive_cb, cb, cookie);
}
