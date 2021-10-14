/****************************************************************************
 * frameworks/media/media_player.c
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
 * Private Types
 ****************************************************************************/

typedef struct MediaPlayerPriv {
    AVFilterContext *filter;
    float           volume;
} MediaPlayerPriv;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static AVFilterContext *media_player_find(AVFilterContext *filter,
                                          const char *name,
                                          bool (*extra)(AVFilterContext *filter))
{
    AVFilterContext *ret;
    int i;

    if (!strcmp(name, filter->filter->name)) {
        if (!extra || extra(filter))
            return filter;
    }

    for (i = 0; i < filter->nb_outputs; i++) {
        AVFilterContext *next = filter->outputs[i]->dst;

        ret = media_player_find(next, name, extra);
        if (ret < 0)
            return ret;
    }

    return NULL;
}

static bool media_player_fadein_extra(AVFilterContext *filter)
{
    int64_t val = 1;

    av_opt_get_int(filter, "type", AV_OPT_SEARCH_CHILDREN, &val);

    return !val;
}

static bool media_player_fadeout_extra(AVFilterContext *filter)
{
    int64_t val = 0;

    av_opt_get_int(filter, "type", AV_OPT_SEARCH_CHILDREN, &val);

    return !!val;
}

static int media_player_set_fadeinout(AVFilterContext *filter,
                                      bool out, unsigned int duration)
{
    AVFilterContext *fade;
    char tmp[32];
    int ret;

    if (out)
        fade = media_player_find(filter, "afade", media_player_fadeout_extra);
    else
        fade = media_player_find(filter, "afade", media_player_fadein_extra);

    if (!fade)
        return -EINVAL;

    if (duration == 0)
        return avfilter_process_command(fade, "enable", "0", NULL, 0, 0);

    snprintf(tmp, 32, "%f", duration / 1000.0f);
    ret = avfilter_process_command(fade, "duration", tmp, NULL, 0, AV_OPT_SEARCH_CHILDREN);
    if (ret < 0)
        return ret;

    if (out) {
        ret = avfilter_process_command(filter, "get_duration", NULL, tmp, sizeof(tmp), 0);
        if (ret >= 0) {
            int start_time = strtoul(tmp, NULL, 0) - duration;

            snprintf(tmp, 32, "%f", start_time / 1000.0f);
            ret = avfilter_process_command(fade, "start_time", tmp, NULL, 0, AV_OPT_SEARCH_CHILDREN);
            if (ret < 0)
                return ret;
        }
    }

    return avfilter_process_command(fade, "enable", "1", NULL, 0, 0);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void *media_player_open_(const char *name)
{
    AVFilterGraph *graph = media_server_get_graph_();
    AVFilterContext *filter;
    MediaPlayerPriv *priv;
    int ret, i;

    if (!graph)
        return NULL;

    for (i = 0; i < graph->nb_filters; i++) {
        filter = graph->filters[i];

        if (!filter->opaque && !strcmp(filter->filter->name, "amovie_async")) {
            if (!name || !strcmp(filter->name, name))
                break;
        }
    }

    if (i == graph->nb_filters)
        return NULL;

    priv = zalloc(sizeof(MediaPlayerPriv));
    if (!priv)
        return NULL;

    ret = avfilter_process_command(filter, "open", NULL, NULL, 0, 0);
    if (ret < 0) {
        free(priv);
        return NULL;
    }

    priv->filter   = filter;
    priv->volume   = 1.0f;
    filter->opaque = priv;

    media_player_set_fadeinout(priv->filter, true, 0);
    media_player_set_fadeinout(priv->filter, false, 0);

    return priv;
}

int media_player_close_(void *handle)
{
    MediaPlayerPriv *priv = handle;
    int ret;

    if (!priv)
        return -EINVAL;

    ret = avfilter_process_command(priv->filter, "close", NULL, NULL, 0, 0);
    if (ret < 0)
        return ret;

    priv->filter->opaque = NULL;
    free(priv);

    return 0;
}

int media_player_set_event_callback_(void *handle, void *cookie,
                                     media_event_callback event_cb)
{
    MediaPlayerPriv *priv = handle;
    AVMovieAsyncEventCookie event;

    if (!priv)
        return -EINVAL;

    event.event  = event_cb;
    event.cookie = cookie;

    return avfilter_process_command(priv->filter, "set_event", (const char *)&event, NULL, 0, 0);
}

int media_player_prepare_(void *handle, const char *url, const char *options)
{
    MediaPlayerPriv *priv = handle;
    int ret;

    if (!priv)
        return -EINVAL;

    if (options) {
        ret = avfilter_process_command(priv->filter, "set_options", options, NULL, 0, 0);
        if (ret < 0)
            return ret;
    }

    return avfilter_process_command(priv->filter, "prepare", url, NULL, 0, 0);
}

int media_player_reset_(void *handle)
{
    MediaPlayerPriv *priv = handle;

    if (!priv)
        return -EINVAL;

    return avfilter_process_command(priv->filter, "reset", NULL, NULL, 0, 0);
}

int media_player_start_(void *handle)
{
    MediaPlayerPriv *priv = handle;

    if (!priv)
        return -EINVAL;

    return avfilter_process_command(priv->filter, "start", NULL, NULL, 0, 0);
}

int media_player_stop_(void *handle)
{
    MediaPlayerPriv *priv = handle;

    if (!priv)
        return -EINVAL;

    return avfilter_process_command(priv->filter, "stop", NULL, NULL, 0, 0);
}

int media_player_pause_(void *handle)
{
    MediaPlayerPriv *priv = handle;

    if (!priv)
        return -EINVAL;

    return avfilter_process_command(priv->filter, "pause", NULL, NULL, 0, 0);
}

int media_player_set_looping_(void *handle, int loop)
{
    MediaPlayerPriv *priv = handle;
    char tmp[16];

    if (!priv)
        return -EINVAL;

    snprintf(tmp, sizeof(tmp), "%d", loop);

    return avfilter_process_command(priv->filter, "set_loop", tmp, NULL, 0, 0);
}

int media_player_is_playing_(void *handle)
{
    MediaPlayerPriv *priv = handle;
    char tmp[16];
    int ret;

    if (!priv)
        return -EINVAL;

    ret = avfilter_process_command(priv->filter, "get_playing",
                                   NULL, tmp, sizeof(tmp), 0);

    return ret < 0 ? ret : !!atoi(tmp);
}

int media_player_seek_(void *handle, unsigned int msec)
{
    MediaPlayerPriv *priv = handle;
    char tmp[32];

    if (!priv)
        return -EINVAL;

    snprintf(tmp, sizeof(tmp), "%u", msec);

    return avfilter_process_command(priv->filter, "seek", tmp, NULL, 0, 0);
}

int media_player_get_position_(void *handle, unsigned int *msec)
{
    MediaPlayerPriv *priv = handle;
    char tmp[16];
    int ret;

    if (!priv)
        return -EINVAL;

    ret = avfilter_process_command(priv->filter, "get_position",
                                   NULL, tmp, sizeof(tmp), 0);
    if (ret >= 0)
        *msec = strtoul(tmp, NULL, 0);

    return ret;
}

int media_player_get_duration_(void *handle, unsigned int *msec)
{
    MediaPlayerPriv *priv = handle;
    char tmp[16];
    int ret;

    if (!priv)
        return -EINVAL;

    ret = avfilter_process_command(priv->filter, "get_duration",
                                   NULL, tmp, sizeof(tmp), 0);
    if (ret >= 0)
        *msec = strtoul(tmp, NULL, 0);

    return ret;
}

int media_player_set_fadein_(void *handle, unsigned int msec)
{
    MediaPlayerPriv *priv = handle;

    if (!priv)
        return -EINVAL;

    return media_player_set_fadeinout(priv->filter, false, msec);
}

int media_player_set_fadeout_(void *handle, unsigned int msec)
{
    MediaPlayerPriv *priv = handle;

    if (!priv)
        return -EINVAL;

    return media_player_set_fadeinout(priv->filter, true, msec);
}

int media_player_set_volume_(void *handle, float volume)
{
    MediaPlayerPriv *priv = handle;
    AVFilterContext *filter;
    char tmp[32];
    int ret;

    if (!priv)
        return -EINVAL;

    if (volume < 0.0 || volume > 1.0)
        return -EINVAL;

    filter = media_player_find(priv->filter, "volume", NULL);
    if (!filter)
        return -EINVAL;

    snprintf(tmp, 32, "%f", volume);

    ret = avfilter_process_command(filter, "volume", tmp, 0, 0, AV_OPT_SEARCH_CHILDREN);
    if (ret >= 0)
        priv->volume = volume;

    return ret;
}

int media_player_get_volume_(void *handle, float *volume)
{
    MediaPlayerPriv *priv = handle;

    if (!priv)
        return -EINVAL;

    *volume = priv->volume;
    return 0;
}
