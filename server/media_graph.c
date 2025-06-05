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
#include <libavutil/channel_layout.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/pixfmt.h>
#include <libavutil/samplefmt.h>

#include <assert.h>
#include <fcntl.h>
#include <media_api.h>
#include <sys/eventfd.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <unistd.h>

#include "media_common.h"
#include "media_graph.h"
#include "media_plugin.h"
#include "media_server.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MAX_GRAPH_SIZE 4096
#define MAX_POLL_FILTERS 32 

#define FLAG_ARG_PRECOPIED (1 << 10)
#define FLAG_RES_PRECOPIED (1 << 11)
#define FLAG_FAST_PROC_CMD (1 << 12)

#define MAX_LINKS 10

#define ROUTE_OFF 0
#define ROUTE_ON 1

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct MediaCommand {
    AVFilterContext* filter;
    char* cmd;
    char* arg;
    char* res;
    int flags;

    TAILQ_ENTRY(MediaCommand)
    entries;
} MediaCommand;

typedef struct MediaGraphPriv {
    AVFilterGraph* graph;
    struct file* filep;
    int fd;
    void* pollfts[MAX_POLL_FILTERS];
    int pollftn;
    int* filter_states;

    TAILQ_HEAD(, MediaCommand)
    cmdq;
    pthread_mutex_t qlock;
} MediaGraphPriv;

typedef struct MediaFilterPriv {
    AVFilterContext* filter;
    void* cookie;
    bool event;
} MediaFilterPriv;

typedef struct MediaGraphAudio {
    AVFilterContext* src;
    void* link_handle;
} MediaGraphAudio;

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

static int media_graph_config_pointers(AVFilterGraph* graph)
{
    FFFilterGraph* ffgraph = fffiltergraph(graph);
    int sink_links_count = 0, n = 0;
    FilterLinkInternal** sinks;
    FilterLinkInternal* li;
    AVFilterContext* f;
    unsigned i, j;

    for (i = 0; i < graph->nb_filters; i++) {
        f = graph->filters[i];
        for (j = 0; j < f->nb_inputs; j++) {
            li = (FilterLinkInternal*)f->inputs[j];
            li->age_index = -1;
        }
        for (j = 0; j < f->nb_outputs; j++) {
            li = (FilterLinkInternal*)f->outputs[j];
            li->age_index = -1;
        }
        if (!f->nb_outputs) {
            if (f->nb_inputs > INT_MAX - sink_links_count)
                return AVERROR(EINVAL);
            sink_links_count += f->nb_inputs;
        }
    }
    sinks = av_calloc(sink_links_count, sizeof(*sinks));
    if (!sinks)
        return AVERROR(ENOMEM);
    for (i = 0; i < graph->nb_filters; i++) {
        f = graph->filters[i];
        if (!f->nb_outputs) {
            for (j = 0; j < f->nb_inputs; j++) {
                li = (FilterLinkInternal*)f->inputs[j];
                sinks[n] = li;
                sinks[n]->age_index = n;
                n++;
            }
        }
    }

    ffgraph->sink_links = sinks;
    ffgraph->sink_links_count = sink_links_count;
    return 0;
}

/* When graph init is executed, this function needs
 * to be called to set link status to eof and to 0
 * when linking.
 */
