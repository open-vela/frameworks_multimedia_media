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
#include <libavfilter/avfilter_internal.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/filters.h>
#include <libavfilter/formats.h>
#include <libavutil/bprint.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libavutil/pixfmt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/samplefmt.h>
#include <libavutil/channel_layout.h>

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
#include "media_graph.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MAX_GRAPH_SIZE 4096
#define MAX_POLL_FILTERS 32
#define FLAG_ARG_PRECOPIED (1 << 10)
#define FLAG_RES_PRECOPIED (1 << 11)
#define FLAG_FAST_PROC_CMD (1 << 12)

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct MediaCommand {
    AVFilterContext           *filter;
    char                      *cmd;
    char                      *arg;
    char                      *res;
    int                       flags;

    TAILQ_ENTRY(MediaCommand) entries;
} MediaCommand;

typedef struct MediaGraphPriv {
    AVFilterGraph* graph;
    int fd;
    pid_t tid;
    void* pollfts[MAX_POLL_FILTERS];
    int pollftn;

    TAILQ_HEAD(, MediaCommand) cmdq;
    pthread_mutex_t qlock;
} MediaGraphPriv;

typedef struct MediaFilterPriv {
    AVFilterContext* filter;
    void* cookie;
    bool event;
} MediaFilterPriv;

typedef struct MediaGraphStream {
    AVFilterContext *src;
    void *link_handle;
} MediaGraphStream;

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
    if (level > av_log_get_level())
        return;

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

    MEDIA_INFO("%s, loadgraph from file: %s\n", __func__, conf);

    fd = open(conf, O_RDONLY | O_BINARY | O_CLOEXEC);
    if (fd < 0) {
        MEDIA_ERR("%s, can't open media graph file\n", __func__);
        return -errno;
    }

    ret = read(fd, graph_desc, MAX_GRAPH_SIZE);
    close(fd);
    if (ret < 0)
        return -errno;
    else if (ret == MAX_GRAPH_SIZE)
        return -EFBIG;

    graph_desc[ret] = 0;

    MEDIA_INFO("%s, graph_desc:\n%s\n", __func__, graph_desc);

    priv->graph = avfilter_graph_alloc();
    if (!priv->graph)
        return -ENOMEM;

    ret = avfilter_graph_parse2(priv->graph, graph_desc, &input, &output);
    if (ret < 0) {
        MEDIA_ERR("%s, media graph parse error\n", __func__);
        goto out;
    }

    avfilter_inout_free(&input);
    avfilter_inout_free(&output);

    avfilter_graph_set_auto_convert(priv->graph, AVFILTER_AUTO_CONVERT_NONE);

    ret = avfilter_graph_config(priv->graph, NULL);
    if (ret < 0) {
        MEDIA_ERR("%s, media graph config error\n", __func__);
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
                MEDIA_ERR("%s, media graph too many pollfds\n", __func__);
                goto out;
            }
        }
    }

    MEDIA_INFO("%s, loadgraph succeed\n", __func__);
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

    TAILQ_INIT(&priv->cmdq);
    pthread_mutex_init(&priv->qlock, NULL);

    return 0;
err:
    if (priv->fd > 0)
        close(priv->fd);

    return ret;
}

static int media_graph_queue_command(MediaGraphPriv* priv, AVFilterContext* filter,
    const char* cmd, const char* arg, char* res, int res_len, int flags)
{
    MediaCommand *newcmd;

    if (flags & FLAG_FAST_PROC_CMD)
        return avfilter_process_command(filter, cmd, arg, res, res_len, flags);

    newcmd = malloc(sizeof(MediaCommand));
    if (!newcmd)
        return -ENOMEM;

    newcmd->cmd = strdup(cmd);
    if (!newcmd->cmd)
        goto err;

    if (arg) {
        if (flags & FLAG_ARG_PRECOPIED)
            newcmd->arg = (char*)arg;
        else {
            newcmd->arg = strdup(arg);
            if (!newcmd->arg)
                goto err_cmd;
        }
    } else
        newcmd->arg = NULL;

    if (res) {
        if (flags & FLAG_RES_PRECOPIED)
            newcmd->res = res;
        else {
            newcmd->res = strdup(res);
            if (!newcmd->res)
                goto err_arg;
        }
    } else
        newcmd->res = NULL;

    newcmd->filter = filter;

    newcmd->flags = flags;

    pthread_mutex_lock(&priv->qlock);
    TAILQ_INSERT_TAIL(&priv->cmdq, newcmd, entries);
    pthread_mutex_unlock(&priv->qlock);
    av_log(NULL, AV_LOG_INFO, "pending %s %s %s\n",
        filter->name, cmd, arg ? arg : "_");
    return 0;

err_arg:
    free(newcmd->arg);
err_cmd:
    free(newcmd->cmd);
err:
    free(newcmd);
    return -ENOMEM;
}

