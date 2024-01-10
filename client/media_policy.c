/****************************************************************************
 * frameworks/media/client/media_policy.c
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
#include <string.h>

#include "media_common.h"
#include "media_proxy.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct MediaPolicyPriv {
    MEDIA_COMMON_FIELDS
    void* cookie;
    media_policy_change_callback on_change;
} MediaPolicyPriv;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void media_policy_change_cb(void* cookie, media_parcel* msg)
{
    MediaPolicyPriv* priv = cookie;
    const char* literal;
    int32_t number;

    media_parcel_read_scanf(msg, "%i%i%s", NULL, &number, &literal);
    priv->on_change(priv->cookie, number, literal);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int media_policy_set_audio_mode(const char* mode)
{
    return media_policy_set_string(MEDIA_POLICY_AUDIO_MODE, mode,
        MEDIA_POLICY_APPLY);
}

int media_policy_get_audio_mode(char* mode, int len)
{
    return media_policy_get_string(MEDIA_POLICY_AUDIO_MODE, mode, len);
}

int media_policy_set_devices_use(const char* devices)
{
    return media_policy_include(MEDIA_POLICY_DEVICE_USE, devices,
        MEDIA_POLICY_APPLY);
}

int media_policy_set_devices_unuse(const char* devices)
{
    return media_policy_exclude(MEDIA_POLICY_DEVICE_USE, devices,
        MEDIA_POLICY_APPLY);
}

int media_policy_is_devices_use(const char* devices, int* use)
{
    return media_policy_contain(MEDIA_POLICY_DEVICE_USE, devices, use);
}

int media_policy_get_devices_use(char* devices, int len)
{
    return media_policy_get_string(MEDIA_POLICY_DEVICE_USE, devices, len);
}

int media_policy_set_hfp_samplerate(int rate)
{
    return media_policy_set_int(MEDIA_POLICY_HFP_SAMPLERATE, rate,
        MEDIA_POLICY_NOT_APPLY);
}

int media_policy_set_devices_available(const char* devices)
{
    return media_policy_include(MEDIA_POLICY_DEVICE_AVAILABLE, devices,
        MEDIA_POLICY_APPLY);
}

int media_policy_set_devices_unavailable(const char* devices)
{
    return media_policy_exclude(MEDIA_POLICY_DEVICE_AVAILABLE, devices,
        MEDIA_POLICY_APPLY);
}

int media_policy_is_devices_available(const char* devices, int* available)
{
    return media_policy_contain(MEDIA_POLICY_DEVICE_AVAILABLE, devices, available);
}

int media_policy_get_devices_available(char* devices, int len)
{
    return media_policy_get_string(MEDIA_POLICY_DEVICE_AVAILABLE, devices, len);
}

int media_policy_set_mute_mode(int mute)
{
    return media_policy_set_int(MEDIA_POLICY_MUTE_MODE, mute,
        MEDIA_POLICY_APPLY);
}

int media_policy_get_mute_mode(int* mute)
{
    return media_policy_get_int(MEDIA_POLICY_MUTE_MODE, mute);
}

int media_policy_set_stream_volume(const char* stream, int volume)
{
    char name[64];
    int len;

    len = snprintf(name, sizeof(name), "%s%s", stream, MEDIA_POLICY_VOLUME);
    if (len >= sizeof(name))
        return -ENAMETOOLONG;

    return media_policy_set_int(name, volume, MEDIA_POLICY_APPLY);
}

int media_policy_get_stream_volume(const char* stream, int* volume)
{
    char name[64];
    int len;

    len = snprintf(name, sizeof(name), "%s%s", stream, MEDIA_POLICY_VOLUME);
    if (len >= sizeof(name))
        return -ENAMETOOLONG;

    return media_policy_get_int(name, volume);
}

int media_policy_increase_stream_volume(const char* stream)
{
    char name[64];
    int len;

    len = snprintf(name, sizeof(name), "%s%s", stream, MEDIA_POLICY_VOLUME);
    if (len >= sizeof(name))
        return -ENAMETOOLONG;

    return media_policy_increase(name, MEDIA_POLICY_APPLY);
}

int media_policy_decrease_stream_volume(const char* stream)
{
    char name[64];
    int len;

    len = snprintf(name, sizeof(name), "%s%s", stream, MEDIA_POLICY_VOLUME);
    if (len >= sizeof(name))
        return -ENAMETOOLONG;

    return media_policy_decrease(name, MEDIA_POLICY_APPLY);
}

void* media_policy_subscribe(const char* name,
    media_policy_change_callback on_change, void* cookie)
{
    MediaPolicyPriv* priv;
    int ret;

    priv = zalloc(sizeof(MediaPolicyPriv));
    if (!priv)
        return NULL;

    priv->cookie = cookie;
    priv->on_change = on_change;

    ret = media_proxy(MEDIA_ID_POLICY, priv, NULL, "ping", NULL, 0, NULL, 0);
    if (ret < 0) {
        media_default_release_cb(priv);
        return NULL;
    }

    media_proxy_set_release_cb(priv->proxy, media_default_release_cb, priv);
    ret = media_proxy_set_event_cb(priv->proxy, priv->cpu, media_policy_change_cb, priv);
    if (ret < 0)
        goto err;

    if (media_proxy_once(priv, name, "subscribe", NULL, 0, NULL, 0) < 0)
        goto err;

    return priv;

err:
    media_policy_unsubscribe(priv->proxy);
    return NULL;
}

int media_policy_unsubscribe(void* handle)
{
    MediaPolicyPriv* priv = handle;
    int ret;

    if (!handle)
        return -EINVAL;

    ret = media_proxy_once(priv, NULL, "unsubscribe", NULL, 0, NULL, 0);
    if (ret < 0)
        return ret;

    return media_proxy_disconnect(priv->proxy);
}

int media_policy_set_int(const char* name, int value, int apply)
{
    char tmp[32];

    snprintf(tmp, sizeof(tmp), "%d", value);
    return media_proxy(MEDIA_ID_POLICY, NULL, name, "set_int", tmp, apply, NULL, 0);
}

int media_policy_get_int(const char* name, int* value)
{
    char tmp[32];
    int ret;

    if (!value)
        return -EINVAL;

    ret = media_proxy(MEDIA_ID_POLICY, NULL, name, "get_int", NULL, 0, tmp, sizeof(tmp));
    if (ret >= 0) {
        *value = atoi(tmp);
        ret = 0;
    }

    return ret;
}

int media_policy_contain(const char* name, const char* values, int* result)
{
    char tmp[32];
    int ret;

    if (!result)
        return -EINVAL;

    ret = media_proxy(MEDIA_ID_POLICY, NULL, name, "contain", values, 0, tmp, sizeof(tmp));
    if (ret >= 0) {
        *result = atoi(tmp);
        ret = 0;
    }

    return ret;
}

int media_policy_set_string(const char* name, const char* value, int apply)
{
    return media_proxy(MEDIA_ID_POLICY, NULL, name, "set_string", value, apply, NULL, 0);
}

int media_policy_get_string(const char* name, char* value, int len)
{
    return media_proxy(MEDIA_ID_POLICY, NULL, name, "get_string", NULL, 0, value, len);
}

int media_policy_include(const char* name, const char* values, int apply)
{
    return media_proxy(MEDIA_ID_POLICY, NULL, name, "include", values, apply, NULL, 0);
}

int media_policy_exclude(const char* name, const char* values, int apply)
{
    return media_proxy(MEDIA_ID_POLICY, NULL, name, "exclude", values, apply, NULL, 0);
}

int media_policy_increase(const char* name, int apply)
{
    return media_proxy(MEDIA_ID_POLICY, NULL, name, "increase", NULL, apply, NULL, 0);
}

int media_policy_decrease(const char* name, int apply)
{
    return media_proxy(MEDIA_ID_POLICY, NULL, name, "decrease", NULL, apply, NULL, 0);
}

void media_policy_dump(const char* options)
{
    media_proxy(MEDIA_ID_POLICY, NULL, NULL, "dump", options, 0, NULL, 0);
}

int media_policy_set_mic_mute(int mute)
{
    return media_policy_set_string(MEDIA_POLICY_MIC_MODE, mute ? "off" : "on",
        MEDIA_POLICY_APPLY);
}
