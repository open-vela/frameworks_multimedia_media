/****************************************************************************
 * frameworks/media/utils/media_event.c
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

#include <media_event.h>
#include <media_utils.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

const char* media_event_get_name(int event)
{
    switch (event) {
    case MEDIA_EVENT_NOP:
        return "NOP";
    case MEDIA_EVENT_PREPARED:
        return "PREPARED";
    case MEDIA_EVENT_STARTED:
        return "STARTED";
    case MEDIA_EVENT_PAUSED:
        return "PAUSED";
    case MEDIA_EVENT_STOPPED:
        return "STOPPED";
    case MEDIA_EVENT_SEEKED:
        return "SEEKED";
    case MEDIA_EVENT_COMPLETED:
        return "COMPLETED";
    case MEDIA_EVENT_CHANGED:
        return "CHANGED";
    case MEDIA_EVENT_UPDATED:
        return "UPDATED";
    case MEDIA_EVENT_START:
        return "START";
    case MEDIA_EVENT_PAUSE:
        return "START";
    case MEDIA_EVENT_STOP:
        return "STOP";
    case MEDIA_EVENT_PREV_SONG:
        return "PREV_SONG";
    case MEDIA_EVENT_NEXT_SONG:
        return "NEXT_SONG";
    case MEDIA_EVENT_INCREASE_VOLUME:
        return "INCREASE_VOLUME";
    case MEDIA_EVENT_DECREASE_VOLUME:
        return "DECREASE_VOLUME";
    default:
        return "UNKOWN";
    }
}
