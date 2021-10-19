/****************************************************************************
 * frameworks/media/media_graph.c
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

#include <nuttx/fs/fs.h>

#include <libavdevice/avdevice.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/internal.h>
#include <libavfilter/movie_async.h>
#include <libavutil/opt.h>

#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <syslog.h>
#include <sys/eventfd.h>

#include <media_api.h>
#include "media_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MAX_GRAPH_SIZE      4096

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct MediaGraphPriv {
    AVFilterGraph *graph;
    struct file   *filep;
    int            fd;
} MediaGraphPriv;

typedef struct MediaPlayerPriv {
    AVFilterContext *filter;
    float           volume;
} MediaPlayerPriv;

/****************************************************************************
 * Private Graph Functions
 ****************************************************************************/

static void media_graph_filter_ready(AVFilterContext *ctx)
{
    MediaGraphPriv *priv = ctx->graph->opaque;
    eventfd_t val = 1;

    file_write(priv->filep, &val, sizeof(eventfd_t));
}

static void media_graph_log_callback(void *avcl, int level,
                                     const char *fmt, va_list vl)
{
    switch (level) {
        case AV_LOG_PANIC:
            level = LOG_EMERG;
            break;
        case AV_LOG_FATAL:
            level = LOG_ALERT;
            break;
        case AV_LOG_ERROR:
            level = LOG_ERR;
            break;
        case AV_LOG_WARNING:
            level = LOG_WARNING;
            break;
        case AV_LOG_INFO:
            level = LOG_INFO;
            break;
        case AV_LOG_VERBOSE:
        case AV_LOG_DEBUG:
        case AV_LOG_TRACE:
            level = LOG_DEBUG;
            break;
    }

    vsyslog(level, fmt, vl);
}

static int media_graph_loadgraph(MediaGraphPriv *priv, char *conf)
{
    char graph_desc[MAX_GRAPH_SIZE];
    AVFilterInOut *input = NULL;
    AVFilterInOut *output = NULL;
    int ret;
    int fd;

    fd = open(conf, O_RDONLY | O_BINARY);
    if (fd < 0) {
        av_log(NULL, AV_LOG_ERROR, "%s, can't open media graph file\n", __func__);
        return -errno;
    }

    ret = read(fd, graph_desc, MAX_GRAPH_SIZE);
    close(fd);
    if (ret < 0)
        return -errno;
    else if (ret == MAX_GRAPH_SIZE)
        return -EFBIG;

    graph_desc[ret] = 0;
    av_log(NULL, AV_LOG_INFO, "%s, loadgraph:\n%s\n", __func__, graph_desc);

    avdevice_register_all();
    av_log_set_callback(media_graph_log_callback);

    priv->graph = avfilter_graph_alloc();
    if (!priv->graph)
        return -ENOMEM;

    ret = avfilter_graph_parse2(priv->graph, graph_desc, &input, &output);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "%s, media graph parse error\n", __func__);
        goto out;
    }

    avfilter_inout_free(&input);
    avfilter_inout_free(&output);

    ret = avfilter_graph_config(priv->graph, NULL);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "%s, media graph config error\n", __func__);
        goto out;
    }

    priv->graph->ready  = media_graph_filter_ready;
    priv->graph->opaque = priv;

    av_log(NULL, AV_LOG_ERROR, "%s, loadgraph succeed\n", __func__);
    return 0;
out:
    avfilter_graph_free(&priv->graph);
    return ret;
}

/****************************************************************************
 * Public Graph Functions
 ****************************************************************************/

void *media_graph_create(void *file)
{
    MediaGraphPriv *priv;
    int ret;

    priv = malloc(sizeof(MediaGraphPriv));
    if (!priv)
        return NULL;

    priv->fd = eventfd(0, 0);
    if (priv->fd < 0)
        goto err;

    ret = fs_getfilep(priv->fd, &priv->filep);
    if (ret < 0)
        goto err;

    ret = media_graph_loadgraph(priv, file);
    if (ret < 0)
        goto err;

    return priv;
err:
    if (priv->fd > 0)
        close(priv->fd);
    free(priv);
    return NULL;
}

int media_graph_destroy(void *handle)
{
    MediaGraphPriv *priv = handle;

    if (!priv || !priv->graph)
        return -EINVAL;

    avfilter_graph_free(&priv->graph);
    free(priv);

    return 0;
}

