/****************************************************************************
 * frameworks/media/client/media_focus.c
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

#include "media_common.h"
#include "media_proxy.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct MediaFocusPriv {
    MEDIA_COMMON_FIELDS
    void* cookie;
    int auto_reply;
    media_focus_callback on_suggestion;
    media_focus_callback2 on_suggestion2;
} MediaFocusPriv;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int media_focus_reply_to_notification(void* handle, int media_id, int req_id)
{
    MediaFocusPriv* priv = handle;
    media_parcel parcel;
    char arg[16] = { 0 };
    int ret = 0;

    if (priv == NULL)
        return -EINVAL;

    media_parcel_init(&parcel);
    snprintf(arg, sizeof(arg), "%d", req_id);
    ret = media_parcel_append_printf(&parcel, "%i%s%s%s%i", media_id, "", "reply", arg, 0);
    if (ret < 0)
        return ret;

    ret = media_proxy_send(priv->proxy, &parcel);

    media_parcel_deinit(&parcel);
    return ret;
}

static void media_suggest_cb(void* cookie, media_parcel* msg)
{
    MediaFocusPriv* priv = cookie;
    const char* extra;
    int32_t req_id;
    int32_t event;

    media_parcel_read_scanf(msg, "%i%i%s", &event, &req_id, &extra);
    if (priv->on_suggestion) {
        priv->on_suggestion(event, priv->cookie);
        media_focus_reply_to_notification(priv, MEDIA_ID_FOCUS, req_id);
        return;
    }

    priv->on_suggestion2(event, req_id, priv->cookie);
    if (priv->auto_reply)
        media_focus_reply_to_notification(priv, MEDIA_ID_FOCUS, req_id);
}

static int media_focus_abandon_l(void* handle, bool enforce)
{
    MediaFocusPriv* priv = handle;
    int ret;

    ret = media_proxy_once(priv, NULL, "abandon", NULL, 0, NULL, 0);
    if (ret < 0 && ret != -ENOENT && !enforce)
        return ret;

    return media_proxy_disconnect(priv->proxy);
}

static void* media_focus_request_l(int* suggestion, const char* scenario,
    media_focus_callback on_suggestion, media_focus_callback2 on_suggestion2,
    int auto_reply, void* cookie)
{
    MediaFocusPriv* priv;
    int ret;

    if (!suggestion || !scenario)
        return NULL;

    priv = calloc(1, sizeof(MediaFocusPriv));
    if (!priv)
        return NULL;

    /* Find the focus stack and create listener. */
    ret = media_proxy(MEDIA_ID_FOCUS, priv, NULL, "ping", NULL, 0, NULL, 0);
    if (ret < 0) {
        media_default_release_cb(priv);
        return NULL;
    }

    priv->cookie = cookie;
    priv->auto_reply = auto_reply;
    priv->on_suggestion = on_suggestion;
    priv->on_suggestion2 = on_suggestion2;
    media_proxy_set_release_cb(priv->proxy, media_default_release_cb, priv);
    ret = media_proxy_set_event_cb(priv->proxy, priv->cpu, media_suggest_cb, priv);
    if (ret < 0)
        goto err;

    /* Request only after acknowledge listener, or there might be suggestions missed. */
    *suggestion = media_proxy_once(priv, scenario, "request", NULL, 0, NULL, 0);
    if (*suggestion < 0)
        goto err;

    return priv;

err:
    media_focus_abandon_l(priv, true);
    return NULL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void* media_focus_request(int* suggestion, const char* scenario,
    media_focus_callback on_suggestion, void* cookie)
{
    return media_focus_request_l(suggestion, scenario, on_suggestion, NULL, 0, cookie);
}

void* media_focus_request2(int* suggestion, const char* scenario,
    media_focus_callback2 on_suggestion, int auto_reply, void* cookie)
{
    return media_focus_request_l(suggestion, scenario, NULL, on_suggestion, auto_reply, cookie);
}

int media_focus_abandon(void* handle)
{
    return media_focus_abandon_l(handle, false);
}

int media_focus_reply(void* handle, int req_id)
{
    MediaFocusPriv* priv = handle;

    if (!priv || req_id < 0 || !priv->on_suggestion2 || priv->auto_reply)
        return -EINVAL;

    return media_focus_reply_to_notification(priv, MEDIA_ID_FOCUS, req_id);
}

void media_focus_dump(const char* options)
{
    media_proxy(MEDIA_ID_FOCUS, NULL, NULL, "dump", options, 0, NULL, 0);
}
