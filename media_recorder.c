/****************************************************************************
 * frameworks/media/media_recorder.c
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

#include <libavutil/opt.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/movie_async.h>

#include "media_internal.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void *media_recorder_open_(const char *name)
{
    AVFilterGraph *graph = media_server_get_graph_();
    AVFilterContext *filter;
    int ret, i;

    if (!graph)
        return NULL;

    for (i = 0; i < graph->nb_filters; i++) {
        filter = graph->filters[i];

        if (!filter->opaque && !strcmp(filter->filter->name, "amoviesink_async")) {
            if (!name || !strcmp(filter->name, name))
                break;
        }
    }

    if (i == graph->nb_filters)
        return NULL;

    ret = avfilter_process_command(filter, "open", NULL, NULL, 0, 0);
    if (ret < 0)
        return NULL;

    filter->opaque = filter;

    return filter;
}

int media_recorder_close_(void *handle)
{
    AVFilterContext *filter = handle;
    int ret;

    if (!filter)
        return -EINVAL;

    ret = avfilter_process_command(filter, "close", NULL, NULL, 0, 0);
    if (ret < 0)
        return ret;

    filter->opaque = NULL;
    return 0;
}

int media_recorder_set_event_callback_(void *handle, void *cookie,
                                       media_event_callback event_cb)
{
    AVMovieAsyncEventCookie event;

    if (!handle)
        return -EINVAL;

    event.event  = event_cb;
    event.cookie = cookie;

    return avfilter_process_command(handle, "set_event", (const char *)&event, NULL, 0, 0);
}

int media_recorder_prepare_(void *handle, const char *url, const char *options)
{
    int ret;

    if (!handle)
        return -EINVAL;

    ret = avfilter_process_command(handle, "set_url", url, NULL, 0, 0);
    if (ret < 0)
        return ret;

    if (options) {
        ret = avfilter_process_command(handle, "set_options", options, NULL, 0, 0);
        if (ret < 0)
            return ret;
    }

    return avfilter_process_command(handle, "prepare", NULL, NULL, 0, 0);
}

int media_recorder_reset_(void *handle)
{
    if (!handle)
        return -EINVAL;

    return avfilter_process_command(handle, "reset", NULL, NULL, 0, 0);
}

int media_recorder_start_(void *handle)
{
    if (!handle)
        return -EINVAL;

    return avfilter_process_command(handle, "start", NULL, NULL, 0, 0);
}

int media_recorder_stop_(void *handle)
{
    if (!handle)
        return -EINVAL;

    return avfilter_process_command(handle, "stop", NULL, NULL, 0, 0);
}
