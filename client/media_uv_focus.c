/****************************************************************************
 * frameworks/media/client/media_uv_focus.c
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
#include <media_defs.h>
#include <media_focus.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "media_common.h"
#include "media_uv.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct MediaFocusPriv {
    void* proxy;
    char* name; /* TODO: use independent focus type. */
    int auto_reply;
    media_focus_callback on_suggest;
    media_focus_callback2 on_suggest2;
    media_uv_callback on_abandon;
    void* cookie;
} MediaFocusPriv;

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int media_uv_focus_send(MediaFocusPriv* priv,
    const char* target, const char* cmd, media_uv_callback cb);
static void media_uv_focus_event_cb(void* cookie,
    void* cookie0, void* cookie1, media_parcel* parcel);
static void media_uv_focus_receive_cb(void* cookie,
    void* cookie0, void* cookie1, media_parcel* parcel);
static void media_uv_focus_ping_cb(void* cookie, int ret);
static void media_uv_focus_request_cb(void* cookie, int ret);
static void media_uv_focus_abandon_cb(void* cookie, int ret);

static void media_uv_focus_listen_cb(void* cookie, int ret);
static void media_uv_focus_release_cb(void* cookie, int ret);
static void media_uv_focus_connect_cb(void* cookie, int ret);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int media_uv_focus_send(MediaFocusPriv* priv,
    const char* target, const char* cmd, media_uv_callback cb)
{
    media_parcel parcel;
    int ret;

    media_parcel_init(&parcel);
    ret = media_parcel_append_printf(&parcel, "%i%s%s%s%i",
        MEDIA_ID_FOCUS, target, cmd, "", 0);
    if (ret < 0)
        return ret;

    ret = media_uv_send(priv->proxy, cb ? media_uv_focus_receive_cb : NULL,
        cb, priv, &parcel);

    media_parcel_deinit(&parcel);
    return ret;
}

static int media_uv_focus_reply_to_notification(MediaFocusPriv* priv, int media_id, int req_id)
{
    media_parcel parcel;
    char arg[16];
    int ret = 0;

    media_parcel_init(&parcel);
    snprintf(arg, sizeof(arg), "%d", req_id);
    ret = media_parcel_append_printf(&parcel, "%i%s%s%s%i", media_id, "", "reply", arg, 0);
    if (ret < 0)
        return ret;

    ret = media_uv_send(priv->proxy, NULL, NULL, NULL, &parcel);

    media_parcel_deinit(&parcel);
    return ret;
}

static void media_uv_focus_event_cb(void* cookie,
    void* cookie0, void* cookie1, media_parcel* parcel)
{
    MediaFocusPriv* priv = cookie;
    int32_t suggest = MEDIA_FOCUS_STOP;
    int32_t req_id = 0;

    if (parcel) {
        media_parcel_read_int32(parcel, &suggest);
        media_parcel_read_int32(parcel, &req_id);
    }

    MEDIA_INFO("%s:%p suggest:%" PRId32 " req_id:%" PRId32 "\n", priv->name, priv, suggest, req_id);
    if (priv->on_suggest) {
        priv->on_suggest(suggest, priv->cookie);
        media_uv_focus_reply_to_notification(priv, MEDIA_ID_FOCUS, req_id);
        return;
    }

    priv->on_suggest2(suggest, req_id, priv->cookie);
    if (req_id >= 0 && priv->auto_reply)
        media_uv_focus_reply_to_notification(priv, MEDIA_ID_FOCUS, req_id);
}

static void media_uv_focus_receive_cb(void* cookie,
    void* cookie0, void* cookie1, media_parcel* parcel)
{
    media_uv_callback cb = cookie0;
    int32_t result = -ECANCELED;

    if (parcel)
        media_parcel_read_int32(parcel, &result);

    cb(cookie1, result);
}

static void media_uv_focus_on_suggest(void* cookie, int ret)
{
    MediaFocusPriv* priv = cookie;

    if (!priv->on_suggest2)
        priv->on_suggest(ret, priv->cookie);
    else
        priv->on_suggest2(ret, -1, priv->cookie);
}

