/****************************************************************************
 * frameworks/media/include/media_dtmf.h
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

#ifndef FRAMEWORKS_MEDIA_INCLUDE_MEDIA_UTILS_H
#define FRAMEWORKS_MEDIA_INCLUDE_MEDIA_UTILS_H

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* DTMF default tone format */

#define MEDIA_TONE_DTMF_FORMAT "format=s16le:sample_rate=8000:ch_layout=mono"

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
 * Send command to media
 * @param[in] target    target name
 * @param[in] cmd       command
 * @param[in] arg       argument
 * @param[out] res      response
 * @param[in] res_len   response msg len
 * @return Zero on success; a negated errno value on failure
 */
int media_process_command(const char* target, const char* cmd,
    const char* arg, char* res, int res_len);

/**
 * Dump media graph
 * @param[in] options   dump options
 */
void media_graph_dump(const char* options);

/**
 * Dump media policy
 * @param[in] options   dump options
 */
void media_policy_dump(const char* options);

/**
 * Get DTMF tone buffer size.
 * @param[in] numbers   Dialbuttons numbers.
 * @return Buffer size of DTMF tone on success; a negated errno value on
 *         failure.
 */
int media_dtmf_get_buffer_size(const char* numbers);

/**
 * Generate one or continuous multiple DTMF signal and save the result in
 * the input buffer.
 * @param[in] numbers   Dialbuttons numbers.
 * @param[out] buffer   Buffer address.
 * @return Zero on success; a negated errno value on failure.
 * @note Format should fixed as MEDIA_TONE_DTMF_FORMAT when to play dtmf tone.
 */
int media_dtmf_generate(const char* numbers, void* buffer);

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* FRAMEWORKS_MEDIA_INCLUDE_MEDIA_UTILS_H */