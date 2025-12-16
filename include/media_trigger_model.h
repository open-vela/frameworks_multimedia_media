/****************************************************************************
 * frameworks/media/include/media_trigger_model.h
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

#ifndef MEDIA_INCLUDE_MEDIATRIGGER_MODEL_H
#define MEDIA_INCLUDE_MEDIATRIGGER_MODEL_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <media_defs.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C" {
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef void (*hotword_detection_callback_t)(void* user_data, int event,
    int result, const char* extra);

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/**
 * @brief Get vendor properties info.
 *
 * @param[in] properties     saved properties.
 * @param[in] size           size of properties.
 * @return void.
 */
void media_trigger_model_get_properties(void* properties, size_t* size);

/**
 * @brief Load sound model.
 *
 * @param[in] model         model data.
 * @param[in] size          model size.
 * @return void* context on success; NULL on failure.
 */
void* media_trigger_model_load(const void* model, size_t size, hotword_detection_callback_t callback, void* priv);

/**
 * @brief Get sound model capture options.
 *
 * @param[in] context        model context.
 * @param[in] options        string,exp:"format=s16le:sample_rate=16000:ch_layout=mono";
 * @param[in] size           size of options
 * @return void.
 */
void media_trigger_model_get_options(void* context, char* options, size_t size);

/**
 * @brief Get record buffer size .
 *
 * @param[in] context        model context.
 * @param[in] size           record buffer size
 * @return void.
 */
void media_trigger_model_get_buffer_size(void* context, size_t* size);

/**
 * @brief hotword detect
 *
 * @param[in] context          model context.
 * @param[in] buffer           pcm buffer to detect.
 * @param[in] size             buffer size.
 * @return bool  true on detected; false no detected.
 */
bool media_trigger_model_detect_hotword(void* context, const char* buffer, size_t size);

/**
 * @brief Unload sound model.
 *
 * @param[in] context    model context which to unload.
 * @return void
 */
void media_trigger_model_unload(void* context);

/*
 * @brief Get model poll fd.
 *
 * @param[in] context    model context.
 * @return poll fd on success; -1 on failure.
 */
int media_trigger_model_get_poll_fd(void* context);

/*
 * @brief Model poll available.
 *
 * @param[in] context    model context.
 * @return 0 on success; -1 on failure.
 */
int media_trigger_model_poll_available(void* context);

#undef EXTERN
#ifdef __cplusplus
}
#endif
#endif /* MEDIA_INCLUDE_MEDIATRIGGER_MODEL_H */
