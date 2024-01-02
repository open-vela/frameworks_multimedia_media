/****************************************************************************
 * frameworks/media/client/media_uv_policy.c
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
#include <stdio.h>

#include "media_common.h"
#include "media_uv.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct MediaPolicyPriv {
    void* proxy;
    media_parcel parcel;
    media_uv_parcel_callback parser;
    void* cb;
    void* cookie;
} MediaPolicyPriv;

/****************************************************************************
 * Private Functions Prototypes
 ****************************************************************************/

static void media_uv_policy_free(MediaPolicyPriv* priv);
static MediaPolicyPriv*
media_uv_policy_alloc(const char* name, const char* cmd, const char* value,
    int apply, int len, media_uv_parcel_callback parser, void* cb, void* cookie);

static void media_uv_policy_release_cb(void* cookie, int ret);
static void media_uv_policy_connect_cb(void* cookie, int ret);

static void media_uv_policy_receive_cb(void* cookie,
    void* cookie0, void* cookie1, media_parcel* parcel);
static void media_uv_policy_receive_int_cb(void* cookie,
    void* cookie0, void* cookie1, media_parcel* parcel);
static void media_uv_policy_receive_string_cb(void* cookie,
    void* cookie0, void* cookie1, media_parcel* parcel);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void media_uv_policy_free(MediaPolicyPriv* priv)
{
    if (priv) {
        media_parcel_deinit(&priv->parcel);
        free(priv);
    }
}

static MediaPolicyPriv*
media_uv_policy_alloc(const char* name, const char* cmd, const char* value,
    int apply, int len, media_uv_parcel_callback parser, void* cb, void* cookie)
{
    MediaPolicyPriv* priv;

    priv = zalloc(sizeof(MediaPolicyPriv));
    if (!priv)
        return NULL;

    media_parcel_init(&priv->parcel);
    if (media_parcel_append_printf(&priv->parcel, "%i%s%s%s%i%i",
            MEDIA_ID_POLICY, name, cmd, value, apply, len)
        < 0) {
        media_uv_policy_free(priv);
        return NULL;
    }

    priv->parser = parser;
    priv->cookie = cookie;
    priv->cb = cb;
    return priv;
}

static void media_uv_policy_release_cb(void* cookie, int ret)
{
    MediaPolicyPriv* priv = cookie;

    media_uv_policy_free(priv);
}

static void media_uv_policy_connect_cb(void* cookie, int ret)
{
    MediaPolicyPriv* priv = cookie;

    if (ret == -ENOENT)
        media_uv_disconnect(priv->proxy, media_uv_policy_release_cb);

    if (ret >= 0)
        ret = media_uv_send(priv->proxy, priv->parser, priv->cb, priv->cookie, &priv->parcel);

    if (ret < 0)
        media_uv_reconnect(priv->proxy);
    else if (!priv->cb) /* Response not needed. */
        media_uv_disconnect(priv->proxy, media_uv_policy_release_cb);
}

static void media_uv_policy_receive_cb(void* cookie,
    void* cookie0, void* cookie1, media_parcel* parcel)
{
    MediaPolicyPriv* priv = cookie;
    media_uv_callback cb = cookie0;
    int32_t result = -ECANCELED;

    if (cb) {
        if (parcel)
            media_parcel_read_scanf(parcel, "%i%s", &result, NULL);

        cb(cookie1, result);
    }

    media_uv_disconnect(priv->proxy, media_uv_policy_release_cb);
}

static void media_uv_policy_receive_int_cb(void* cookie,
    void* cookie0, void* cookie1, media_parcel* parcel)
{
    media_uv_int_callback cb = cookie0;
    MediaPolicyPriv* priv = cookie;
    const char* response = NULL;
    int32_t result = -ECANCELED;
    int value = 0;

    if (cb) {
        if (parcel) {
            media_parcel_read_scanf(parcel, "%i%s", &result, &response);
            if (response)
                value = strtol(response, NULL, 0);
        }

        cb(cookie1, result, value);
    }

    media_uv_disconnect(priv->proxy, media_uv_policy_release_cb);
}

