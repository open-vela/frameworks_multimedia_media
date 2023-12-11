/****************************************************************************
 * frameworks/media/utils/media_common.h
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

#ifndef FRAMEWORKS_MEDIA_UTILS_MEDIA_COMMON_H
#define FRAMEWORKS_MEDIA_UTILS_MEDIA_COMMON_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <syslog.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* RPC definitions. */

#define MEDIA_SOCKADDR_NAME "md:%s"
#ifndef CONFIG_RPTUN_LOCAL_CPUNAME
#define CONFIG_RPTUN_LOCAL_CPUNAME CONFIG_MEDIA_SERVER_CPUNAME
#endif
#define MEDIA_GRAPH_SOCKADDR_NAME "med%p"

/* Module ID. */

#define MEDIA_ID_GRAPH 1
#define MEDIA_ID_POLICY 2
#define MEDIA_ID_PLAYER 3
#define MEDIA_ID_RECORDER 4
#define MEDIA_ID_SESSION 5
#define MEDIA_ID_FOCUS 6

/* Policy key criterion names. */
#define MEDIA_POLICY_APPLY 1
#define MEDIA_POLICY_NOT_APPLY 0
#define MEDIA_POLICY_AUDIO_MODE "AudioMode"
#define MEDIA_POLICY_DEVICE_USE "UsingDevices"
#define MEDIA_POLICY_DEVICE_AVAILABLE "AvailableDevices"
#define MEDIA_POLICY_HFP_SAMPLERATE "HFPSampleRate"
#define MEDIA_POLICY_MUTE_MODE "MuteMode"
#define MEDIA_POLICY_VOLUME "Volume"
#define MEDIA_POLICY_MIC_MODE "MicMode"

/* Debug log definition. */
#define MEDIA_LOG(level, fmt, args...) \
    syslog(level, "[media][%s:%d] " fmt, __func__, __LINE__, ##args)

#if defined(CONFIG_MEDIA_LOG_DEBUG)
#define MEDIA_DEBUG(fmt, args...) MEDIA_LOG(LOG_DEBUG, fmt, ##args)
#define MEDIA_INFO(fmt, args...) MEDIA_LOG(LOG_INFO, fmt, ##args)
#define MEDIA_WARN(fmt, args...) MEDIA_LOG(LOG_WARNING, fmt, ##args)
#define MEDIA_ERR(fmt, args...) MEDIA_LOG(LOG_ERR, fmt, ##args)
#elif defined(CONFIG_MEDIA_LOG_INFO)
#define MEDIA_DEBUG(fmt, args...)
#define MEDIA_INFO(fmt, args...) MEDIA_LOG(LOG_INFO, fmt, ##args)
#define MEDIA_WARN(fmt, args...) MEDIA_LOG(LOG_WARNING, fmt, ##args)
#define MEDIA_ERR(fmt, args...) MEDIA_LOG(LOG_ERR, fmt, ##args)
#elif defined(CONFIG_MEDIA_LOG_WARN)
#define MEDIA_DEBUG(fmt, args...)
#define MEDIA_INFO(fmt, args...)
#define MEDIA_WARN(fmt, args...) MEDIA_LOG(LOG_WARNING, fmt, ##args)
#define MEDIA_ERR(fmt, args...) MEDIA_LOG(LOG_ERR, fmt, ##args)
#elif defined(CONFIG_MEDIA_LOG_ERR)
#define MEDIA_DEBUG(fmt, args...)
#define MEDIA_INFO(fmt, args...)
#define MEDIA_WARN(fmt, args...)
#define MEDIA_ERR(fmt, args...) MEDIA_LOG(LOG_ERR, fmt, ##args)
#else
#define MEDIA_DEBUG(fmt, args...)
#define MEDIA_INFO(fmt, args...)
#define MEDIA_WARN(fmt, args...)
#define MEDIA_ERR(fmt, args...)
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/**
 * @brief Get id name.
 *
 * For example, media_id_get_name(MEDIA_ID_GRAPH) returns "graph".
 *
 * @param id     MEDIA_ID_* .
 * @return const char* Always a printable string.
 */
const char* media_id_get_name(int id);

#endif /* FRAMEWORKS_MEDIA_UTILS_MEDIA_COMMON_H */
