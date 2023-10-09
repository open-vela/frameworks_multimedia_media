/****************************************************************************
 * frameworks/media/include/media_event.h
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

#ifndef FRAMEWORKS_MEDIA_INCLUDE_MEDIA_EVENT_H
#define FRAMEWORKS_MEDIA_INCLUDE_MEDIA_EVENT_H

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Status change */

#define MEDIA_EVENT_NOP 0
#define MEDIA_EVENT_PREPARED 1
#define MEDIA_EVENT_STARTED 2
#define MEDIA_EVENT_PAUSED 3
#define MEDIA_EVENT_STOPPED 4
#define MEDIA_EVENT_SEEKED 5
#define MEDIA_EVENT_COMPLETED 6

/* Status change of client */

#define MEDIA_EVENT_PREVED 100
#define MEDIA_EVENT_NEXTED 101

/* Control Message */

#define MEDIA_EVENT_START 200
#define MEDIA_EVENT_PAUSE 201
#define MEDIA_EVENT_STOP 202
#define MEDIA_EVENT_PREV 203
#define MEDIA_EVENT_NEXT 204

/****************************************************************************
 * Public Types
 ****************************************************************************/

/**
 * @brief Define a event-callback function pointer when media comes.
 *
 * Notify some events back to users, such as MEDIA_EVENT_STARTED, which means
 * the av stream is successfully played.
 *
 * @param[in] cookie Usr's private context.
 * @param[in] event  The type of current event, all macro definitions of
 *                   events are listed as following:
 *                   controller's callback can receive status-changed
 *                   notifications as events:
 *                   -   MEDIA_EVENT_NOP
 *                   -   MEDIA_EVENT_PREPARED
 *                   -   MEDIA_EVENT_STARTED
 *                   -   MEDIA_EVENT_PAUSED
 *                   -   MEDIA_EVENT_STOPPED
 *                   -   MEDIA_EVENT_SEEKED
 *                   -   MEDIA_EVENT_COMPLETED
 *                   -   MEDIA_EVENT_PREVED
 *                   -   MEDIA_EVENT_NEXTED
 *                  controllee's callback can receive control message as events:
 *                   -   MEDIA_EVENT_START
 *                   -   MEDIA_EVENT_PAUSE
 *                   -   MEDIA_EVENT_STOP
 *                   -   MEDIA_EVENT_PREV
 *                   -   MEDIA_EVENT_NEXT
 *                  player/recorder's can receive status-changed notifications
 *                  as events:
 *                   -   MEDIA_EVENT_PREPARED
 *                   -   MEDIA_EVENT_STARTED
 *                   -   MEDIA_EVENT_PAUSED
 *                   -   MEDIA_EVENT_STOPPED
 *                   -   MEDIA_EVENT_SEEKED
 *                   -   MEDIA_EVENT_COMPLETED
 * @param[in] ret   The result of this callback function
 * @param[in] extra Extra data needed to complete this callback function.
 * @note There is no playlist in the media framework,so PREVED/NEXTED will not be
 *       notified to player's callback, but the controllee user can actively notify PREVED/NEXTED
 *       to controller user through the media framework.
 *
 */
typedef void (*media_event_callback)(void* cookie, int event, int ret,
    const char* extra);

/**
 * @brief Common async callback.
 *
 * @param[out] cookie   Private context.
 * @param[out] ret      Result of process, zero on success, negative errno on failure.
 */
typedef void (*media_uv_callback)(void* cookie, int ret);

#endif /* FRAMEWORKS_MEDIA_INCLUDE_MEDIA_EVENT_H */
