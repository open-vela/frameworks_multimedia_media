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

#include "media_proxy.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MEDIA_POLICY_APPLY 1
#define MEDIA_POLICY_NOT_APPLY 0

/* Key criterion names. */
#define MEDIA_POLICY_AUDIO_MODE "AudioMode"
#define MEDIA_POLICY_DEVICE_USE "UsingDevices"
#define MEDIA_POLICY_DEVICE_AVAILABLE "AvailableDevices"
#define MEDIA_POLICY_HFP_SAMPLERATE "HFPSampleRate"
#define MEDIA_POLICY_MUTE_MODE "MuteMode"
#define MEDIA_POLICY_VOLUME "Volume"

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

int media_policy_set_hfp_samplerate(const char* rate)
{
    return media_policy_set_string(MEDIA_POLICY_HFP_SAMPLERATE, rate,
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

int media_policy_set_int(const char* name, int value, int apply)
{
    char tmp[32];

    snprintf(tmp, sizeof(tmp), "%d", value);
    return media_proxy(MEDIA_ID_POLICY, NULL, name, "set_int", tmp, apply, NULL, 0, false);
}

int media_policy_get_int(const char* name, int* value)
{
    char tmp[32];
    int ret;

    if (!value)
        return -EINVAL;

    ret = media_proxy(MEDIA_ID_POLICY, NULL, name, "get_int", NULL, 0, tmp, sizeof(tmp), false);
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

    ret = media_proxy(MEDIA_ID_POLICY, NULL, name, "contain", values, 0, tmp, sizeof(tmp), false);
    if (ret >= 0) {
        *result = atoi(tmp);
        ret = 0;
    }

    return ret;
}

int media_policy_set_string(const char* name, const char* value, int apply)
{
    return media_proxy(MEDIA_ID_POLICY, NULL, name, "set_string", value, apply, NULL, 0, false);
}

int media_policy_get_string(const char* name, char* value, int len)
{
    return media_proxy(MEDIA_ID_POLICY, NULL, name, "get_string", NULL, 0, value, len, false);
}

int media_policy_include(const char* name, const char* values, int apply)
{
    return media_proxy(MEDIA_ID_POLICY, NULL, name, "include", values, apply, NULL, 0, false);
}

int media_policy_exclude(const char* name, const char* values, int apply)
{
    return media_proxy(MEDIA_ID_POLICY, NULL, name, "exclude", values, apply, NULL, 0, false);
}

int media_policy_increase(const char* name, int apply)
{
    return media_proxy(MEDIA_ID_POLICY, NULL, name, "increase", NULL, apply, NULL, 0, false);
}

int media_policy_decrease(const char* name, int apply)
{
    return media_proxy(MEDIA_ID_POLICY, NULL, name, "decrease", NULL, apply, NULL, 0, false);
}

void media_policy_dump(const char* options)
{
    media_proxy(MEDIA_ID_POLICY, NULL, NULL, "dump", options, 0, NULL, 0, false);
}