static void media_uv_focus_connect_cb(void* cookie, int ret)
{
    MediaFocusPriv* priv = cookie;

    if (ret < 0)
        media_uv_focus_on_suggest(priv, ret);
    else
        media_uv_focus_send(priv, NULL, "ping", media_uv_focus_ping_cb);
}

static void media_uv_focus_ping_cb(void* cookie, int ret)
{
    MediaFocusPriv* priv = cookie;

    if (ret < 0)
        media_uv_reconnect(priv->proxy);
    else
        media_uv_listen(priv->proxy,
            media_uv_focus_listen_cb, media_uv_focus_event_cb);
}

static void media_uv_focus_listen_cb(void* cookie, int ret)
{
    MediaFocusPriv* priv = cookie;

    /* Should only request after acknowledge listener is ready,
     * or there might be suggestion missed.
     */
    if (ret < 0)
        media_uv_focus_on_suggest(priv, ret);
    else
        media_uv_focus_send(priv, priv->name, "request",
            media_uv_focus_request_cb);
}

static void media_uv_focus_request_cb(void* cookie, int ret)
{
    MediaFocusPriv* priv = cookie;

    if (priv->on_abandon)
        return;

    MEDIA_INFO("%s:%p suggest:%d\n", priv->name, priv, ret);
    media_uv_focus_on_suggest(priv, ret);
}

static void media_uv_focus_abandon_cb(void* cookie, int ret)
{
    MediaFocusPriv* priv = cookie;

    MEDIA_INFO("%s:%p ret:%d\n", priv->name, priv, ret);
    media_uv_disconnect(priv->proxy, media_uv_focus_release_cb);
}

static void media_uv_focus_release_cb(void* cookie, int ret)
{
    MediaFocusPriv* priv = cookie;

    if (priv->on_abandon)
        priv->on_abandon(priv->cookie, ret);

    free(priv);
}

static void* media_uv_focus_request_l(void* loop, const char* name,
    media_focus_callback on_suggest, media_focus_callback2 on_suggest2,
    int auto_reply, void* cookie)
{
    MediaFocusPriv* priv;

    if (!name || name[0] == '\0' || (!on_suggest && !on_suggest2))
        return NULL;

    priv = zalloc(sizeof(MediaFocusPriv) + strlen(name) + 1);
    if (!priv)
        return NULL;

    priv->name = (char*)(priv + 1);
    strcpy(priv->name, name);

    priv->cookie = cookie;
    priv->auto_reply = auto_reply;
    priv->on_suggest = on_suggest;
    priv->on_suggest2 = on_suggest2;
    priv->proxy = media_uv_connect(loop, media_get_cpuname(),
        media_uv_focus_connect_cb, priv);
    if (!priv->proxy) {
        media_uv_focus_release_cb(priv, 0);
        return NULL;
    }

    MEDIA_INFO("%s:%p\n", priv->name, priv);
    return priv;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void* media_uv_focus_request(void* loop, const char* name,
    media_focus_callback on_suggest, void* cookie)
{
    return media_uv_focus_request_l(loop, name, on_suggest, NULL, 0, cookie);
}

void* media_uv_focus_request2(void* loop, const char* name,
    media_focus_callback2 on_suggest, int auto_reply, void* cookie)
{
    return media_uv_focus_request_l(loop, name, NULL, on_suggest, auto_reply, cookie);
}

int media_uv_focus_abandon(void* handle, media_uv_callback on_abandon)
{
    MediaFocusPriv* priv = handle;
    int ret;

    if (!priv)
        return -EINVAL;

    priv->on_abandon = on_abandon;
    ret = media_uv_focus_send(priv, NULL, "abandon", media_uv_focus_abandon_cb);
    MEDIA_INFO("%s:%p ret:%d\n", priv->name, priv, ret);
    return ret;
}

int media_uv_focus_reply(void* handle, int req_id)
{
    MediaFocusPriv* priv = handle;
    int ret = 0;

    if (!priv || req_id < 0 || !priv->on_suggest2 || priv->auto_reply)
        return -EINVAL;

    ret = media_uv_focus_reply_to_notification(priv, MEDIA_ID_FOCUS, req_id);
    MEDIA_INFO("%s:%p ret:%d\n", priv->name, priv, ret);
    return ret;
}
