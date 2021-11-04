/****************************************************************************
 * frameworks/media/include/media_api.h
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

#ifndef FRAMEWORKS_MEDIA_INCLUDE_MEDIA_API_H
#define FRAMEWORKS_MEDIA_INCLUDE_MEDIA_API_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <media_event.h>
#include <media_focus.h>
#include <media_player.h>
#include <media_policy.h>
#include <media_recorder.h>

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/**
 * Send command to media
 * @param[in] target    target name
 * @param[in] cmd       command
 * @param[in] arg       argument
 * @param[out] res      response
 * @param[in] res_len   response msg len
 * @return Zero on success; a negated errno value on failure
 */
int media_process_command(const char *target, const char *cmd,
                          const char *arg, char *res, int res_len);

/**
 * Dump media
 * @param[in] options dump options
 */
void media_dump(const char *options);

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* FRAMEWORKS_MEDIA_INCLUDE_MEDIA_API_H */
