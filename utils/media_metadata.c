/****************************************************************************
 * frameworks/media/utils/media_metadata.c
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

#include <errno.h>
#include <media_metadata.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void media_metadata_init(media_metadata_t* data)
{
    memset(data, 0, sizeof(media_metadata_t));
}

void media_metadata_deinit(media_metadata_t* data)
{
    if (data) {
        free(data->title);
        data->title = NULL;
        free(data->artist);
        data->artist = NULL;
    }
}

void media_metadata_reinit(media_metadata_t* data)
{
    media_metadata_deinit(data);
    media_metadata_init(data);
}

void media_metadata_update(media_metadata_t* data, media_metadata_t* diff)
{
    data->flags |= diff->flags;
    if (diff->flags & MEDIA_METAFLAG_STATE)
        data->state = diff->state;

    if (diff->flags & MEDIA_METAFLAG_VOLUME)
        data->volume = diff->volume;

    if (diff->flags & MEDIA_METAFLAG_POSITION)
        data->position = diff->position;

    if (diff->flags & MEDIA_METAFLAG_DURATION)
        data->duration = diff->duration;

    if (diff->flags & MEDIA_METAFLAG_TITLE) {
        free(data->title);
        data->title = diff->title;
        diff->title = NULL;
    }

    if (diff->flags & MEDIA_METAFLAG_ARTIST) {
        free(data->artist);
        data->artist = diff->artist;
        diff->artist = NULL;
    }
}

int media_metadata_serialize(const media_metadata_t* data, char* base, int len)
{
    /* TODO: should append these fields to parcel in need. */
    return snprintf(base, len, "%d:%d:%d:%u:%u:%s\t%s", data->flags, data->state,
        data->volume, data->position, data->duration, data->title, data->artist);
}

int media_metadata_unserialize(media_metadata_t* data, const char* str)
{
    char title[128], artist[128];
    int ret;

    if (!str)
        return -EINVAL;

    ret = sscanf(str, "%d:%d:%d:%u:%u:%s\t%s", &data->flags, &data->state,
        &data->volume, &data->position, &data->duration, title, artist);

    if ((data->flags & MEDIA_METAFLAG_TITLE) && title[0])
        data->title = strdup(title);

    if ((data->flags & MEDIA_METAFLAG_ARTIST) && artist[0])
        data->artist = strdup(artist);

    return ret;
}
