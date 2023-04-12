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

typedef void (*media_event_callback)(void* cookie, int event, int ret,
    const char* extra);

#endif /* FRAMEWORKS_MEDIA_INCLUDE_MEDIA_EVENT_H */