static void media_graph_set_links_status(AVFilterGraph* graph, int status)
{
    AVFilterContext* filt;
    int i, j;

    for (i = 0; i < graph->nb_filters; i++) {
        filt = graph->filters[i];

        if (filt->nb_inputs == 0) {
            for (j = 0; j < filt->nb_outputs; j++) {
                AVFilterLink* outlink = filt->outputs[j];
                FilterLinkInternal* li = (FilterLinkInternal*)outlink;
                li->status_in = status;
                li->status_out = status;
            }
        }
    }
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

    ret = media_graph_config_pointers(priv->graph);
    if (ret < 0)
        goto out;

    /* set the status of all links to AVERROR_EOF */
    media_graph_set_links_status(priv->graph, AVERROR_EOF);

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

/****************************************************************************
 * Public Functions
 ****************************************************************************/

static int media_graph_init(MediadPlugin* ctx)
{
    char* file = CONFIG_MEDIA_SERVER_CONFIG_PATH "graph.conf";
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

    priv->filter_states = av_mallocz(priv->graph->nb_filters * sizeof(int));
    if (!priv->filter_states) {
        ret = -ENOMEM;
        goto err;
    }

    TAILQ_INIT(&priv->cmdq);
    pthread_mutex_init(&priv->qlock, NULL);

    return 0;
err:
    if (priv->fd > 0) {
        close(priv->fd);
        priv->fd = -1;
    }

    return ret;
}

static MediaCommand* media_graph_create_command(const char* cmd, const char* arg, char* res, AVFilterContext* filter, int flags)
{
    MediaCommand* newcmd;

    newcmd = malloc(sizeof(MediaCommand));
    if (!newcmd)
        return NULL;

    newcmd->cmd = strdup(cmd);
    if (!newcmd->cmd) {
        free(newcmd);
        return NULL;
    }

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

    return newcmd;

err_arg:
    free(newcmd->arg);
err_cmd:
    free(newcmd->cmd);
    free(newcmd);
    return NULL;
}

static int media_graph_queue_command(MediaGraphPriv* priv, AVFilterContext* filter,
    const char* cmd, const char* arg, char* res, int res_len, int flags)
{
    MediaCommand* newcmd = NULL;
    int ret = 0;

    if (flags & FLAG_FAST_PROC_CMD) {
        if (!strcmp(cmd, "map")) {
            int old_map[MAX_LINKS] = { 0 };
            int new_map[MAX_LINKS] = { 0 };
            int i, index = 0;

            ret = av_opt_get_array(filter, "map_array", AV_OPT_SEARCH_CHILDREN, 0, filter->nb_outputs, AV_OPT_TYPE_INT, old_map);
            if (ret < 0)
                return ret;

            ret = avfilter_process_command(filter, cmd, arg, res, res_len, flags);
            if (ret < 0)
                return ret;

            ret = av_opt_get_array(filter, "map_array", AV_OPT_SEARCH_CHILDREN, 0, filter->nb_outputs, AV_OPT_TYPE_INT, new_map);
            if (ret < 0)
                return ret;

            for (i = 0; i < filter->nb_outputs; i++) {
                if (old_map[i] == ROUTE_OFF && new_map[i] == ROUTE_ON) {
                    index++;
                    break;
                }
            }

            if (index) {
                newcmd = media_graph_create_command(cmd, arg, res, filter, flags);
                if (!newcmd)
                    return -ENOMEM;

                pthread_mutex_lock(&priv->qlock);
                TAILQ_INSERT_TAIL(&priv->cmdq, newcmd, entries);
                pthread_mutex_unlock(&priv->qlock);
            }

            return ret;
        }

        return avfilter_process_command(filter, cmd, arg, res, res_len, flags);
    } else if (!strcmp(cmd, "volume")) {
        char msg[32];
        snprintf(msg, sizeof(msg), "stream_volume=%s", arg);
        return avfilter_process_command(filter, "set_parameter", msg, res, res_len, flags);
    } else if (!strcmp(cmd, "sample_rate")) {
        char msg[128];
        snprintf(msg, sizeof(msg), "%s=%s", cmd, arg);
        return avfilter_process_command(filter, "set_parameter", msg, res, res_len, flags);
    }

    newcmd = media_graph_create_command(cmd, arg, res, filter, flags);
    if (!newcmd)
        return -ENOMEM;

    pthread_mutex_lock(&priv->qlock);
    TAILQ_INSERT_TAIL(&priv->cmdq, newcmd, entries);
    pthread_mutex_unlock(&priv->qlock);

    av_log(NULL, AV_LOG_INFO, "Pending command: %s %s\n", cmd, arg ? arg : "_");
    return ret;
}

static int media_graph_calc_active_inputs(MediaCommand* cmd, int map[MAX_LINKS], int* inputs_indexs)
{
    AVFilterContext* src_filter;
    AVFilterLink* out_link;
    int ret, i, j;
    int count = 0;

    for (i = 0; i < cmd->filter->nb_inputs; i++) {
        src_filter = cmd->filter->inputs[i]->src;

        ret = av_opt_get_array(src_filter, "map_array", AV_OPT_SEARCH_CHILDREN, 0, src_filter->nb_outputs, AV_OPT_TYPE_INT, map);
        if (ret < 0)
            return ret;

        for (j = 0; j < src_filter->nb_outputs; j++) {
            out_link = src_filter->outputs[j];
            if (map[j] == ROUTE_ON && !strcmp(out_link->dst->name, cmd->filter->name))
                inputs_indexs[count++] = i;
        }
    }

    return count;
}

static void media_graph_config_links(AVFilterLink** links, int map[MAX_LINKS], int nb_links,
    int format, int sample_rate, int channels, bool playback, int* inputs_indexs)
{
    FilterLinkInternal* li;
    AVFilterLink* link;
    int i;

    for (i = 0; i < nb_links; i++) {
        link = playback ? links[i] : links[inputs_indexs[i]];
        if (playback && map[i] == ROUTE_OFF)
            continue;

        link->format = format;
        link->sample_rate = sample_rate;
        av_channel_layout_default(&link->ch_layout, channels);
        link->time_base = (AVRational) { 1, sample_rate };

        li = (FilterLinkInternal*)link;
        li->status_in = 0;
        li->status_out = 0;
    }
}

static int media_graph_format_transfer(MediaCommand* cmd)
{
    bool playback = (cmd->filter->nb_inputs == 0);
    int nb_links = playback ? cmd->filter->nb_outputs : cmd->filter->nb_inputs;
    AVFilterLink** links = playback ? cmd->filter->outputs : cmd->filter->inputs;
    int format = -1, sample_rate = 0, channels = 0;
    int inputs_indexs[MAX_LINKS] = { 0 };
    int map[MAX_LINKS] = { 0 };
    char res[128];
    int ret = 0;

    if (!cmd->arg || !strcmp(cmd->cmd, "map")) {
        ret = avfilter_process_command(cmd->filter, "get_parameter", "format", res, sizeof(res), 0);
        if (ret < 0 || sscanf(res, "fmt=%d:rate=%d:ch=%d", &format, &sample_rate, &channels) != 3)
            MEDIA_WARN("Failed to parse format: %s\n", res);
    } else {
        if (sscanf(cmd->arg, "%*p %*p fmt=%d:rate=%d:ch=%d", &format, &sample_rate, &channels) != 3)
            MEDIA_WARN("Failed to parse format: %s\n", cmd->arg);
    }

    if (format < 0 || sample_rate <= 0 || channels <= 0) {
        MEDIA_WARN("Invalid format: fmt=%d, rate=%d, ch=%d\n", format, sample_rate, channels);
        return 0;
    }

    if (playback) { // Playback
        ret = av_opt_get_array(cmd->filter, "map_array", AV_OPT_SEARCH_CHILDREN, 0, cmd->filter->nb_outputs, AV_OPT_TYPE_INT, map);
        if (ret < 0)
            return ret;
    } else { // Capture
        nb_links = media_graph_calc_active_inputs(cmd, map, inputs_indexs);
        if (nb_links < 0)
            return nb_links;
    }

    media_graph_config_links(links, map, nb_links, format, sample_rate, channels, playback, inputs_indexs);

    return ret;
}

static void media_graph_try_touch(MediaGraphPriv* priv)
{
    eventfd_t val = 1;
    file_write(priv->filep, &val, sizeof(val));
}

static int media_graph_dequeue_command(MediaGraphPriv* priv, bool process)
{
    MediaCommand* cmd;
    int i, ret = 0;

    pthread_mutex_lock(&priv->qlock);
    if (TAILQ_EMPTY(&priv->cmdq)) {
        ret = -EAGAIN;
        goto exit;
    }

    cmd = TAILQ_FIRST(&priv->cmdq);
    if (process) {
        av_log(NULL, AV_LOG_INFO, "process %s %s %s\n",
            cmd->filter->name, cmd->cmd, cmd->arg ? cmd->arg : "_");

        if (!strcmp(cmd->cmd, "link") || !strcmp(cmd->cmd, "map")) {
            for (i = 0; i < cmd->filter->nb_outputs; i++) {
                FilterLinkInternal* li = (FilterLinkInternal*)cmd->filter->outputs[i];
                if (li->status_in != AVERROR_EOF && li->status_out != AVERROR_EOF) {
                    MEDIA_WARN("%s outlink is not eof, cmd %s pending\n",
                        cmd->filter->name, cmd->cmd);

                    ret = -EAGAIN;
                    media_graph_try_touch(priv);
                    goto exit;
                }
            }
            ret = media_graph_format_transfer(cmd);
            if (ret < 0) {
                MEDIA_ERR("media graph link error ret:%d:%s\n", ret, av_err2str(ret));
                goto exit;
            }
        }

        if (!(cmd->flags & FLAG_FAST_PROC_CMD))
            ret = avfilter_process_command(cmd->filter, cmd->cmd, cmd->arg,
                cmd->res, 0, cmd->flags);
    }

    TAILQ_REMOVE(&priv->cmdq, cmd, entries);
    pthread_mutex_unlock(&priv->qlock);

    free(cmd->cmd);

    if (!(cmd->flags & FLAG_ARG_PRECOPIED) && cmd->arg)
        free(cmd->arg);

    if (!(cmd->flags & FLAG_RES_PRECOPIED) && cmd->res)
        free(cmd->res);

    free(cmd);

    return ret;

exit:
    pthread_mutex_unlock(&priv->qlock);
    return ret;
}

static int media_graph_uninit(MediadPlugin* ctx)
{
    MediaGraphPriv* priv = ctx->priv;
    int ret;

    do {
        ret = media_graph_dequeue_command(priv, false);
    } while (ret >= 0);

    avfilter_graph_free(&priv->graph);
    av_freep(&priv->filter_states);

    return 0;
}

static int media_graph_get_pollfds(MediadPlugin* ctx, struct pollfd* fds,
    void** cookies, int count)
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

static int media_graph_poll_available(MediadPlugin* ctx, struct pollfd* fd, void* cookie)
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

static int media_graph_run_once(MediadPlugin* ctx)
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

static int media_graph_dump_link(AVBPrint* buf, AVFilterLink* link)
{
    const char* format;
    AVBPrint dummy_buffer;
    FilterLinkInternal* li = ff_link_internal(link);

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
        av_bprintf(buf, "[%dHz %s: fifo:%d wt:%d icnt:%" PRId64 " ocnt:%" PRId64 " ",
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

static void media_graph_dump_to_buf(AVBPrint* buf, AVFilterGraph* graph)
{
    unsigned i, j, x, e;

    for (i = 0; i < graph->nb_filters; i++) {
        AVFilterContext* filter = graph->filters[i];
        unsigned max_src_name = 0, max_dst_name = 0;
        unsigned max_in_name = 0, max_out_name = 0;
        unsigned max_in_fmt = 0, max_out_fmt = 0;
        unsigned width, height, in_indent;
        unsigned lname = strlen(filter->name);
        unsigned ltype = strlen(filter->filter->name);

        for (j = 0; j < filter->nb_inputs; j++) {
            AVFilterLink* l = filter->inputs[j];
            unsigned ln = strlen(l->src->name) + 1 + strlen(l->srcpad->name);
            max_src_name = FFMAX(max_src_name, ln);
            max_in_name = FFMAX(max_in_name, strlen(l->dstpad->name));
            max_in_fmt = FFMAX(max_in_fmt, media_graph_dump_link(NULL, l));
        }
        for (j = 0; j < filter->nb_outputs; j++) {
            AVFilterLink* l = filter->outputs[j];
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
                AVFilterLink* l = filter->inputs[in_no];
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
                AVFilterLink* l = filter->outputs[out_no];
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

static char* media_graph_server_dump(AVFilterGraph* graph, const char* options)
{
    AVBPrint buf;
    char* dump;

    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_COUNT_ONLY);
    media_graph_dump_to_buf(&buf, graph);
    dump = av_malloc(buf.len + 1);
    if (!dump)
        return NULL;
    av_bprint_init_for_buffer(&buf, dump, buf.len + 1);
    media_graph_dump_to_buf(&buf, graph);
    return dump;
}

static int media_graph_handler(MediadPlugin* ctx, struct media_server_conn* conn, const char* target, const char* cmd,
    const char* arg, int flags, char* res, int res_len)
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
    } else if (!strcmp(cmd, "map"))
        flags |= FLAG_FAST_PROC_CMD;

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

int media_graph_audio_open(MediaGraphAudio** pctx, const char* stream)
{
    MediaGraphPriv* priv = media_graph_plugin.priv;
    AVFilterGraph* graph = priv->graph;
    char stream_name[64] = { 0 };
    MediaGraphAudio* ctx;
    int ret;

    ctx = av_calloc(1, sizeof(*ctx));
    if (!ctx)
        return -ENOMEM;

    ret = media_stub_get_stream_name(stream, stream_name, sizeof(stream_name));
    if (ret >= 0)
        stream = stream_name;

    pthread_mutex_lock(&priv->qlock);
    for (int i = 0; i < graph->nb_filters; i++) {
        if (graph->filters[i]->name && !priv->filter_states[i] && !strncmp(stream, graph->filters[i]->name, strlen(stream))) {
            priv->filter_states[i] = 1;
            ctx->src = graph->filters[i];
            break;
        }
    }
    pthread_mutex_unlock(&priv->qlock);

    if (!ctx->src) {
        MEDIA_ERR("%s stream is not found\n", ret >= 0 ? stream_name : stream);
        ret = -EINVAL;
        goto fail;
    }

    *pctx = ctx;
    return 0;

fail:
    av_free(ctx);
    return ret;
}

int media_graph_audio_start(MediaGraphAudio** pctx, int format, int sample_rate, int channels,
    int (*on_event_cb)(void* udata, int evt, int64_t args), void* udata)
{
    MediaGraphPriv* priv = media_graph_plugin.priv;
    MediaGraphAudio* ctx = *pctx;
    char msg[128] = { 0 };
    int ret;

    if (!pctx || !ctx || !ctx->src)
        return -EINVAL;

    snprintf(msg, sizeof(msg), "%p %p fmt=%d:rate=%d:ch=%d", on_event_cb,
        udata, format, sample_rate, channels);
    ret = media_graph_queue_command(priv, ctx->src, "link", msg, (char*)&ctx->link_handle, sizeof(void**), FLAG_RES_PRECOPIED);
    if (ret < 0) {
        MEDIA_ERR("%s link failed ret:%d\n", ctx->src->name, ret);
        return ret;
    }

    media_graph_try_touch(priv);
    return 0;
}

int media_graph_audio_stop(MediaGraphAudio** pctx)
{
    MediaGraphPriv* priv = media_graph_plugin.priv;
    MediaGraphAudio* ctx = *pctx;
    int ret;

    if (!pctx || !ctx || !ctx->src)
        return -EINVAL;

    ret = media_graph_queue_command(priv, ctx->src, "unlink", (char*)ctx->link_handle, NULL, 0, FLAG_ARG_PRECOPIED);
    if (ret < 0)
        MEDIA_ERR("unlink %s failed: %d\n", ctx->src->name, ret);

    media_graph_try_touch(priv);
    return ret;
}

int media_graph_audio_close(MediaGraphAudio** pctx)
{
    MediaGraphPriv* priv = media_graph_plugin.priv;
    MediaGraphAudio* ctx = *pctx;

    if (!pctx || !ctx)
        return -EINVAL;

    pthread_mutex_lock(&priv->qlock);
    for (int i = 0; i < priv->graph->nb_filters; i++) {
        AVFilterContext* filter = priv->graph->filters[i];
        if (filter == ctx->src) {
            priv->filter_states[i] = 0;
            break;
        }
    }
    pthread_mutex_unlock(&priv->qlock);

    av_free(ctx);
    *pctx = NULL;
    return 0;
}

int media_graph_audio_set_parameter(MediaGraphAudio** pctx, const char* param, const char* value)
{
    MediaGraphPriv* priv = media_graph_plugin.priv;
    MediaGraphAudio* ctx = *pctx;
    char msg[128];
    int ret;

    if (!ctx || !param || !value)
        return -EINVAL;

    snprintf(msg, sizeof(msg), "%s=%s", param, value);

    ret = media_graph_queue_command(priv, ctx->src, "set_parameter", msg, NULL, 0, 0);
    if (ret < 0)
        MEDIA_ERR("%s set_parameter failed ret:%d\n", ctx->src->name, ret);

    media_graph_try_touch(priv);
    return ret;
}

int media_graph_audio_get_parameter(MediaGraphAudio** pctx, const char* key, char* res, int res_len)
{
    MediaGraphPriv* priv = media_graph_plugin.priv;
    MediaGraphAudio* ctx = *pctx;
    char msg[128];
    int ret;

    if (!ctx || !key || !res || res_len <= 0)
        return -EINVAL;

    snprintf(msg, sizeof(msg), "%s", key);

    ret = media_graph_queue_command(priv, ctx->src, "get_parameter", msg, res, res_len, FLAG_RES_PRECOPIED | FLAG_FAST_PROC_CMD);
    if (ret < 0)
        MEDIA_ERR("%s get_parameter failed ret:%d\n", ctx->src->name, ret);

    return ret;
}
