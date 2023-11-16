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
#include <media_focus.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "media_proxy.h"
#include "media_uv.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct MediaFocusPriv {
    void* proxy;
    char* name; /* TODO: use independent focus type. */
    media_focus_callback on_suggest;
    media_uv_callback on_abandon;
    void* cookie;
} MediaFocusPriv;

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int media_uv_focus_send(MediaFocusPriv* priv,
    const char* target, const char* cmd, media_uv_parcel_callback parser);
static void media_uv_focus_event_cb(void* cookie,
    void* cookie0, void* cookie1, media_parcel* parcel);
static void media_uv_focus_request_cb(void* cookie,
    void* cookie0, void* cookie1, media_parcel* parcel);
static void media_uv_focus_release_cb(void* cookie, int ret);
static void media_uv_focus_connect_cb(void* cookie, int ret);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int media_uv_focus_send(MediaFocusPriv* priv,
    const char* target, const char* cmd, media_uv_parcel_callback parser)
{
    media_parcel parcel;
    int ret;

    media_parcel_init(&parcel);
    ret = media_parcel_append_printf(&parcel, "%i%s%s%i",
        MEDIA_ID_FOCUS, target, cmd, 0);
    if (ret < 0)
        return ret;

    ret = media_uv_send(priv->proxy, parser, NULL, NULL, &parcel);
    media_parcel_deinit(&parcel);
    return ret;
}

static void media_uv_focus_event_cb(void* cookie,
    void* cookie0, void* cookie1, media_parcel* parcel)
{
    int32_t suggest = MEDIA_EVENT_STOP;
    MediaFocusPriv* priv = cookie;

    if (parcel)
        media_parcel_read_int32(parcel, &suggest);

    priv->on_suggest(suggest, priv->cookie);
}

static void media_uv_focus_request_cb(void* cookie,
    void* cookie0, void* cookie1, media_parcel* parcel)
{
    MediaFocusPriv* priv = cookie;
    int32_t suggest;

    /* Shall not create listener if not enter focus stack. */
    media_parcel_read_int32(parcel, &suggest);
    if (suggest != MEDIA_FOCUS_STOP) {
        media_uv_listen(priv->proxy, media_uv_focus_event_cb);
        media_uv_focus_send(priv, NULL, "set_notify", NULL);
        /* Might be pending suggestions during creating listener. */
    }

    priv->on_suggest(suggest, priv->cookie);
}

static void media_uv_focus_release_cb(void* cookie, int ret)
{
    MediaFocusPriv* priv = cookie;

    if (priv->on_abandon)
        priv->on_abandon(priv->cookie, ret);

    free(priv);
}

static void media_uv_focus_connect_cb(void* cookie, int ret)
{
    MediaFocusPriv* priv = cookie;

    if (ret < 0)
        priv->on_suggest(ret, priv->cookie);
    else
        media_uv_focus_send(priv, priv->name, "request",
            media_uv_focus_request_cb);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void* media_uv_focus_request(void* loop, const char* name,
    media_focus_callback on_suggest, void* cookie)
{
    MediaFocusPriv* priv;

    if (!name || name[0] == '\0' || !on_suggest)
        return NULL;

    priv = zalloc(sizeof(MediaFocusPriv) + strlen(name) + 1);
    if (!priv)
        return NULL;

    priv->name = (char*)(priv + 1);
    strcpy(priv->name, name);

    priv->cookie = cookie;
    priv->on_suggest = on_suggest;
    priv->proxy = media_uv_connect(loop, CONFIG_MEDIA_SERVER_CPUNAME,
        media_uv_focus_connect_cb, priv);
    if (!priv->cookie) {
        media_uv_focus_release_cb(priv, 0);
        return NULL;
    }

    return priv;
}

int media_uv_focus_abandon(void* handle, media_uv_callback on_abandon)
{
    MediaFocusPriv* priv = handle;
    int ret;

    if (!priv)
        return -EINVAL;

    priv->on_abandon = on_abandon;
    ret = media_uv_focus_send(priv, NULL, "abandon", NULL);
    if (ret < 0)
        return ret;

    return media_uv_disconnect(priv->proxy, media_uv_focus_release_cb);
}