static void media_uv_policy_receive_string_cb(void* cookie,
    void* cookie0, void* cookie1, media_parcel* parcel)
{
    media_uv_string_callback cb = cookie0;
    MediaPolicyPriv* priv = cookie;
    const char* response = NULL;
    int32_t result = -ECANCELED;

    if (cb) {
        if (parcel)
            media_parcel_read_scanf(parcel, "%i%s", &result, &response);

        cb(cookie1, result, response);
    }

    media_uv_disconnect(priv->proxy, media_uv_policy_release_cb);
}

/****************************************************************************
 * Public Basic Functions
 ****************************************************************************/

int media_uv_policy_set_string(void* loop, const char* name,
    const char* value, int apply, media_uv_callback cb, void* cookie)
{
    MediaPolicyPriv* priv;

    priv = media_uv_policy_alloc(name, "set_string", value, apply, 0,
        media_uv_policy_receive_cb, cb, cookie);
    if (!priv)
        return -ENOMEM;

    priv->proxy = media_uv_connect(loop, CONFIG_MEDIA_SERVER_CPUNAME,
        media_uv_policy_connect_cb, priv);
    if (!priv->proxy) {
        media_uv_policy_free(priv);
        return -ENOMEM;
    }

    return 0;
}

int media_uv_policy_get_string(void* loop, const char* name,
    media_uv_string_callback cb, void* cookie)
{
    MediaPolicyPriv* priv;

    priv = media_uv_policy_alloc(name, "get_string", NULL, 0, 128,
        media_uv_policy_receive_string_cb, cb, cookie);
    if (!priv)
        return -ENOMEM;

    priv->proxy = media_uv_connect(loop, CONFIG_MEDIA_SERVER_CPUNAME,
        media_uv_policy_connect_cb, priv);
    if (!priv->proxy) {
        media_uv_policy_free(priv);
        return -ENOMEM;
    }

    return 0;
}

int media_uv_policy_set_int(void* loop, const char* name,
    int value, int apply, media_uv_callback cb, void* cookie)
{
    MediaPolicyPriv* priv;
    char tmp[32];

    snprintf(tmp, sizeof(tmp), "%d", value);
    priv = media_uv_policy_alloc(name, "set_int", tmp, apply, 0,
        media_uv_policy_receive_cb, cb, cookie);
    if (!priv)
        return -ENOMEM;

    priv->proxy = media_uv_connect(loop, CONFIG_MEDIA_SERVER_CPUNAME,
        media_uv_policy_connect_cb, priv);
    if (!priv->proxy) {
        media_uv_policy_free(priv);
        return -ENOMEM;
    }

    return 0;
}

int media_uv_policy_get_int(void* loop, const char* name,
    media_uv_int_callback cb, void* cookie)
{
    MediaPolicyPriv* priv;

    priv = media_uv_policy_alloc(name, "get_int", NULL, 0, 128,
        media_uv_policy_receive_int_cb, cb, cookie);
    if (!priv)
        return -ENOMEM;

    priv->proxy = media_uv_connect(loop, CONFIG_MEDIA_SERVER_CPUNAME,
        media_uv_policy_connect_cb, priv);
    if (!priv->proxy) {
        media_uv_policy_free(priv);
        return -ENOMEM;
    }

    return 0;
}

int media_uv_policy_increase(void* loop, const char* name, int apply,
    media_uv_callback cb, void* cookie)
{
    MediaPolicyPriv* priv;

    priv = media_uv_policy_alloc(name, "increase", NULL, apply, 0,
        media_uv_policy_receive_cb, cb, cookie);
    if (!priv)
        return -ENOMEM;

    priv->proxy = media_uv_connect(loop, CONFIG_MEDIA_SERVER_CPUNAME,
        media_uv_policy_connect_cb, priv);
    if (!priv->proxy) {
        media_uv_policy_free(priv);
        return -ENOMEM;
    }

    return 0;
}

