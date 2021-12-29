/****************************************************************************
 * frameworks/media/wrapper/media_wrapper.c
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
#include <stdio.h>
#include "media_policy.h"
#include "media_wrapper.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MEDIA_POLICY_APPLY              1
#define MEDIA_POLICY_NOT_APPLY          0

// criterion names
#define MEDIA_POLICY_AUDIO_MODE         "AudioMode"
#define MEDIA_POLICY_DEVICES            "AvailableDevices"
#define MEDIA_POLICY_HFP_SAMPLERATE     "HFPSampleRate"
#define MEDIA_POLICY_MUTE_MODE          "MuteMode"
#define MEDIA_POLICY_VOLUME             "Volume"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int media_policy_set_audio_mode(const char *mode)
{
    return media_policy_set_string(MEDIA_POLICY_AUDIO_MODE, mode,
                                   MEDIA_POLICY_APPLY);
}

int media_policy_get_audio_mode(char *mode, int len)
{
    return media_policy_get_string(MEDIA_POLICY_AUDIO_MODE, mode, len);
}

int media_policy_set_hfp_samplerate(const char *rate)
{
    return media_policy_set_string(MEDIA_POLICY_HFP_SAMPLERATE, rate,
                                   MEDIA_POLICY_NOT_APPLY);
}

int media_policy_set_mute_mode(int mute)
{
    return media_policy_set_int(MEDIA_POLICY_MUTE_MODE, mute,
                                MEDIA_POLICY_APPLY);
}

int media_policy_get_mute_mode(int *mute)
{
    return media_policy_get_int(MEDIA_POLICY_MUTE_MODE, mute);
}

int media_policy_set_devices_available(const char *devices)
{
    return media_policy_include(MEDIA_POLICY_DEVICES, devices,
                                MEDIA_POLICY_APPLY);
}

int media_policy_set_devices_unavailable(const char *devices)
{
    return media_policy_exclude(MEDIA_POLICY_DEVICES, devices,
                                MEDIA_POLICY_APPLY);
}

int media_policy_get_devices_available(char *devices, int len)
{
    return media_policy_get_string(MEDIA_POLICY_DEVICES, devices, len);
}

int media_policy_set_stream_volume(const char *stream, int volume)
{
    char name[64];
    int len;

    len = snprintf(name, sizeof(name), "%s%s", stream, MEDIA_POLICY_VOLUME);
    if (len >= sizeof(name))
        return  -ENAMETOOLONG;

    return media_policy_set_int(name, volume, MEDIA_POLICY_APPLY);
}

int media_policy_get_stream_volume(const char *stream, int *volume)
{
    char name[64];
    int len;

    len = snprintf(name, sizeof(name), "%s%s", stream, MEDIA_POLICY_VOLUME);
    if (len >= sizeof(name))
        return  -ENAMETOOLONG;

    return media_policy_get_int(name, volume);
}

int media_policy_increase_stream_volume(const char *stream)
{
    char name[64];
    int len;

    len = snprintf(name, sizeof(name), "%s%s", stream, MEDIA_POLICY_VOLUME);
    if (len >= sizeof(name))
        return  -ENAMETOOLONG;

    return media_policy_increase(name, MEDIA_POLICY_APPLY);
}

int media_policy_decrease_stream_volume(const char *stream)
{
    char name[64];
    int len;

    len = snprintf(name, sizeof(name), "%s%s", stream, MEDIA_POLICY_VOLUME);
    if (len >= sizeof(name))
        return  -ENAMETOOLONG;

    return media_policy_decrease(name, MEDIA_POLICY_APPLY);
}