static int media_graph_dequeue_command(MediaGraphPriv* priv, bool process)
{
    MediaCommand* cmd;
    int ret = 0;

    //TODO We should check if the graph has pending status by inlink is_activate
    // if (media_graph_has_pending_status(priv->graph))
    //     return ret;

    pthread_mutex_lock(&priv->qlock);
    if (TAILQ_EMPTY(&priv->cmdq)) {
        pthread_mutex_unlock(&priv->qlock);
        return -EAGAIN;
    }

    cmd = TAILQ_FIRST(&priv->cmdq);
    TAILQ_REMOVE(&priv->cmdq, cmd, entries);
    pthread_mutex_unlock(&priv->qlock);

    if (process) {
            av_log(NULL, AV_LOG_INFO, "process %s %s %s\n",
                cmd->filter->name, cmd->cmd, cmd->arg ? cmd->arg : "_");
            ret = avfilter_process_command(cmd->filter, cmd->cmd, cmd->arg,
                cmd->res, 0, cmd->flags);
    }

    free(cmd->cmd);

    if (!(cmd->flags & FLAG_ARG_PRECOPIED) && cmd->arg)
        free(cmd->arg);

    if (!(cmd->flags & FLAG_RES_PRECOPIED) && cmd->res)
        free(cmd->res);

    free(cmd);

    return ret;
}

static int media_graph_uninit(MediadPlugin *ctx)
{
    MediaGraphPriv* priv = ctx->priv;
    int ret;

    do {
       ret = media_graph_dequeue_command(priv, false);
    } while(ret >= 0);

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

        ret = media_graph_queue_command(priv, filter, "get_pollfd", NULL,
            (char*)&fds[nfd], sizeof(struct pollfd) * (count - nfd),
            AV_OPT_SEARCH_CHILDREN | FLAG_FAST_PROC_CMD);
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
        media_graph_queue_command(priv, cookie, "poll_available", NULL,
            (char*)fd, sizeof(struct pollfd),
            AV_OPT_SEARCH_CHILDREN | FLAG_FAST_PROC_CMD);
    else
        eventfd_read(priv->fd, &unuse);

    return 0;
}

static int media_graph_run_once(MediadPlugin *ctx)
{
    MediaGraphPriv* priv = ctx->priv;
    int ret;

    do {
        ret = media_graph_dequeue_command(priv, true);
    } while (ret >= 0);

    while (1) {
        ret = ff_filter_graph_run_once(priv->graph);
        if (ret < 0)
            break;
    }

    if (ret < 0) {
        if (ret == AVERROR(EAGAIN))
            return 0;
        MEDIA_ERR("media graph run error ret:%d:%s\n", ret, av_err2str(ret));
    }

    return 0;
}