int media_uv_policy_decrease(void* loop, const char* name, int apply,
    media_uv_callback cb, void* cookie)
{
    MediaPolicyPriv* priv;

    priv = media_uv_policy_alloc(name, "decrease", NULL, apply, 0,
        media_uv_policy_receive_cb, cb, cookie);
    if (!priv)
        return -ENOMEM;

    priv->proxy = media_uv_connect(loop, CONFIG_MEDIA_SERVER_CPUNAME,
        media_uv_policy_connect_cb, priv);
    if (!priv->proxy) {
        media_uv_policy_free(priv);
        return -ENOMEM;
    }

    return 0;
}

int media_uv_policy_include(void* loop, const char* name,
    const char* value, int apply, media_uv_callback cb, void* cookie)
{
    MediaPolicyPriv* priv;

    priv = media_uv_policy_alloc(name, "include", value, apply, 0,
        media_uv_policy_receive_cb, cb, cookie);
    if (!priv)
        return -ENOMEM;

    priv->proxy = media_uv_connect(loop, CONFIG_MEDIA_SERVER_CPUNAME,
        media_uv_policy_connect_cb, priv);
    if (!priv->proxy) {
        media_uv_policy_free(priv);
        return -ENOMEM;
    }

    return 0;
}

int media_uv_policy_exclude(void* loop, const char* name,
    const char* value, int apply, media_uv_callback cb, void* cookie)
{
    MediaPolicyPriv* priv;

    priv = media_uv_policy_alloc(name, "exclude", value, apply, 0,
        media_uv_policy_receive_cb, cb, cookie);
    if (!priv)
        return -ENOMEM;

    priv->proxy = media_uv_connect(loop, CONFIG_MEDIA_SERVER_CPUNAME,
        media_uv_policy_connect_cb, priv);
    if (!priv->proxy) {
        media_uv_policy_free(priv);
        return -ENOMEM;
    }

    return 0;
}

int media_uv_policy_contain(void* loop, const char* name, const char* value,
    media_uv_int_callback cb, void* cookie)
{
    MediaPolicyPriv* priv;

    priv = media_uv_policy_alloc(name, "contain", value, 0, 128,
        media_uv_policy_receive_int_cb, cb, cookie);
    if (!priv)
        return -ENOMEM;

    priv->proxy = media_uv_connect(loop, CONFIG_MEDIA_SERVER_CPUNAME,
        media_uv_policy_connect_cb, priv);
    if (!priv->proxy) {
        media_uv_policy_free(priv);
        return -ENOMEM;
    }

    return 0;
}

/****************************************************************************
 * Public Wrapper Functions
 ****************************************************************************/

int media_uv_policy_set_stream_volume(void* loop, const char* stream,
    int volume, media_uv_callback cb, void* cookie)
{
    char name[64];
    int len;

    len = snprintf(name, sizeof(name), "%s%s", stream, MEDIA_POLICY_VOLUME);
    if (len >= sizeof(name))
        return -ENAMETOOLONG;

    return media_uv_policy_set_int(loop, name, volume,
        MEDIA_POLICY_APPLY, cb, cookie);
}

int media_uv_policy_get_stream_volume(void* loop, const char* stream,
    media_uv_int_callback cb, void* cookie)
{
    char name[64];
    int len;

    len = snprintf(name, sizeof(name), "%s%s", stream, MEDIA_POLICY_VOLUME);
    if (len >= sizeof(name))
        return -ENAMETOOLONG;

    return media_uv_policy_get_int(loop, name, cb, cookie);
}

int media_uv_policy_increase_stream_volume(void* loop, const char* stream,
    media_uv_callback cb, void* cookie)
{
    char name[64];
    int len;

    len = snprintf(name, sizeof(name), "%s%s", stream, MEDIA_POLICY_VOLUME);
    if (len >= sizeof(name))
        return -ENAMETOOLONG;

    return media_uv_policy_increase(loop, name, MEDIA_POLICY_APPLY, cb, cookie);
}

