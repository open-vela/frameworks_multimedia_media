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
#include <media_api.h>
#include <sys/eventfd.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

#include "media_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MAX_GRAPH_SIZE 4096
#define MAX_POLL_FILTERS 16

#define MediaPlayerPriv MediaFilterPriv
#define MediaRecorderPriv MediaFilterPriv

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct MediaGraphPriv {
    AVFilterGraph* graph;
    struct file* filep;
    int fd;
    pid_t tid;
    void* pollfts[MAX_POLL_FILTERS];
    int pollftn;
    struct MediaCommand* cmdhead;
    struct MediaCommand* cmdtail;
} MediaGraphPriv;

typedef struct MediaFilterPriv {
    AVFilterContext* filter;
    void* cookie;
    bool event;
} MediaFilterPriv;

typedef struct MediaCommand {
    AVFilterContext* filter;
    char* cmd;
    char* arg;
    int flags;
    struct MediaCommand* next;
} MediaCommand;

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const char* g_media_inputs[] = {
    "amovie_async",
    "movie_async",
    NULL,
};

static const char* g_media_outputs[] = {
    "amoviesink_async",
    "moviesink_async",
    NULL,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void media_graph_filter_ready(AVFilterContext* ctx)
{
    MediaGraphPriv* priv = ctx->graph->opaque;
    eventfd_t val = 1;

    if (priv->tid != gettid())
        file_write(priv->filep, &val, sizeof(eventfd_t));
}

static void media_graph_log_callback(void* avcl, int level,
    const char* fmt, va_list vl)
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

static int media_graph_load(MediaGraphPriv* priv, char* conf)
{
    char graph_desc[MAX_GRAPH_SIZE];
    AVFilterInOut* input = NULL;
    AVFilterInOut* output = NULL;
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

    priv->graph->ready = media_graph_filter_ready;
    priv->graph->opaque = priv;

    priv->pollftn = 0;
    for (fd = 0; fd < priv->graph->nb_filters; fd++) {
        AVFilterContext* filter = priv->graph->filters[fd];

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

static int media_graph_queue_command(MediaGraphPriv* priv, AVFilterContext* filter,
    const char* cmd, const char* arg, char* res, int res_len, int flags)
{
    MediaCommand* tmp;

    if (res && res_len > 0)
        return avfilter_process_command(filter, cmd, arg, res, res_len, flags);

    if (!priv->cmdhead && !ff_filter_graph_has_pending_status(filter->graph)) {
        av_log(NULL, AV_LOG_INFO, "process %s %s %s\n",
            filter->name, cmd, arg ? arg : "_");
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
    tmp->flags = flags;
    tmp->next = NULL;

    if (!priv->cmdhead)
        priv->cmdhead = tmp;
    else
        priv->cmdtail->next = tmp;

    priv->cmdtail = tmp;
    av_log(NULL, AV_LOG_INFO, "pending %s %s %s\n",
        filter->name, cmd, arg ? arg : "_");
    return 0;

err2:
    free(tmp->cmd);
err1:
    free(tmp);
    return -ENOMEM;
}

static int media_graph_dequeue_command(MediaGraphPriv* priv, bool process)
{
    MediaCommand* tmp;
    int ret = 0;

    tmp = priv->cmdhead;
    if (!tmp)
        return -EAGAIN;

    if (process) {
        if (ff_filter_graph_has_pending_status(priv->graph))
            return -EAGAIN;

        av_log(NULL, AV_LOG_INFO, "process %s %s %s\n",
            tmp->filter->name, tmp->cmd, tmp->arg ? tmp->arg : "_");
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

static void* media_find_filter(MediaGraphPriv* priv, const char* prefix,
    bool input, bool available)
{
    AVFilterContext* filter = NULL;
    char name[64];
    int i, j;

    for (i = 0; i < priv->graph->nb_filters; i++) {
        filter = priv->graph->filters[i];

        if (!available || !filter->opaque) {
            if (!prefix) {
                // find an available input/output as prefix is not specified.
                if (input) {
                    for (j = 0; g_media_inputs[j]; j++)
                        if (!strcmp(filter->filter->name, g_media_inputs[j]))
                            return filter;
                } else {
                    for (j = 0; g_media_outputs[j]; j++)
                        if (!strcmp(filter->filter->name, g_media_outputs[j]))
                            return filter;
                }
            } else {
                // policy might do mapping (e.g. "Music" => "amovie_async@Music")
                if (0 == media_policy_get_stream_name(prefix, name, sizeof(name)))
                    prefix = name;

                if (!strncmp(filter->name, prefix, strlen(prefix)))
                    return filter;
            }
        }
    }

    return NULL;
}

static void media_common_notify_cb(void* cookie, int event,
    int result, const char* extra)
{
    MediaFilterPriv* ctx = cookie;

    if (ctx->event)
        media_stub_notify_event(ctx->cookie, event, result, extra);
}

static void media_common_event_cb(void* cookie, int event,
    int result, const char* extra)
{
    MediaFilterPriv* ctx = cookie;

    switch (event) {
    case AVMOVIE_ASYNC_EVENT_STARTED:
        if (result == 0)
            media_policy_set_stream_status(ctx->filter->name, true);
        break;

    case AVMOVIE_ASYNC_EVENT_PAUSED:
    case AVMOVIE_ASYNC_EVENT_STOPPED:
    case AVMOVIE_ASYNC_EVENT_COMPLETED:
        media_policy_set_stream_status(ctx->filter->name, false);
        break;

    case AVMOVIE_ASYNC_EVENT_CLOSED:
        media_policy_set_stream_status(ctx->filter->name, false);
        media_stub_notify_finalize(&ctx->cookie);
        ctx->filter->opaque = NULL;
        free(ctx);
        return;
    }

    media_common_notify_cb(ctx, event, result, extra);
}

static MediaFilterPriv* media_common_open(MediaGraphPriv* priv,
    const char* arg, void* cookie, bool player)
{
    AVMovieAsyncEventCookie event;
    MediaFilterPriv* ctx;

    ctx = zalloc(sizeof(MediaFilterPriv));
    if (!ctx)
        return NULL;

    ctx->filter = media_find_filter(priv, arg, player, true);
    if (!ctx->filter)
        goto err;

    /* Launch filter worker thread. */
    if (avfilter_process_command(ctx->filter, "open", NULL, NULL, 0, 0) < 0)
        goto err;

    event.cookie = ctx;
    event.event = media_common_event_cb;

    if (avfilter_process_command(ctx->filter, "set_event", (const char*)&event, NULL, 0, 0) < 0) {
        avfilter_process_command(ctx->filter, "close", NULL, NULL, 0, 0);
        goto err;
    }

    ctx->cookie = cookie;
    ctx->filter->opaque = ctx;

    return ctx;

err:
    free(ctx);
    return NULL;
}

static int media_common_handler(MediaGraphPriv* priv, void* handle,
    const char* target, const char* cmd, const char* arg,
    char* res, int res_len, bool player)
{
    MediaFilterPriv* ctx = handle;
    AVFilterContext* filter;
    int pending;

    if (!strcmp(cmd, "open")) {
        ctx = media_common_open(priv, arg, handle, player);
        if (!ctx)
            return -EINVAL;

        return snprintf(res, res_len, "%" PRIu64 "", (uint64_t)(uintptr_t)ctx);
    }

    if (!strcmp(cmd, "set_event")) {
        ctx->event = true;

        return 0;
    }

    if (!strcmp(cmd, "close")) {
        sscanf(arg, "%d", &pending);
        if (!pending)
            media_stub_notify_finalize(&ctx->cookie);
    }

    if (target) {
        filter = avfilter_find_on_link(ctx->filter, target, NULL, player, NULL);
        if (!filter)
            return -EINVAL;
    } else
        filter = ctx->filter;

    return media_graph_queue_command(priv, filter, cmd, arg, res, res_len, AV_OPT_SEARCH_CHILDREN);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void* media_graph_create(void* file)
{
    MediaGraphPriv* priv;
    int ret;

    priv = malloc(sizeof(MediaGraphPriv));
    if (!priv)
        return NULL;

    priv->fd = eventfd(0, EFD_CLOEXEC);
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

int media_graph_destroy(void* graph)
{
    MediaGraphPriv* priv = graph;
    int ret;

    do {
        ret = media_graph_dequeue_command(priv, false);
    } while (ret >= 0);

    avfilter_graph_free(&priv->graph);
    free(priv);

    return 0;
}

int media_graph_get_pollfds(void* graph, struct pollfd* fds,
    void** cookies, int count)
{
    MediaGraphPriv* priv = graph;
    int ret, nfd, i;

    if (!fds || count < 2)
        return -EINVAL;

    fds[0].fd = priv->fd;
    fds[0].events = POLLIN;
    cookies[0] = NULL;
    nfd = 1;

    for (i = 0; i < priv->pollftn; i++) {
        AVFilterContext* filter = priv->pollfts[i];

        ret = avfilter_process_command(filter, "get_pollfd", NULL,
            (char*)&fds[nfd], sizeof(struct pollfd) * (count - nfd),
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

int media_graph_poll_available(void* graph, struct pollfd* fd, void* cookie)
{
    MediaGraphPriv* priv = graph;
    eventfd_t unuse;

    if (!fd)
        return -EINVAL;

    if (cookie)
        avfilter_process_command(cookie, "poll_available", NULL,
            (char*)fd, sizeof(struct pollfd),
            AV_OPT_SEARCH_CHILDREN);
    else
        eventfd_read(priv->fd, &unuse);

    return 0;
}

int media_graph_run_once(void* graph)
{
    MediaGraphPriv* priv = graph;
    int ret;

    ret = ff_filter_graph_run_all(priv->graph);
    if (ret < 0)
        return ret;

    do {
        ret = media_graph_dequeue_command(priv, true);
    } while (ret >= 0);

    return ret == -EAGAIN ? 0 : ret;
}

int media_graph_handler(void* graph, const char* target, const char* cmd,
    const char* arg, char* res, int res_len)
{
    MediaGraphPriv* priv = graph;
    int i, ret = 0;
    char* dump;

    if (!target && !strcmp(cmd, "dump")) {
        dump = avfilter_graph_dump_ext(priv->graph, arg);
        if (dump)
            syslog(LOG_INFO, "\n%s\n", dump);

        free(dump);
        return 0;
    } else if (!strcmp(cmd, "loglevel")) {
        if (!arg)
            return -EINVAL;

        av_log_set_level(strtol(arg, NULL, 0));
        return 0;
    }

    for (i = 0; i < priv->graph->nb_filters; i++) {
        AVFilterContext* filter = priv->graph->filters[i];

        if (!strcmp(target, filter->name))
            ret = media_graph_queue_command(priv, filter, cmd, arg, res, res_len, 0);
        else {
            const char* tmp = strchr(filter->name, '@');

            if (tmp && !strncmp(tmp + 1, target, strlen(target)))
                ret = media_graph_queue_command(priv, filter, cmd, arg, res, res_len, 0);
        }

        if (ret < 0)
            return ret;
    }

    return 0;
}

int media_player_handler(void* graph, void* handle, const char* target, const char* cmd,
    const char* arg, char* res, int res_len)
{
    return media_common_handler(graph, handle, target, cmd, arg, res, res_len, true);
}

int media_recorder_handler(void* graph, void* handle, const char* target, const char* cmd,
    const char* arg, char* res, int res_len)
{
    return media_common_handler(graph, handle, target, cmd, arg, res, res_len, false);
}
