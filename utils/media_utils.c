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

#include <cutils/properties.h>
#include <media_defs.h>
#include <media_utils.h>

#include "media_common.h"

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

const char* media_id_get_name(int id)
{
    switch (id) {
    case MEDIA_ID_GRAPH:
        return "graph";
    case MEDIA_ID_POLICY:
        return "policy";
    case MEDIA_ID_PLAYER:
        return "player";
    case MEDIA_ID_RECORDER:
        return "recorder";
    case MEDIA_ID_SESSION:
        return "session";
    case MEDIA_ID_FOCUS:
        return "focus";
    default:
        return "none";
    }
}

const char* media_get_cpuname(void)
{
#ifdef CONFIG_MEDIA_SERVER_CPUNAME
    return CONFIG_MEDIA_SERVER_CPUNAME;
#else
    static char cpuname[PROPERTY_VALUE_MAX];
    if (cpuname[0] == '\0')
        property_get("vendor.audio.server.cpuname", cpuname, "audio");
    return cpuname;
#endif
}
