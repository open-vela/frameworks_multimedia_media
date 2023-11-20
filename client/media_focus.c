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
#include <media_focus.h>
#include <stdio.h>

#include "media_proxy.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct MediaFocusPriv {
    MEDIA_COMMON_FIELDS
    void* cookie;
    media_focus_callback suggest;
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
    priv->suggest(event, priv->cookie);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void* media_focus_request(int* suggestion, const char* stream_type,
    media_focus_callback cb, void* cookie)
{
    MediaFocusPriv* priv;

    if (!suggestion || !stream_type)
        return NULL;

    priv = zalloc(sizeof(MediaFocusPriv));
    if (!priv)
        return NULL;

    priv->suggest = cb;
    priv->cookie = cookie;

    *suggestion = media_proxy(MEDIA_ID_FOCUS, priv, stream_type, "request", NULL, 0, NULL, 0, false);
    if (*suggestion < 0)
        goto err;

    /* Create listener only if non-STOP suggestion. */
    if (*suggestion != MEDIA_FOCUS_STOP) {
        if (media_proxy_set_event_cb(priv->proxy, priv->cpu, media_suggest_cb, priv) < 0)
            goto err;

        if (media_proxy(MEDIA_ID_FOCUS, priv, NULL, "set_notify", NULL, 0, NULL, 0, false) < 0)
            goto err;
    }

    media_proxy_set_release_cb(priv->proxy, media_default_release_cb, priv);
    return priv;

err:
    media_default_release_cb(priv);
    return NULL;
}

int media_focus_abandon(void* handle)
{
    MediaFocusPriv* priv = handle;
    int ret;

    ret = media_proxy(MEDIA_ID_FOCUS,
        priv, NULL, "abandon", NULL, 0, NULL, 0, false);
    if (ret < 0 && ret != -ENOENT)
        return ret;

    return media_proxy_disconnect(priv->proxy);
}

void media_focus_dump(const char* options)
{
    media_proxy(MEDIA_ID_FOCUS, NULL, NULL, "dump", options, 0, NULL, 0, false);
}