int media_uv_policy_decrease_stream_volume(void* loop, const char* stream,
    media_uv_callback cb, void* cookie)
{
    char name[64];
    int len;

    len = snprintf(name, sizeof(name), "%s%s", stream, MEDIA_POLICY_VOLUME);
    if (len >= sizeof(name))
        return -ENAMETOOLONG;

    return media_uv_policy_decrease(loop, name, MEDIA_POLICY_APPLY, cb, cookie);
}

int media_uv_policy_set_audio_mode(void* loop, const char* mode,
    media_uv_callback cb, void* cookie)
{
    return media_uv_policy_set_string(loop, MEDIA_POLICY_AUDIO_MODE,
        mode, MEDIA_POLICY_APPLY, cb, cookie);
}

int media_uv_policy_get_audio_mode(void* loop,
    media_uv_string_callback cb, void* cookie)
{
    return media_uv_policy_get_string(loop, MEDIA_POLICY_AUDIO_MODE, cb, cookie);
}

int media_uv_policy_set_devices_use(void* loop, const char* devices, bool use,
    media_uv_callback cb, void* cookie)
{
    if (use)
        return media_uv_policy_include(loop, MEDIA_POLICY_DEVICE_USE, devices,
            MEDIA_POLICY_APPLY, cb, cookie);
    else
        return media_uv_policy_exclude(loop, MEDIA_POLICY_DEVICE_USE, devices,
            MEDIA_POLICY_APPLY, cb, cookie);
}

int media_uv_policy_get_devices_use(void* loop, media_uv_string_callback cb, void* cookie)
{
    return media_uv_policy_get_string(loop, MEDIA_POLICY_DEVICE_USE, cb, cookie);
}

int media_uv_policy_is_devices_use(void* loop, const char* devices,
    media_uv_int_callback cb, void* cookie)
{
    return media_uv_policy_contain(loop, MEDIA_POLICY_DEVICE_USE, devices, cb, cookie);
}

int media_uv_policy_set_hfp_samplerate(void* loop, int rate,
    media_uv_callback cb, void* cookie)
{
    return media_uv_policy_set_int(loop, MEDIA_POLICY_HFP_SAMPLERATE, rate,
        MEDIA_POLICY_NOT_APPLY, cb, cookie);
}

int media_uv_policy_set_devices_available(void* loop, const char* devices, bool available,
    media_uv_callback cb, void* cookie)
{
    if (available)
        return media_uv_policy_include(loop, MEDIA_POLICY_DEVICE_AVAILABLE, devices,
            MEDIA_POLICY_APPLY, cb, cookie);
    else
        return media_uv_policy_exclude(loop, MEDIA_POLICY_DEVICE_AVAILABLE, devices,
            MEDIA_POLICY_APPLY, cb, cookie);
}

int media_uv_policy_get_devices_available(void* loop, media_uv_string_callback cb, void* cookie)
{
    return media_uv_policy_get_string(loop, MEDIA_POLICY_DEVICE_AVAILABLE, cb, cookie);
}

int media_uv_policy_is_devices_available(void* loop, const char* devices,
    media_uv_int_callback cb, void* cookie)
{
    return media_uv_policy_contain(loop, MEDIA_POLICY_DEVICE_AVAILABLE, devices, cb, cookie);
}

int media_uv_policy_set_mute_mode(void* loop, int mute, media_uv_callback cb, void* cookie)
{
    return media_uv_policy_set_int(loop, MEDIA_POLICY_MUTE_MODE, mute,
        MEDIA_POLICY_APPLY, cb, cookie);
}

int media_uv_policy_get_mute_mode(void* loop, media_uv_int_callback cb, void* cookie)
{
    return media_uv_policy_get_int(loop, MEDIA_POLICY_MUTE_MODE, cb, cookie);
}

int media_uv_policy_set_mic_mute(void* loop, int mute, media_uv_callback cb, void* cookie)
{
    return media_uv_policy_set_string(loop, MEDIA_POLICY_MIC_MODE, mute ? "off" : "on",
        MEDIA_POLICY_APPLY, cb, cookie);
}
