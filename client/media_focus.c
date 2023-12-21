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

#include "media_proxy.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct MediaFocusPriv {
    MEDIA_COMMON_FIELDS
    void* cookie;
    media_focus_callback on_suggestion;
} MediaFocusPriv;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void media_suggest_cb(void* cookie, media_parcel* msg)
{
    MediaFocusPriv* priv = cookie;
    const char* extra;
    int32_t event;
    int32_t ret;

    media_parcel_read_scanf(msg, "%i%i%s", &event, &ret, &extra);
    priv->on_suggestion(event, priv->cookie);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void* media_focus_request(int* suggestion, const char* scenario,
    media_focus_callback on_suggestion, void* cookie)
{
    MediaFocusPriv* priv;
    int ret;

    if (!suggestion || !scenario)
        return NULL;

    priv = zalloc(sizeof(MediaFocusPriv));
    if (!priv)
        return NULL;

    /* Find the focus stack and create listener. */
    ret = media_proxy(MEDIA_ID_FOCUS, priv, NULL, "ping", NULL, 0, NULL, 0, false);
    if (ret < 0) {
        media_default_release_cb(priv);
        return NULL;
    }

    priv->cookie = cookie;
    priv->on_suggestion = on_suggestion;
    media_proxy_set_release_cb(priv->proxy, media_default_release_cb, priv);
    ret = media_proxy_set_event_cb(priv->proxy, priv->cpu, media_suggest_cb, priv);
    if (ret < 0)
        goto err;

    /* Request only after acknowledge listener, or there might be suggestions missed. */
    *suggestion = media_proxy(MEDIA_ID_FOCUS, priv, scenario, "request", NULL, 0, NULL, 0, false);
    if (*suggestion < 0)
        goto err;

    return priv;

err:
    media_focus_abandon(priv);
    return NULL;
}

int media_focus_abandon(void* handle)
{
    MediaFocusPriv* priv = handle;
    int ret;

    ret = media_proxy(MEDIA_ID_FOCUS, priv, NULL, "abandon", NULL, 0, NULL, 0, false);
    if (ret < 0 && ret != -ENOENT)
        return ret;

    return media_proxy_disconnect(priv->proxy);
}

void media_focus_dump(const char* options)
{
    media_proxy(MEDIA_ID_FOCUS, NULL, NULL, "dump", options, 0, NULL, 0, false);
}
