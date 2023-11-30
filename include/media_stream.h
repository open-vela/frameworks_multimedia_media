/****************************************************************************
 * frameworks/media/include/media_stream.h
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

#ifndef FRAMEWORKS_MEDIA_INCLUDE_MEDIA_STREAM_H
#define FRAMEWORKS_MEDIA_INCLUDE_MEDIA_STREAM_H

#ifdef __cplusplus
extern "C" {
#endif

/* Scenario types, for focus. */

#define MEDIA_SCENARIO_INCALL "SCO"
#define MEDIA_SCENARIO_RING "Ring"
#define MEDIA_SCENARIO_ALARM "Alarm"
#define MEDIA_SCENARIO_DRAIN "Enforced"
#define MEDIA_SCENARIO_NOTIFICATION "Notify" /* message notification */
#define MEDIA_SCENARIO_RECORD "Record"
#define MEDIA_SCENARIO_TTS "TTS" /* text-to-speech */
#define MEDIA_SCENARIO_ACCESSIBILITY "Health" /* health notification */
#define MEDIA_SCENARIO_SPORT "Sport"
#define MEDIA_SCENARIO_INFO "Info"
#define MEDIA_SCENARIO_MUSIC "Music"

/* Stream types, for player and policy. */

#define MEDIA_STREAM_RING "Ring"
#define MEDIA_STREAM_ALARM "Alarm"
#define MEDIA_STREAM_SYSTEM_ENFORCED "Enforced"
#define MEDIA_STREAM_NOTIFICATION "Notify"
#define MEDIA_STREAM_RECORD "Record"
#define MEDIA_STREAM_TTS "TTS"
#define MEDIA_STREAM_ACCESSIBILITY "Health"
#define MEDIA_STREAM_SPORT "Sport"
#define MEDIA_STREAM_INFO "Info"
#define MEDIA_STREAM_MUSIC "Music"
#define MEDIA_STREAM_EMERGENCY "Emergency"
#define MEDIA_STREAM_CALLRING "CallRing"
#define MEDIA_STREAM_MEDIA "Media" /* video */
#define MEDIA_STREAM_A2DP_SNK "A2dpsnk" /* bt music */
#define MEDIA_STREAM_SCO "SCO" /* @deprecated */
#define MEDIA_STREAM_CAPTURE "Capture" /* @deprecated */

/* Source types, for recorder. */

#define MEDIA_SOURCE_MIC "Capture"

#ifdef __cplusplus
}
#endif

#endif /* FRAMEWORKS_MEDIA_INCLUDE_MEDIA_STREAM_H */