static int media_graph_dump_link(AVBPrint *buf, AVFilterLink *link)
{
    const char *format;
    AVBPrint dummy_buffer;
    FilterLinkInternal *li = ff_link_internal(link);

    if (!buf) {
        buf = &dummy_buffer;
        av_bprint_init(buf, 0, AV_BPRINT_SIZE_COUNT_ONLY);
    }
    switch (link->type) {
    case AVMEDIA_TYPE_VIDEO:
        format = av_x_if_null(av_get_pix_fmt_name(link->format), "?");
        av_bprintf(buf, "[%dx%d %d:%d %s]", link->w, link->h,
            link->sample_aspect_ratio.num,
            link->sample_aspect_ratio.den,
            format);
        break;

    case AVMEDIA_TYPE_AUDIO:
        format = av_x_if_null(av_get_sample_fmt_name(link->format), "?");
        av_bprintf(buf, "[%dHz %s: fifo:%d wt:%d icnt:%"PRId64" ocnt:%"PRId64" ",
            (int)link->sample_rate, format, (int)ff_framequeue_queued_frames(&li->fifo),
            li->frame_wanted_out, li->l.frame_count_in, li->l.frame_count_out);
        av_channel_layout_describe_bprint(&link->ch_layout, buf);
        av_bprint_chars(buf, ']', 1);
        break;

    default:
        av_bprintf(buf, "?");
        break;
    }
    return buf->len;
}

static void media_graph_dump_to_buf(AVBPrint *buf, AVFilterGraph *graph)
{
    unsigned i, j, x, e;

    for (i = 0; i < graph->nb_filters; i++) {
        AVFilterContext *filter = graph->filters[i];
        unsigned max_src_name = 0, max_dst_name = 0;
        unsigned max_in_name = 0, max_out_name = 0;
        unsigned max_in_fmt = 0, max_out_fmt = 0;
        unsigned width, height, in_indent;
        unsigned lname = strlen(filter->name);
        unsigned ltype = strlen(filter->filter->name);

        for (j = 0; j < filter->nb_inputs; j++) {
            AVFilterLink *l = filter->inputs[j];
            unsigned ln = strlen(l->src->name) + 1 + strlen(l->srcpad->name);
            max_src_name = FFMAX(max_src_name, ln);
            max_in_name = FFMAX(max_in_name, strlen(l->dstpad->name));
            max_in_fmt = FFMAX(max_in_fmt, media_graph_dump_link(NULL, l));
        }
        for (j = 0; j < filter->nb_outputs; j++) {
            AVFilterLink *l = filter->outputs[j];
            unsigned ln = strlen(l->dst->name) + 1 + strlen(l->dstpad->name);
            max_dst_name = FFMAX(max_dst_name, ln);
            max_out_name = FFMAX(max_out_name, strlen(l->srcpad->name));
            max_out_fmt = FFMAX(max_out_fmt, media_graph_dump_link(NULL, l));
        }
        in_indent = max_src_name + max_in_name + max_in_fmt;
        in_indent += in_indent ? 4 : 0;
        width = FFMAX(lname + 2, ltype + 4);
        height = FFMAX3(2, filter->nb_inputs, filter->nb_outputs);
        av_bprint_chars(buf, ' ', in_indent);
        av_bprintf(buf, "+");
        av_bprint_chars(buf, '-', width);
        av_bprintf(buf, "+\n");
        for (j = 0; j < height; j++) {
            unsigned in_no = j - (height - filter->nb_inputs) / 2;
            unsigned out_no = j - (height - filter->nb_outputs) / 2;

            /* Input link */
            if (in_no < filter->nb_inputs) {
                AVFilterLink *l = filter->inputs[in_no];
                e = buf->len + max_src_name + 2;
                av_bprintf(buf, "%s:%s", l->src->name, l->srcpad->name);
                av_bprint_chars(buf, '-', e - buf->len);
                e = buf->len + max_in_fmt + 2 + max_in_name - strlen(l->dstpad->name);
                media_graph_dump_link(buf, l);
                av_bprint_chars(buf, '-', e - buf->len);
                av_bprintf(buf, "%s", l->dstpad->name);
            } else {
                av_bprint_chars(buf, ' ', in_indent);
            }

            /* Filter */
            av_bprintf(buf, "|");
            if (j == (height - 2) / 2) {
                x = (width - lname) / 2;
                av_bprintf(buf, "%*s%-*s", x, "", width - x, filter->name);
            } else if (j == (height - 2) / 2 + 1) {
                x = (width - ltype - 2) / 2;
                av_bprintf(buf, "%*s(%s)%*s", x, "", filter->filter->name,
                    width - ltype - 2 - x, "");
            } else {
                av_bprint_chars(buf, ' ', width);
            }
            av_bprintf(buf, "|");

            /* Output link */
            if (out_no < filter->nb_outputs) {
                AVFilterLink *l = filter->outputs[out_no];
                unsigned ln = strlen(l->dst->name) + 1 + strlen(l->dstpad->name);
                e = buf->len + max_out_name + 2;
                av_bprintf(buf, "%s", l->srcpad->name);
                av_bprint_chars(buf, '-', e - buf->len);
                e = buf->len + max_out_fmt + 2 + max_dst_name - ln;
                media_graph_dump_link(buf, l);
                av_bprint_chars(buf, '-', e - buf->len);
                av_bprintf(buf, "%s:%s", l->dst->name, l->dstpad->name);
            }
            av_bprintf(buf, "\n");
        }
        av_bprint_chars(buf, ' ', in_indent);
        av_bprintf(buf, "+");
        av_bprint_chars(buf, '-', width);
        av_bprintf(buf, "+\n");
        av_bprintf(buf, "\n");
    }
}

