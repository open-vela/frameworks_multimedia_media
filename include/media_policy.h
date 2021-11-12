/****************************************************************************
 * frameworks/media/include/med_policy.h
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

#ifndef FRAMEWORKS_MEDIA_INCLUDE_MEDIA_POLICY_H
#define FRAMEWORKS_MEDIA_INCLUDE_MEDIA_POLICY_H

#ifdef __cplusplus
extern "C" {
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: media_policy_set_int
 *
 * Description:
 *   Set criterion with interger value.
 *   You should use wrapper functions rather than using this funtion directly.
 *
 * Input Parameters:
 *   name   - criterion name
 *   value  - new value
 *   apply  - whether apply configurations
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_policy_set_int(const char *name, int value, int apply);

/****************************************************************************
 * Name: media_policy_get_int
 *
 * Description:
 *   Get criterion in interger value.
 *   You should use wrapper functions rather than using this funtion directly.
 *
 * Input Parameters:
 *   name   - criterion name
 *   value  - address to return current value
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_policy_get_int(const char *name, int *value);

/****************************************************************************
 * Name: media_policy_set_string
 *
 * Description:
 *   Get criterion with string value.
 *   You should use wrapper functions rather than using this funtion directly.
 *
 * Input Parameters:
 *   name   - criterion name
 *   value  - new value
 *   apply  - whether apply configurations
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_policy_set_string(const char *name, const char *value, int apply);

/****************************************************************************
 * Name: media_policy_get_string
 *
 * Description:
 *   Get criterion in string value.
 *   You should use wrapper functions rather than using this funtion directly.
 *
 * Input Parameters:
 *   name   - criterion name
 *   value  - address to return current value
 *   len    - size of value
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_policy_get_string(const char *name, char *value, int len);

/****************************************************************************
 * Name: media_policy_include
 *
 * Description:
 *   Include(insert) inclusive criterion values.
 *   You should use wrapper functions rather than using this funtion directly.
 *
 * Input Parameters:
 *   name    - inclusive criterion name
 *   values  - new values
 *   apply   - whether apply configurations
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_policy_include(const char *name, const char *values, int apply);

/****************************************************************************
 * Name: media_policy_exclude
 *
 * Description:
 *   Exclude(remove) inclusive criterion values.
 *   You should use wrapper functions rather than using this funtion directly.
 *
 * Input Parameters:
 *   name    - inclusive criterion name
 *   values  - new values
 *   apply   - whether apply configurations
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_policy_exclude(const char *name, const char *values, int apply);

/****************************************************************************
 * Name: media_policy_increase
 *
 * Description:
 *   Increase criterion value.
 *   You should use wrapper functions rather than using this funtion directly.
 *
 * Input Parameters:
 *   name   - criterion name
 *   apply  - whether apply configurations
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_policy_increase(const char *name, int apply);

/****************************************************************************
 * Name: media_policy_decrease
 *
 * Description:
 *   Decrease criterion value.
 *   You should use wrapper functions rather than using this funtion directly.
 *
 * Input Parameters:
 *   name   - criterion name
 *   apply  - whether apply configurations
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_policy_decrease(const char *name, int apply);

/****************************************************************************
 * Wrapper Functions
 ****************************************************************************/

#define MEDIA_POLICY_AUDIO_MODE        "AudioMode"
#define MEDIA_POLICY_AVAILABLE_DEVICES "AvailableDevices"

/****************************************************************************
 * Name: media_policy_set_audio_mode
 *
 * Description:
 *   Set audio mode, let media_policy_set_string do real work.
 *
 * Input Parameters:
 *   mode       - new audio mode.
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

static inline int media_policy_set_audio_mode(const char *mode)
{
    return media_policy_set_string(MEDIA_POLICY_AUDIO_MODE, mode, 1);
}

/****************************************************************************
 * Name: media_policy_get_audio_mode
 *
 * Description:
 *   Get audio mode, let media_policy_get_string do real work.
 *
 * Input Parameters:
 *   mode       - return current audio mode.
 *   len        - size of mode.
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

static inline int media_policy_get_audio_mode(char *mode, int len)
{
    return media_policy_get_string(MEDIA_POLICY_AUDIO_MODE, mode, len);
}

/****************************************************************************
 * Name: media_policy_enable_devices
 *
 * Description:
 *   Enable some devices, let media_policy_include do real work.
 *
 * Input Parameters:
 *   devices    - new available devices.
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

static inline int media_policy_enable_devices(const char *devices)
{
    return media_policy_include(MEDIA_POLICY_AVAILABLE_DEVICES, devices, 1);
}

/****************************************************************************
 * Name: media_policy_disable_devices
 *
 * Description:
 *   Disable some devices, let media_policy_exclude do real work.
 *
 * Input Parameters:
 *   devices    - new available devices.
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

static inline int media_policy_disable_devices(const char *devices)
{
    return media_policy_exclude(MEDIA_POLICY_AVAILABLE_DEVICES, devices, 1);
}

/****************************************************************************
 * Name: media_policy_get_available_devices
 *
 * Description:
 *   Get available devices, let media_policy_get_string do real work.
 *
 * Input Parameters:
 *   devices    - return current available devices.
 *   len        - size of devices.
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

static inline int media_policy_get_available_devices(char *devices, int len)
{
    return media_policy_get_string(MEDIA_POLICY_AVAILABLE_DEVICES, devices, len);
}

/****************************************************************************
 * Name: media_policy_set_stream_volume
 *
 * Description:
 *   Set stream type's volume index, let media_policy_set_int do real work.
 *
 * Input Parameters:
 *   stream     - stream type volume's criterion name.
 *   volume     - new volume index.
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

static inline int media_policy_set_stream_volume(const char *stream, int volume)
{
    return media_policy_set_int(stream, volume, 1);
}

/****************************************************************************
 * Name: media_policy_get_stream_volume
 *
 * Description:
 *   Set stream type's volume index, let media_policy_get_int do real work.
 *
 * Input Parameters:
 *   stream     - stream type volume's criterion name.
 *   volume     - return current volume index.
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

static inline int media_policy_get_stream_volume(const char *stream, int *volume)
{
    return media_policy_get_int(stream, volume);
}

/****************************************************************************
 * Name: media_policy_increase_stream_volume
 *
 * Description:
 *   Increase stream type's volume index by 1, let media_policy_increase
 *     do real work.
 *
 * Input Parameters:
 *   stream     - stream type volume's criterion name.
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

static inline int media_policy_increase_stream_volume(const char *stream)
{
    return media_policy_increase(stream, 1);
}

/****************************************************************************
 * Name: media_policy_decrease_stream_volume
 *
 * Description:
 *   Increase stream type's volume index by 1, let media_policy_decrease
 *     do real work.
 *
 * Input Parameters:
 *   stream     - stream type volume's criterion name.
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

static inline int media_policy_decrease_stream_volume(const char *stream)
{
    return media_policy_decrease(stream, 1);
}

#ifdef __cplusplus
}
#endif

#endif /* FRAMEWORKS_MEDIA_INCLUDE_MEDIA_POLICY_H */
