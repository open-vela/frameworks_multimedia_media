/****************************************************************************
 * frameworks/media/server/media_graph.c
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
#include <nuttx/sched_note.h>

#include <libavdevice/avdevice.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/filters.h>
#include <libavfilter/formats.h>
#include <libavfilter/avfilter_internal.h>
#include <libavfilter/avfilter-nx.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>

#include <assert.h>
#include <fcntl.h>
#include <media_api.h>
#include <sys/eventfd.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <unistd.h>

#include "media_common.h"
#include "media_plugin.h"
#include "media_server.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MAX_GRAPH_SIZE 4096
#define MAX_POLL_FILTERS 32

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct MediaGraphPriv {
    AVFilterGraph* graph;
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

/****************************************************************************
 * Private Data
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_MEDIA_TRACE
static void media_trace_begin(void* avcl, const char* fmt, va_list vl)
{
    char buffer[128];
    vsnprintf(buffer, sizeof(buffer) - 1, fmt, vl);
    sched_note_beginex(NOTE_TAG_ALWAYS, buffer);
}

static void media_trace_end(void* avcl, const char* fmt, va_list vl)
{
    char buffer[128];
    vsnprintf(buffer, sizeof(buffer) - 1, fmt, vl);
    sched_note_endex(NOTE_TAG_ALWAYS, buffer);
}
#endif

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
#ifdef CONFIG_MEDIA_TRACE
    av_trace_set_callback(media_trace_begin, media_trace_end);
#endif
    avdevice_register_all();

    av_log(NULL, AV_LOG_INFO, "%s, loadgraph from file: %s\n", __func__, conf);

    fd = open(conf, O_RDONLY | O_BINARY | O_CLOEXEC);
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

static int media_find_filter(MediaGraphPriv* priv, const char* prefix,
    bool input, bool available, AVFilterContext** pfilter)
{
    AVFilterContext* filter = NULL;
    char name[64];
    int i, j, ret;

    // policy might do mapping (e.g. "Music" => "amovie_async@Music")
    ret = media_stub_get_stream_name(prefix, name, sizeof(name));

    for (i = 0; i < priv->graph->nb_filters; i++) {
        filter = priv->graph->filters[i];

        if (!available || !filter->opaque) {
            if (!prefix) {
                // find an available input/output as prefix is not specified.
                if (input) {
                    for (j = 0; g_media_inputs[j]; j++)
                        if (!strcmp(filter->filter->name, g_media_inputs[j])) {
                            *pfilter = filter;
                            return 0;
                        }
                } else {
                    for (j = 0; g_media_outputs[j]; j++)
                        if (!strcmp(filter->filter->name, g_media_outputs[j])) {
                            *pfilter = filter;
                            return 0;
                        }
                }
            } else {
                if (ret == 0)
                    prefix = name;

                if (!strncmp(filter->name, prefix, strlen(prefix))) {
                    *pfilter = filter;
                    return 0;
                }
            }
        }
    }

    *pfilter = NULL;
    if (ret == 0) /* filter found in policy but not available. */
        return -EINVAL;
    return -ENOTSUP;
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
            media_stub_set_stream_status(ctx->filter->name, true);
        break;

    case AVMOVIE_ASYNC_EVENT_PAUSED:
    case AVMOVIE_ASYNC_EVENT_STOPPED:
    case AVMOVIE_ASYNC_EVENT_COMPLETED:
        media_stub_set_stream_status(ctx->filter->name, false);
        break;

    case AVMOVIE_ASYNC_EVENT_CLOSED:
        media_stub_set_stream_status(ctx->filter->name, false);
        media_server_set_data(ctx->cookie, NULL);
        media_stub_notify_finalize(&ctx->cookie);
        ctx->filter->opaque = NULL;
        free(ctx);
        return;
    }

    media_common_notify_cb(ctx, event, result, extra);
}

static int media_common_open(MediaGraphPriv* priv,
    const char* arg, void* cookie, bool player, MediaFilterPriv** pctx)
{
    AVMovieAsyncEventCookie event;
    MediaFilterPriv* ctx;
    int ret;

    *pctx = NULL;
    ctx = zalloc(sizeof(MediaFilterPriv));
    if (!ctx)
        return -ENOMEM;

    ret = media_find_filter(priv, arg, player, true, &ctx->filter);
    if (ret < 0)
        goto err;

    /* Launch filter worker thread. */
    ret = avfilter_process_command(ctx->filter, "open", NULL, NULL, 0, 0);
    if (ret < 0)
        goto err;

    event.cookie = ctx;
    event.event = media_common_event_cb;

    ret = avfilter_process_command(ctx->filter, "set_event", (const char*)&event, NULL, 0, 0);
    if (ret < 0) {
        avfilter_process_command(ctx->filter, "close", NULL, NULL, 0, 0);
        goto err;
    }

    ctx->cookie = cookie;
    ctx->filter->opaque = ctx;
    *pctx = ctx;
    return 0;

err:
    free(ctx);
    return ret;
}