static char *media_graph_server_dump(AVFilterGraph *graph, const char *options)
{
    AVBPrint buf;
    char *dump;

    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_COUNT_ONLY);
    media_graph_dump_to_buf(&buf, graph);
    dump = av_malloc(buf.len + 1);
    if (!dump)
        return NULL;
    av_bprint_init_for_buffer(&buf, dump, buf.len + 1);
    media_graph_dump_to_buf(&buf, graph);
    return dump;
}

static int media_graph_handler(MediadPlugin *ctx, struct media_server_conn *conn, const char *target, const char *cmd,
    const char *arg, int flags, char *res, int res_len)
{
    MediaGraphPriv* priv = ctx->priv;
    int i, ret = 0;
    char* dump;

    MEDIA_INFO("cookie %p target %s cmd %s arg %s flags %d res %p res_len %d\n",
        conn, target, cmd, arg, flags, res, res_len);

    if (!target && !strcmp(cmd, "dump")) {
        dump = media_graph_server_dump(priv->graph, NULL);
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

    if (res && res_len > 0)
        flags |= FLAG_FAST_PROC_CMD;

    for (i = 0; i < priv->graph->nb_filters; i++) {
        AVFilterContext* filter = priv->graph->filters[i];

        if (!strcmp(target, filter->name)) {
            ret = media_graph_queue_command(priv, filter, cmd, arg, res, res_len, flags);
        } else {
            const char* tmp = strchr(filter->name, '@');

            if (tmp && !strncmp(tmp + 1, target, strlen(target)))
                ret = media_graph_queue_command(priv, filter, cmd, arg, res, res_len, flags);
        }

        if (ret < 0)
            return ret;
    }

    return 0;
}

static void media_graph_try_touch(MediaGraphPriv *priv)
{
    if (priv->tid != gettid()) {
        eventfd_t val = 1;
        file_write(priv->filep, &val, sizeof(val));
    }
}

static int media_graph_stream_unlink(MediaGraphPriv *priv, AVFilterContext *ctx, AVFilterLink *link)
{
    AVFilterLink *cur_link;
    int ret, i;

    if (!ctx || !link)
        return AVERROR(EINVAL);

    if (!ctx->nb_inputs) {
        ret = media_graph_queue_command(priv, ctx, "unlink", (char*)link, NULL, 0, FLAG_ARG_PRECOPIED);
        if (ret < 0)
            return ret;

        MEDIA_INFO("unlink filter %s success.", ctx->name);
        return 0;
    }

    for (i = 0; i < ctx->nb_inputs; i++) {
        cur_link = ctx->inputs[i];
        if (cur_link && avfilter_link_is_active(cur_link)) {
            ret = media_graph_stream_unlink(priv, cur_link->src, cur_link);
            if (ret < 0) {
                MEDIA_ERR("unlink failed for filter %s: %d.", ctx->name, ret);
                return ret;
            }
        }
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

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int media_graph_stream_open(MediaGraphStream** pctx,
                            const char* stream,
                            int format, int sample_rate, int channels,
                            int (*on_event_cb)(void* udata, int evt, int64_t args), void* udata)
{
    MediaGraphPriv *priv = media_graph_plugin.priv;
    AVChannelLayout ch_layout = {0};
    char layout_str[32] = {0};
    MediaGraphStream *ctx;
    char stream_name[64] = { 0 };
    char msg[128] = { 0 };
    int ret;

    ctx = av_calloc(1, sizeof(*ctx));
    if (!ctx)
        return -ENOMEM;

    ret = media_stub_get_stream_name(stream, stream_name, sizeof(stream_name));
    if (ret >= 0)
        stream = stream_name;

    ctx->src = avfilter_graph_get_filter(priv->graph, stream);
    if (!ctx->src) {
        MEDIA_ERR("%s stream is not found\n", ret >= 0 ? stream_name : stream);
        ret = -EINVAL;
        goto fail;
    }

    snprintf(msg, sizeof(msg), "%p %p", on_event_cb, udata);
    ret = media_graph_queue_command(priv, ctx->src, "link", msg, (char *)&ctx->link_handle, sizeof(void **), FLAG_RES_PRECOPIED);
    if (ret < 0) {
        MEDIA_ERR("%s link failed ret:%d\n", ctx->src->name, ret);
        goto fail;
    }

    av_channel_layout_default(&ch_layout, channels);
    ret = av_channel_layout_describe(&ch_layout, layout_str, sizeof(layout_str));
    if (ret < 0)
        MEDIA_ERR("Failed to describe channel layout: %s\n", av_err2str(ret));

    snprintf(msg, sizeof(msg), "format=%d:sample_rate=%d:ch_layout=%s",
            format, sample_rate, layout_str);
    ret = media_graph_queue_command(priv, ctx->src, "set_options", msg, NULL, 0, 0);
    if (ret < 0)
        MEDIA_ERR("%s set_options failed ret:%d\n", ctx->src->name, ret);

    media_graph_try_touch(priv);
    *pctx = ctx;
    return 0;
fail:
    av_free(ctx);
    return ret;
}

int media_graph_stream_close(MediaGraphStream** pctx)
{
    MediaGraphPriv *priv = media_graph_plugin.priv;
    MediaGraphStream *ctx = *pctx;
    int ret;

    if (!pctx || !ctx || !ctx->src)
        return -EINVAL;

    ret = media_graph_queue_command(priv, ctx->src, "unlink", (char *)ctx->link_handle, NULL, 0, FLAG_ARG_PRECOPIED);

    if (ctx->src->nb_inputs)
        /* unlink and trigger pcmxc send empty frame flush record pipe*/
        ret = media_graph_stream_unlink(priv, ctx->src, ctx->src->inputs[0]);

    if (ret < 0)
        MEDIA_ERR("unlink %s failed: %d\n", ctx->src->name, ret);

    media_graph_try_touch(priv);
    av_free(ctx);
    *pctx = NULL;
    return ret;
}

int media_graph_stream_set_parameter(MediaGraphStream** pctx, const char* param, const char* value)
{
    MediaGraphPriv *priv = media_graph_plugin.priv;
    MediaGraphStream *ctx = *pctx;
    char msg[128];
    int ret;

    if (!ctx || !param || !value)
        return -EINVAL;

    snprintf(msg, sizeof(msg), "%p %s %s", ctx->link_handle, param, value);

    ret = media_graph_queue_command(priv, ctx->src, "set_parameter", msg, NULL, 0, 0);
    if (ret < 0)
        MEDIA_ERR("%s set_parameter failed ret:%d\n", ctx->src->name, ret);

    return ret;
}

int media_graph_stream_get_parameter(MediaGraphStream** pctx, const char* key, char *res, int res_len)
{
    MediaGraphPriv *priv = media_graph_plugin.priv;
    MediaGraphStream *ctx = *pctx;
    char msg[128];
    int ret;

    if (!ctx || !key || !res || res_len <= 0)
        return -EINVAL;

    snprintf(msg, sizeof(msg), "%p %s", ctx->link_handle, key);

    ret = media_graph_queue_command(priv, ctx->src, "get_parameter", msg, res, res_len, FLAG_RES_PRECOPIED | FLAG_FAST_PROC_CMD);
    if (ret < 0)
        MEDIA_ERR("%s get_parameter failed ret:%d\n", ctx->src->name, ret);

    return ret;
}
