/****************************************************************************
 * frameworks/media/include/media_trigger.h
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

#ifndef FRAMEWORKS_MEDIA_INCLUDE_MEDIA_TRIGGER_H
#define FRAMEWORKS_MEDIA_INCLUDE_MEDIA_TRIGGER_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <media_defs.h>
#include <stddef.h>

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C" {
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/**
 * @brief Open media trigger handle for contorl.
 *
 * @param[in] params   media trigger handle parameters, example: "Dual Mic" or "default"
 * @return void*    media trigger handle, NULL on failure.
 *
 * @code To simply start media trigger:
 *  // 1. create a instance.
 *  handle = media_trigger_open("default");
 *
 *  // 2. set event callback for media trigger events.
 *  ret = media_trigger_set_event_callback(handle, cookie, callback);
 *
 *  // 3. load a sound model for media trigger.
 *  ret = media_trigger_load_sound_model(handle, model, model_size);
 *
 *  // 4. start recognition.
 *  ret = media_trigger_start_recognition(handle);
 *
 *  // 5. stop recognition.
 *  ret = media_trigger_stop_recognition(handle);
 *
 *  // 6. unload sound model.
 *  ret = media_trigger_unload_sound_model(handle);
 *
 *  // 7. close the handle.
 *  ret = media_trigger_close(handle);
 * @endcode
 */
void* media_trigger_open(const char* params);

/**
 * @brief Set a event callback to listen to event media trigger model events.
 *
 * @param[in] handle        media trigger handle.
 * @param[in] event_cookie  Callback argument.
 * @param[out] on_event     Callback to receive events about stream status change.
 * @return int  Zero on success; a negated errno value on failure.
 *
 */
int media_trigger_set_event_callback(void* handle, void* event_cookie,
    media_event_callback on_event);

/**
 * @brief Load a sound model for media trigger.
 *
 * @param[in] handle        media trigger handle.
 * @param[in] model         Sound model data.
 * @param[in] model_size    Sound model size.
 * @return int  Zero on success; a negated errno value on failure.
 *
 */
int media_trigger_load_sound_model(void* handle, void* model, size_t model_size);

/**
 * @brief Start media trigger recognition.
 *
 * @param[in] handle    media trigger handle.
 * @return int  Zero on success; a negated errno value on failure.
 */
int media_trigger_start_recognition(void* handle);

/**
 * @brief Stop media trigger recognition.
 *
 * @param[in] handle    media trigger handle.
 * @return int  Zero on success; a negated errno value on failure.
 */
int media_trigger_stop_recognition(void* handle);

/**
 * @brief Unload sound model
 *
 * @param[in] handle    media trigger handle
 * @return int  Zero on success; a negated errno value on failure.
 */
int media_trigger_unload_sound_model(void* handle);

/**
 * @brief Close sound model handle
 *
 * @param[in] handle    media trigger handle which is to be closed.
 * @return int  Zero on success; a negated errno value on failure.
 */
int media_trigger_close(void* handle);

/**
 * @brief Get properties of the media trigger dsp info.
 *
 * @param[in] handle        Current media trigger handle.
 * @param[in] properties    Buffer of value.
 * @param[in] len           Buffer length of value.
 * @return int Zero on success; a negated errno value on failure.
 */
int media_trigger_get_property(char* properties, int len);

#undef EXTERN
#ifdef __cplusplus
}
#endif
#endif /* FRAMEWORKS_MEDIA_INCLUDE_MEDIA_TRIGGER_H */