static int media_common_handler(media_plugin_t* pctx, struct media_server_conn* conn,
    const char* target, const char* cmd, const char* arg, int player, char* res, int res_len)
{
    MediaGraphPriv* priv = pctx->priv;
    MediaFilterPriv* ctx = media_server_get_data(conn);
    AVFilterContext* filter = NULL;
    char url[PATH_MAX];
    int pending;
    int ret = 0;

    if (!strcmp(cmd, "open")) {
        ret = media_common_open(priv, arg, conn, player, &ctx);
        if (ret < 0)
            return ret;

        media_server_set_data(conn, ctx);
        return 0;
    }

    if (!ctx)
        return -EINVAL;

    if (!strcmp(cmd, "set_event")) {
        ctx->event = true;
        return 0;
    }

    if (!strcmp(cmd, "prepare") && target) {
        /* Buffer mode, use `target` as cpuname, `arg` as sockname. */
        if (!strcmp(target, CONFIG_RPMSG_LOCAL_CPUNAME))
            snprintf(url, sizeof(url), "unix:%s?listen=0", arg);
        else
            snprintf(url, sizeof(url), "rpmsg:%s:%s?listen=0", arg, target);

        arg = url;
    } else if (!strcmp(cmd, "close")) {
        /* Direct end notification if close without pending. */
        if (arg)
            sscanf(arg, "%d", &pending);

        if (!arg || !pending) {
            media_server_set_data(ctx->cookie, NULL);
            media_stub_notify_finalize(&ctx->cookie);
        }
    } else if (target) {
        /* Find other filter if user specified one. */
        filter = avfilter_find_on_link(ctx->filter, target, NULL, player, NULL);
        if (!filter)
            return -EINVAL;
    }

    if (!filter)
        filter = ctx->filter;

    return media_graph_queue_command(priv, filter, cmd, arg,
        res, res_len, AV_OPT_SEARCH_CHILDREN);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

static int media_graph_init(MediadPlugin *ctx)
{
    char *file = CONFIG_MEDIA_SERVER_CONFIG_PATH "graph.conf";
    MediaGraphPriv* priv = ctx->priv;
    int ret;

    priv->fd = eventfd(0, EFD_CLOEXEC);
    if (priv->fd < 0) {
        ret = -errno;
        goto err;
    }

    ret = fs_getfilep(priv->fd, &priv->filep);
    if (ret < 0)
        goto err;

    ret = media_graph_load(priv, file);
    if (ret < 0)
        goto err;

    priv->tid = gettid();
    return 0;
err:
    if (priv->fd > 0)
        close(priv->fd);

    return ret;
}


static int media_graph_uninit(MediadPlugin *ctx)
{
    MediaGraphPriv* priv = ctx->priv;

    avfilter_graph_free(&priv->graph);

    return 0;
}

static int media_graph_get_pollfds(MediadPlugin *ctx, struct pollfd *fds,
    void **cookies, int count)
{
    MediaGraphPriv* priv = ctx->priv;
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

static int media_graph_poll_available(MediadPlugin *ctx, struct pollfd *fd, void *cookie)
{
    MediaGraphPriv* priv = ctx->priv;
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

static int media_graph_run_once(MediadPlugin *ctx)
{
    MediaGraphPriv* priv = ctx->priv;
    int ret;

    while (1) {
        ret = ff_filter_graph_run_once(priv->graph);
        if (ret < 0)
            break;
    }

    if (ret < 0) {
        if (ret == AVERROR(EAGAIN))
            return 0;
        av_log(NULL, AV_LOG_ERROR, "media graph run error ret:%d:%s\n", ret, av_err2str(ret));
    }

    return 0;
}

static int ff_filter_graph_has_pending_status(AVFilterGraph *graph)
{
    int i, j;

    for (i = 0; i < graph->nb_filters; i++) {
        AVFilterContext *filter = graph->filters[i];
        for (j = 0; j < filter->nb_outputs; j++) {
            AVFilterLink *outlink = filter->outputs[j];
            FilterLinkInternal *ilink = ff_link_internal(outlink);
            if (ilink->status_in != ilink->status_out) {
                MEDIA_ERR("%s src %s dst %s in %d out %d\n", __func__,
                    outlink->src->name, outlink->dst->name, ilink->status_in, ilink->status_out);
                return true;
            }
        }
    }

    return false;
}

static int media_graph_process_command(MediaGraphPriv *priv, AVFilterContext *filter, const char *cmd,
                                       const char *arg, char *res, int res_len)
{
    if (res && res_len > 0)
        return avfilter_process_command(filter, cmd, arg, res, res_len, 0);

    if (!ff_filter_graph_has_pending_status(filter->graph)) {
        av_log(NULL, AV_LOG_INFO, "process %s %s %s\n", filter->name, cmd, arg ? arg : "_");
        return avfilter_process_command(filter, cmd, arg, NULL, 0, 0);
    }

    return -EINVAL;
}

static int media_graph_handler(MediadPlugin *ctx, void *cookie, const char *target, const char *cmd,
    const char *arg, int flags, char *res, int res_len)
{
    MediaGraphPriv* priv = ctx->priv;
    int i, ret = 0;
    char* dump;

    MEDIA_INFO("cookie %p target %s cmd %s arg %s flags %d res %p res_len %d\n",
        cookie, target, cmd, arg, flags, res, res_len);

    if (!target && !strcmp(cmd, "dump")) {
        dump = avfilter_graph_dump(priv->graph, NULL);
        if (dump)
            MEDIA_INFO("\n%s\n", dump);

        av_free(dump);
        return 0;
    } else if (!strcmp(cmd, "loglevel")) {
        if (!arg)
            return -EINVAL;

        av_log_set_level(strtol(arg, NULL, 0));
        return 0;
    }

    if (!target)
        return -EINVAL;

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

MediadPlugin media_graph_plugin = {
    .name = "media_graph",
    .priv_size = sizeof(MediaGraphPriv),
    .priv = NULL,
    .init = media_graph_init,
    .get = media_graph_get_pollfds,
    .available = media_graph_poll_available,
    .run_once = media_graph_run_once,
    .uninit = media_graph_uninit,
    .process_command = media_graph_handler,
};

///////////////////////////////////////////////////////////////////
typedef struct MediaGraphTrack {
    AVFilterContext *src;
    int format;
    int samplerate;
    AVChannelLayout ch_layout;
    int64_t last_pts;
} MediaGraphTrack;

int media_graph_track_open(MediaGraphTrack **pctx, const char *stream_type,
    int format, int sample_rate, int channels,
    int (*on_event_cb)(void *udata, int evt, int64_t args), void *udata)
{
    MediaGraphPriv *priv = media_graph_plugin.priv;
    MediaGraphTrack *ctx;
    int ret;

    ctx = av_calloc(1, sizeof(*ctx));
    if (!ctx)
        return -ENOMEM;

    if (format < 0)
        ctx->format = AV_SAMPLE_FMT_S16;
    else
        ctx->format = format;
    if (sample_rate <= 0)
        ctx->samplerate = 48000;
    else
        ctx->samplerate = sample_rate;

    av_channel_layout_default(&ctx->ch_layout, channels ? channels : 2);

    ctx->src = avfilter_graph_get_filter(priv->graph, stream_type);
    if (!ctx->src) {
        MEDIA_ERR("buffersrc:%s not found\n", stream_type);
        ret = -EINVAL;
        goto fail;
    }

    ret = av_buffersrc_set_event_cb(ctx->src, on_event_cb, udata);
    if (ret < 0) {
        MEDIA_ERR("buffersrc:%s failed ret:%d\n", ctx->src->name, ret);
        goto fail;
    }

    *pctx = ctx;

    return 0;
fail:
    av_channel_layout_uninit(&ctx->ch_layout);
    av_free(ctx);
    return ret;
}

int media_graph_track_close(MediaGraphTrack **pctx)
{
    MediaGraphTrack *ctx = *pctx;
    if (!pctx || !ctx)
        return -EINVAL;

    av_buffersrc_set_event_cb(ctx->src, NULL, NULL);
    av_channel_layout_uninit(&ctx->ch_layout);
    av_free(ctx);
    *pctx = NULL;
    return 0;
}

int media_graph_track_write_frame(MediaGraphTrack *ctx, AVFrame *frame)
{
    int ret;
    ret = av_buffersrc_add_frame_flags(ctx->src, frame, AV_BUFFERSRC_FLAG_KEEP_REF);
    if (ret < 0) {
        MEDIA_ERR("buffersrc:%s failed ret:%d\n", ctx->src->name, ret);
        return ret;
    }
    if (ctx->last_pts <= 0) {
        MediaGraphPriv *priv = media_graph_plugin.priv;
        eventfd_t val = 1;
        file_write(priv->filep, &val, sizeof(val));
    }

    ctx->last_pts += frame->nb_samples;
    return 0;
}