int media_graph_get_pollfds(void *handle, struct pollfd *fds,
                            void **cookies, int count)
{
    MediaGraphPriv *priv = handle;
    int ret, nfd, i;

    if (!priv || !priv->graph || !fds || count < 2)
        return -EINVAL;

    fds[0].fd     = priv->fd;
    fds[0].events = POLLIN;
    cookies[0]    = NULL;
    nfd           = 1;

    for (i = 0; i < priv->graph->nb_filters; i++) {
        AVFilterContext *filter = priv->graph->filters[i];

        ret = avfilter_process_command(filter, "get_pollfd", NULL,
                (char *)&fds[nfd], sizeof(struct pollfd) * (count - nfd),
                AV_OPT_SEARCH_CHILDREN);
        if (ret < 0)
            continue;

        while (fds[nfd].events) {
            cookies[nfd++] = filter;
            if (nfd > count)
                return -EINVAL;
        }
    }

    return nfd;
}

int media_graph_poll_available(void *handle, struct pollfd *fd, void *cookie)
{
    MediaGraphPriv *priv = handle;
    eventfd_t unuse;
    int ret;

    if (!priv || !priv->graph || !fd)
        return -EINVAL;

    if (cookie)
        avfilter_process_command(cookie, "poll_available", NULL,
                                 (char *)fd, sizeof(struct pollfd),
                                 AV_OPT_SEARCH_CHILDREN);
    else
        eventfd_read(priv->fd, &unuse);

    do {
        ret = ff_filter_graph_run_once(priv->graph);
    } while (ret == 0);

    return 0;
}

int media_graph_process_command(void *handle, const char *target,
                                const char *cmd, const char *arg,
                                char *res, int res_len)
{
    MediaGraphPriv *priv = handle;

    if (!priv || !priv->graph)
        return -EINVAL;

    return avfilter_graph_send_command(priv->graph, target, cmd, arg, res, res_len, 0);
}

void media_graph_dump(void *handle, const char *options)
{
    MediaGraphPriv *priv = handle;
    char *tmp;

    if (!priv || !priv->graph)
        return;

    tmp = avfilter_graph_dump_ext(priv->graph, options);
    if (tmp) {
        av_log(NULL, AV_LOG_INFO, "%s\n%s", __func__, tmp);
        free(tmp);
    }
}

/****************************************************************************
 * Private Player Functions
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
        if (ret)
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
 * Public Player Functions
 ****************************************************************************/

void *media_player_open_(void *graph, const char *name)
{
    MediaGraphPriv *media = graph;
    AVFilterContext *filter;
    MediaPlayerPriv *priv;
    int ret, i;

    if (!media || !media->graph)
        return NULL;

    for (i = 0; i < media->graph->nb_filters; i++) {
        filter = media->graph->filters[i];

        if (!filter->opaque && !strcmp(filter->filter->name, "amovie_async")) {
            if (!name || !strcmp(filter->name, name))
                break;
        }
    }

    if (i == media->graph->nb_filters)
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

int media_player_close_(void *handle, int pending_stop)
{
    MediaPlayerPriv *priv = handle;
    char tmp[16];
    int ret;

    if (!priv)
        return -EINVAL;

    snprintf(tmp, sizeof(tmp), "%d", pending_stop);

    ret = avfilter_process_command(priv->filter, "close", tmp, NULL, 0, 0);
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

    if (!priv || !url)
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

/****************************************************************************
 * Public Recorder Functions
 ****************************************************************************/

void *media_recorder_open_(void *graph, const char *name)
{
    MediaGraphPriv *media = graph;
    AVFilterContext *filter;
    int ret, i;

    if (!media || !media->graph)
        return NULL;

    for (i = 0; i < media->graph->nb_filters; i++) {
        filter = media->graph->filters[i];

        if (!filter->opaque && !strcmp(filter->filter->name, "amoviesink_async")) {
            if (!name || !strcmp(filter->name, name))
                break;
        }
    }

    if (i == media->graph->nb_filters)
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

    if (!handle || !url)
        return -EINVAL;

    if (options) {
        ret = avfilter_process_command(handle, "set_options", options, NULL, 0, 0);
        if (ret < 0)
            return ret;
    }

    return avfilter_process_command(handle, "prepare", url, NULL, 0, 0);
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