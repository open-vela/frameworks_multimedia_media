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
#include <libavfilter/filters.h>
#include <libavfilter/internal.h>
#include <libavfilter/movie_async.h>
#include <libavutil/opt.h>

#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/eventfd.h>

#include <media_api.h>
#include "media_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MAX_GRAPH_SIZE      4096
#define MAX_POLL_FILTERS    16

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct MediaGraphPriv {
    AVFilterGraph       *graph;
    struct file         *filep;
    int                  fd;
    pid_t                tid;
    void                *pollfts[MAX_POLL_FILTERS];
    int                  pollftn;
    struct MediaCommand *cmdhead;
    struct MediaCommand *cmdtail;
} MediaGraphPriv;

typedef struct MediaPlayerPriv {
    AVFilterContext *filter;
    char            *name;
} MediaPlayerPriv;

typedef struct MediaCommand {
    AVFilterContext     *filter;
    char                *cmd;
    char                *arg;
    int                  flags;
    struct MediaCommand *next;
} MediaCommand;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void media_graph_filter_ready(AVFilterContext *ctx)
{
    MediaGraphPriv *priv = ctx->graph->opaque;
    eventfd_t val = 1;

    if (priv->tid != gettid())
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

static int media_graph_load(MediaGraphPriv *priv, char *conf)
{
    char graph_desc[MAX_GRAPH_SIZE];
    AVFilterInOut *input = NULL;
    AVFilterInOut *output = NULL;
    int ret;
    int fd;

    av_log_set_callback(media_graph_log_callback);
    avdevice_register_all();

    av_log(NULL, AV_LOG_INFO, "%s, loadgraph from file: %s\n", __func__, conf);

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

    priv->pollftn = 0;
    for (fd = 0; fd < priv->graph->nb_filters; fd++) {
        AVFilterContext *filter = priv->graph->filters[fd];

        if ((filter->filter->flags & AVFILTER_FLAG_SUPPORT_POLL) != 0) {
            priv->pollfts[priv->pollftn++] = filter;
            if (priv->pollftn > MAX_POLL_FILTERS) {
                av_log(NULL, AV_LOG_ERROR, "%s, media graph too many pollfds\n", __func__);
                goto out;
            }
        }
    }

    av_log(NULL, AV_LOG_INFO, "%s, loadgraph succeed\n", __func__);
    return 0;
out:
    avfilter_graph_free(&priv->graph);
    return ret;
}

static int media_graph_queue_command(AVFilterContext *filter,
                                     const char *cmd, const char *arg,
                                     char **res, int res_len, int flags)
{
    MediaGraphPriv *priv;
    MediaCommand *tmp;

    priv = media_get_graph();
    if (!priv)
        return -EINVAL;

    if (res && *res)
        return avfilter_process_command(filter, cmd, arg, *res, res_len, flags);

    if (!priv->cmdhead &&
        !ff_filter_graph_has_pending_status(filter->graph)) {
        av_log(NULL, AV_LOG_INFO, "%s:do name:%s cmd:%s arg:%s\n", __func__,
               filter->name, cmd, arg);
        return avfilter_process_command(filter, cmd, arg, NULL, 0, flags);
    }

    tmp = malloc(sizeof(MediaCommand));
    if (!tmp)
        return -ENOMEM;

    tmp->cmd = strdup(cmd);
    if (!tmp->cmd)
        goto err1;

    if (arg) {
        tmp->arg = strdup(arg);
        if (!tmp->arg)
            goto err2;
    } else
        tmp->arg = NULL;

    tmp->filter = filter;
    tmp->flags  = flags;
    tmp->next   = NULL;

    if (!priv->cmdhead)
        priv->cmdhead = tmp;
    else
        priv->cmdtail->next = tmp;

    priv->cmdtail = tmp;
    av_log(NULL, AV_LOG_INFO, "%s:pend name:%s cmd:%s arg:%s\n", __func__,
           filter->name, cmd, arg);
    return 0;

err2:
    free(tmp->cmd);
err1:
    free(tmp);
    return -ENOMEM;
}

static int media_graph_dequeue_command(bool process)
{
    MediaGraphPriv *priv;
    MediaCommand *tmp;
    int ret = 0;

    priv = media_get_graph();
    if (!priv)
        return -EINVAL;

    tmp = priv->cmdhead;
    if (!tmp)
        return -EAGAIN;

    if (process) {
        if (ff_filter_graph_has_pending_status(priv->graph))
            return -EAGAIN;

        av_log(NULL, AV_LOG_INFO, "%s:do name:%s cmd:%s arg:%s\n", __func__,
               tmp->filter->name, tmp->cmd, tmp->arg);
        ret = avfilter_process_command(tmp->filter, tmp->cmd, tmp->arg,
                                       NULL, 0, tmp->flags);
    }

    priv->cmdhead = tmp->next;
    if (!priv->cmdhead)
        priv->cmdtail = NULL;

    av_free(tmp->cmd);
    av_free(tmp->arg);
    av_free(tmp);

    return ret;
}

static void *media_find_filter(void *graph, const char *prefix, const char *name)
{
    MediaGraphPriv *media = graph;
    AVFilterContext *filter = NULL;
    int i;

    if (!media || !media->graph)
        return NULL;

    for (i = 0; i < media->graph->nb_filters; i++) {
        filter = media->graph->filters[i];

        if (!filter->opaque && !strcmp(filter->filter->name, prefix)) {
            if (!name || !strcmp(filter->name, name) ||
                !strncmp(filter->name + strlen(prefix) + 1, name, strlen(name)))
                return filter;
        }
    }

    return NULL;
}

static inline int media_alloc_response(int len, char **res)
{
    if (len && res && !*res) {
        *res = malloc(len);
        if (!*res)
            return -ENOMEM;
    }

    return 0;
}

/****************************************************************************
 * Public Functions
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

    ret = media_graph_load(priv, file);
    if (ret < 0)
        goto err;

    priv->tid = gettid();
    priv->cmdhead = NULL;
    priv->cmdtail = NULL;

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
    int ret;

    if (!priv || !priv->graph)
        return -EINVAL;

    do {
       ret = media_graph_dequeue_command(false);
    } while(ret >= 0);

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

    for (i = 0; i < priv->pollftn; i++) {
        AVFilterContext *filter = priv->pollfts[i];

        ret = avfilter_process_command(filter, "get_pollfd", NULL,
                (char *)&fds[nfd], sizeof(struct pollfd) * (count - nfd),
                AV_OPT_SEARCH_CHILDREN);
        if (ret < 0)
            continue;

        while (ret--) {
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

    if (!priv || !priv->graph || !fd)
        return -EINVAL;

    if (cookie)
        avfilter_process_command(cookie, "poll_available", NULL,
                                 (char *)fd, sizeof(struct pollfd),
                                 AV_OPT_SEARCH_CHILDREN);
    else
        eventfd_read(priv->fd, &unuse);

    return 0;
}

int media_graph_run_once(void *handle)
{
    MediaGraphPriv *priv = handle;
    int ret;

    if (!priv || !priv->graph)
        return -EINVAL;

    ret = ff_filter_graph_run_all(priv->graph);
    if (ret < 0)
        return ret;

    do {
        ret = media_graph_dequeue_command(true);
    } while (ret >= 0);

    return ret == -EAGAIN ? 0 : ret;
}

int media_graph_control(void *handle, const char *target, const char *cmd,
                        const char *arg, char **res, int res_len)
{
    MediaGraphPriv *priv = handle;
    int i, ret = 0;

    if (!priv || !priv->graph)
        return -EINVAL;

    if (!strcmp(cmd, "dump")) {
        if (!res)
            return -EINVAL;

        *res = avfilter_graph_dump_ext(priv->graph, arg);
        return 0;
    }

    ret = media_alloc_response(res_len, res);
    if (ret < 0)
        return ret;

    for (i = 0; i < priv->graph->nb_filters; i++) {
        AVFilterContext *filter = priv->graph->filters[i];

        if (!strcmp(target, filter->name))
            ret = media_graph_queue_command(filter, cmd, arg, res, res_len, 0);
        else {
            const char *tmp = strchr(filter->name, '@');

            if (tmp && !strncmp(tmp + 1, target, strlen(target)))
                ret = media_graph_queue_command(filter, cmd, arg, res, res_len, 0);
        }

        if (ret < 0)
            return ret;
    }

    return 0;
}

int media_player_control(void *handle, const char *target, const char *cmd,
                         const char *arg, char **res, int res_len)
{
    MediaPlayerPriv *priv = handle;
    AVFilterContext *filter;
    char name[64];
    int ret;

    ret = media_alloc_response(res_len, res);
    if (ret < 0)
        return ret;

    if (!strcmp(cmd, "open")) {
        if (!res)
            return -EINVAL;

        if (arg && media_policy_get_stream_name(media_get_policy(), arg, name, sizeof(name)) == 0)
            arg = name;

        filter = media_find_filter(media_get_graph(), MEDIA_PLAYER_NAME, arg);
        if (!filter)
            goto out;

        priv = malloc(sizeof(MediaPlayerPriv));
        if (!priv)
            goto out;

        if (arg) {
            priv->name = strdup(arg);
            if (!priv->name)
                goto err1;
        } else
            priv->name = NULL;

        ret = media_graph_queue_command(filter, "open", NULL, NULL, 0, 0);
        if (ret < 0)
            goto err2;

        priv->filter   = filter;
        filter->opaque = priv;
        goto out;

err2:
        free(priv->name);
err1:
        free(priv);
        priv = NULL;
out:
        snprintf(*res, res_len, "%llu", (uint64_t)(uintptr_t)priv);
        return priv ? 0 : -EINVAL;
    } else if (!strcmp(cmd, "close")) {
        ret = media_graph_queue_command(priv->filter, "close", arg, NULL, 0, 0);
        if (ret >= 0) {
            media_policy_set_stream_status(media_get_policy(), priv->name,
                                           priv->filter->name + sizeof(MEDIA_PLAYER_NAME), false);
            priv->filter->opaque = NULL;
            free(priv->name);
            free(priv);
        }

        return ret;
    } else if (!strcmp(cmd, "start")) {
        media_policy_set_stream_status(media_get_policy(), priv->name,
                                       priv->filter->name + sizeof(MEDIA_PLAYER_NAME), true);
        return media_graph_queue_command(priv->filter, "start", NULL, NULL, 0, 0);
    } else if (!strcmp(cmd, "pause") || !strcmp(cmd, "stop")) {
        ret = media_graph_queue_command(priv->filter, cmd, NULL, NULL, 0, 0);
        media_policy_set_stream_status(media_get_policy(), priv->name,
                                       priv->filter->name + sizeof(MEDIA_PLAYER_NAME), false);
        return ret;
    }

    if (target) {
        filter = avfilter_find_on_link(priv->filter, target, NULL, true, NULL);
        if (!filter)
            return -EINVAL;
    } else
        filter = priv->filter;

    return media_graph_queue_command(filter, cmd, arg, res, res_len, AV_OPT_SEARCH_CHILDREN);
}

int media_player_set_event_callback_(void *handle, void *cookie, media_event_callback event_cb)
{
    MediaPlayerPriv *priv = handle;
    AVMovieAsyncEventCookie event;

    if (!priv)
        return -EINVAL;

    event.event  = event_cb;
    event.cookie = cookie;

    return avfilter_process_command(priv->filter, "set_event", (const char *)&event, NULL, 0, 0);
}

int media_recorder_control(void *handle, const char *target, const char *cmd,
                           const char *arg, char **res, int res_len)
{
    AVFilterContext *filter = handle;
    int ret;

   ret = media_alloc_response(res_len, res);
    if (ret < 0)
        return ret;

    if (!strcmp(cmd, "open")) {
        if (!res)
            return -EINVAL;

        filter = media_find_filter(media_get_graph(), MEDIA_RECORDER_NAME, arg);
        if (!filter)
            goto out;

        ret = media_graph_queue_command(filter, "open", NULL, NULL, 0, 0);
        if (ret < 0)
            goto out;

        filter->opaque = filter;
out:
        return snprintf(*res, res_len, "%llu", (uint64_t)(uintptr_t)filter);
    } else if (!strcmp(cmd, "close")) {
        ret = media_graph_queue_command(filter, "close", NULL, NULL, 0, 0);
        if (ret >= 0)
            filter->opaque = NULL;

        return ret;
    }

    if (target) {
        filter = avfilter_find_on_link(filter, target, NULL, false, NULL);
        if (!filter)
            return -EINVAL;
    }

    return media_graph_queue_command(filter, cmd, arg, res, res_len, AV_OPT_SEARCH_CHILDREN);
}

int media_recorder_set_event_callback_(void *handle, void *cookie, media_event_callback event_cb)
{
    AVMovieAsyncEventCookie event;

    if (!handle)
        return -EINVAL;

    event.event  = event_cb;
    event.cookie = cookie;

    return avfilter_process_command(handle, "set_event", (const char *)&event, NULL, 0, 0);
}

