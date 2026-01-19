/****************************************************************************
 * frameworks/media/server/audio_graph.c
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
#include <libavutil/bprint.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>

#include <fcntl.h>
#include <sys/eventfd.h>
#include <sys/queue.h>

#include "audio_graph.h"
#include "audio_negotiation.h"
#include "media_common.h"
#include "media_plugin.h"
#include "media_server.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MAX_GRAPH_SIZE CONFIG_MEDIA_GRAPH_SIZE_MAX
#define MAX_POLL_FILTERS 32
#define MAX_LINKS 10

#define ROUTE_OFF 0
#define ROUTE_ON 1

#define SUBGRAPH_PARSER_FLAGS "<subgraph>"

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct MediaCommand {
    AVFilterContext* filter;
    char* cmd;
    char* arg;
    char* res;

    TAILQ_ENTRY(MediaCommand)
    entries;
} MediaCommand;

typedef struct MediaGraphPriv {
    AVFilterGraph* graph;
    int fd;
    void* pollfts[MAX_POLL_FILTERS];
    int pollftn;
    int* occupied;

    TAILQ_HEAD(, MediaCommand)
    cmdq;
    pthread_mutex_t qlock;
} MediaGraphPriv;

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

static void audio_graph_log_callback(void* avcl, int level,
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

static int audio_graph_config_pointers(AVFilterGraph* graph)
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
static void audio_graph_set_links_status(AVFilterGraph* graph, int status)
{
    AVFilterContext* filt;
    int i, j;

    for (i = 0; i < graph->nb_filters; i++) {
        filt = graph->filters[i];

        for (j = 0; j < filt->nb_outputs; j++) {
            if (filt->outputs[j]) {
                FilterLinkInternal* li = (FilterLinkInternal*)filt->outputs[j];
                li->status_in = status;
                li->status_out = status;
            }
        }

        for (j = 0; j < filt->nb_inputs; j++) {
            if (filt->inputs[j]) {
                FilterLinkInternal* li = (FilterLinkInternal*)filt->inputs[j];
                li->status_in = status;
                li->status_out = status;
            }
        }
    }
}

static int audio_graph_load(MediaGraphPriv* priv, char* conf)
{
    char graph_desc[MAX_GRAPH_SIZE];
    AVFilterInOut* input = NULL;
    AVFilterInOut* output = NULL;
    char* subgraph_start;
    int ret;
    int fd;

    av_log_set_callback(audio_graph_log_callback);
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
    subgraph_start = strstr(graph_desc, SUBGRAPH_PARSER_FLAGS);
    if (subgraph_start)
        *subgraph_start = '\0';

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

    ret = audio_graph_config_pointers(priv->graph);
    if (ret < 0)
        goto out;

    /* set the status of all links to AVERROR_EOF */
    audio_graph_set_links_status(priv->graph, AVERROR_EOF);

    priv->graph->opaque = priv;

    if (subgraph_start) {
        char* current = subgraph_start + strlen(SUBGRAPH_PARSER_FLAGS);
        while (current) {
            char *colon, *config_end, *subgraph_name;
            int found_subgraph = 0;
            char filter_name[64];

            // Skip whitespace
            while (*current == ' ' || *current == '\n' || *current == '\r')
                current++;

            colon = strchr(current, ':');
            if (!colon) {
                MEDIA_ERR("Error in parsing subgraph filter name.\n");
                ret = -EINVAL;
                goto out;
            }

            *colon = '\0';
            subgraph_name = current;
            current = colon + 1;

            config_end = strstr(current, SUBGRAPH_PARSER_FLAGS);
            if (config_end)
                *config_end = '\0';

            MEDIA_INFO("Found subgraph %s with config: %s\n", subgraph_name, current);

            snprintf(filter_name, sizeof(filter_name), "asubgraph@%s", subgraph_name);
            for (int i = 0; i < priv->graph->nb_filters; i++) {
                AVFilterContext* filter = priv->graph->filters[i];
                if (filter->name && !strcmp(filter->name, filter_name)) {
                    ret = avfilter_process_command(filter, "graph_parse", current, NULL, 0, 0);
                    if (ret < 0) {
                        MEDIA_ERR("Failed to parse subgraph config for %s:%s, %s\n",
                            filter_name, current, av_err2str(ret));
                        goto out;
                    }

                    found_subgraph = 1;
                    MEDIA_INFO("Successfully parsed subgraph config for %s:%s\n",
                        filter_name, current);
                    break;
                }
            }

            if (!found_subgraph) {
                MEDIA_ERR("Failed to find subgraph filter %s\n", filter_name);
                ret = -EINVAL;
                goto out;
            }

            if (!config_end)
                break;
            current = config_end + strlen(SUBGRAPH_PARSER_FLAGS);
        }
    }

    priv->pollftn = 0;
    for (fd = 0; fd < priv->graph->nb_filters; fd++) {
        AVFilterContext* filter = priv->graph->filters[fd];

        if ((filter->filter->flags & AVFILTER_FLAG_SUPPORT_POLL) != 0) {
            priv->pollfts[priv->pollftn++] = filter;
            if (priv->pollftn > MAX_POLL_FILTERS) {
                MEDIA_ERR("%s, media graph too many pollfds\n", __func__);
                ret = -EINVAL;
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

static int audio_graph_init(MediadPlugin* ctx)
{
    char* file = CONFIG_MEDIA_SERVER_CONFIG_PATH "graph.conf";
    MediaGraphPriv* priv = ctx->priv;
    int ret;

    priv->fd = eventfd(0, EFD_CLOEXEC);
    if (priv->fd < 0) {
        ret = -errno;
        goto err;
    }

    ret = audio_graph_load(priv, file);
    if (ret < 0)
        goto err;

    priv->occupied = av_mallocz(priv->graph->nb_filters * sizeof(int));
    if (!priv->occupied) {
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

static inline bool audio_graph_immediate_cmd(const char* cmd)
{
    return strcmp(cmd, "link")
        && strcmp(cmd, "unlink")
        && strcmp(cmd, "map")
        && strcmp(cmd, "pause")
        && strcmp(cmd, "resume");
}

static MediaCommand* audio_graph_create_command(const char* cmd, const char* arg, char* res, AVFilterContext* filter)
{
    MediaCommand* newcmd = NULL;
    char* cmd_dup = NULL;
    char* arg_dup = NULL;

    cmd_dup = strdup(cmd);
    if (!cmd_dup)
        return NULL;

    if (arg) {
        arg_dup = strdup(arg);
        if (!arg_dup) {
            free(cmd_dup);
            return NULL;
        }
    }

    newcmd = malloc(sizeof(MediaCommand));
    if (!newcmd) {
        free(cmd_dup);
        free(arg_dup);
        return NULL;
    }

    newcmd->cmd = cmd_dup;
    newcmd->arg = arg_dup;
    newcmd->res = res;
    newcmd->filter = filter;

    return newcmd;
}

static void audio_graph_try_touch(MediaGraphPriv* priv)
{
    eventfd_t val = 1;
    write(priv->fd, &val, sizeof(val));
}

static int audio_graph_queue_command(MediaGraphPriv* priv, AVFilterContext* filter,
    const char* cmd, const char* arg, char* res, int res_len, int flags)
{
    MediaCommand* newcmd = NULL;
    char msg[128];
    int ret = 0;

    if (audio_graph_immediate_cmd(cmd)) {
        if (!strcmp(cmd, "volume")) {
            snprintf(msg, sizeof(msg), "volume=%.*s", (int)(sizeof(msg) - 8), arg ? arg : "");
            return avfilter_process_command(filter, "set_parameter", msg, res, res_len, 0);
        }

        return avfilter_process_command(filter, cmd, arg, res, res_len, flags);
    }

    newcmd = audio_graph_create_command(cmd, arg, res, filter);
    if (!newcmd)
        return -ENOMEM;

    pthread_mutex_lock(&priv->qlock);
    TAILQ_INSERT_TAIL(&priv->cmdq, newcmd, entries);
    pthread_mutex_unlock(&priv->qlock);

    audio_graph_try_touch(priv);

    av_log(NULL, AV_LOG_INFO, "Pending command: %s %s\n", cmd, arg ? arg : "_");
    return ret;
}

static int audio_graph_dequeue_command(MediaGraphPriv* priv, bool process)
{
    MediaCommand* cmd = NULL;
    int i, j, ret = 0;

    pthread_mutex_lock(&priv->qlock);
    cmd = TAILQ_FIRST(&priv->cmdq);
    if (!cmd) {
        pthread_mutex_unlock(&priv->qlock);
        return -EAGAIN;
    }

    if (!process) {
        TAILQ_REMOVE(&priv->cmdq, cmd, entries);
        pthread_mutex_unlock(&priv->qlock);
        goto exit;
    }
    pthread_mutex_unlock(&priv->qlock);

    av_log(NULL, AV_LOG_INFO, "process %s %s %s\n",
        cmd->filter->name, cmd->cmd, cmd->arg ? cmd->arg : "_");

    if (!strcmp(cmd->cmd, "link") || !strcmp(cmd->cmd, "map")) {
        for (i = 0; i < cmd->filter->nb_outputs; i++) {
            AVFilterLink* outlink = cmd->filter->outputs[i];
            FilterLinkInternal* li = (FilterLinkInternal*)outlink;

            if (li->status_in != li->status_out) {
                MEDIA_WARN("%s outlink %d is not EOF synchronized, cmd '%s' pending\n",
                    cmd->filter->name, i, cmd->cmd);
                return -EAGAIN;
            }

            if (outlink->dst && outlink->dst->nb_outputs > 0) {
                for (j = 0; j < outlink->dst->nb_outputs; j++) {
                    AVFilterLink* dst_outlink = outlink->dst->outputs[j];
                    FilterLinkInternal* dst_li = (FilterLinkInternal*)dst_outlink;

                    if (dst_li->status_in != dst_li->status_out) {
                        MEDIA_WARN("%s (downstream of %s) outlink %d not EOF synchronized, cmd '%s' pending\n",
                            outlink->dst->name, cmd->filter->name, j, cmd->cmd);
                        return -EAGAIN;
                    }
                }
            }
        }

        ret = avfilter_process_command(cmd->filter, cmd->cmd, cmd->arg, cmd->res, 0, 0);
        if (ret >= 0) {
            ret = audio_negotiation_trigger(cmd->filter);
            if (ret < 0)
                MEDIA_ERR("Media graph link error after command '%s': %d:%s\n",
                    cmd->cmd, ret, av_err2str(ret));
        }
    } else
        ret = avfilter_process_command(cmd->filter, cmd->cmd, cmd->arg,
            cmd->res, 0, 0);

    if (ret >= 0)
        audio_graph_try_touch(priv);

    pthread_mutex_lock(&priv->qlock);
    TAILQ_REMOVE(&priv->cmdq, cmd, entries);
    pthread_mutex_unlock(&priv->qlock);

exit:
    free(cmd->cmd);
    free(cmd->arg);
    free(cmd);
    return ret;
}

static int audio_graph_uninit(MediadPlugin* ctx)
{
    MediaGraphPriv* priv = ctx->priv;
    int ret;

    do {
        ret = audio_graph_dequeue_command(priv, false);
    } while (ret >= 0);

    avfilter_graph_free(&priv->graph);
    av_freep(&priv->occupied);

    return 0;
}

static int audio_graph_get_pollfds(MediadPlugin* ctx, struct pollfd* fds,
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

        ret = audio_graph_queue_command(priv, filter, "get_pollfd", NULL,
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

static int audio_graph_poll_available(MediadPlugin* ctx, struct pollfd* fd, void* cookie)
{
    MediaGraphPriv* priv = ctx->priv;
    eventfd_t unuse;
    int ret = 0;

    if (!fd)
        return -EINVAL;

    if (cookie)
        ret = audio_graph_queue_command(priv, cookie, "poll_available", NULL,
            (char*)fd, sizeof(struct pollfd),
            AV_OPT_SEARCH_CHILDREN);
    else
        eventfd_read(priv->fd, &unuse);

    if (ret < 0)
        MEDIA_ERR("audio_graph_poll_available failed: %d:%s\n",
            ret, av_err2str(ret));

    return ret;
}

static int audio_graph_run_all(AVFilterGraph* graph)
{
    FFFilterContext* ctxi;
    unsigned i;
    int ret;

    while (1) {
        ctxi = fffilterctx(graph->filters[0]);
        for (i = 1; i < graph->nb_filters; i++) {
            FFFilterContext* ctxi_other = fffilterctx(graph->filters[i]);
            if (ctxi_other->ready > ctxi->ready)
                ctxi = ctxi_other;
        }
        if (!ctxi->ready) {
            ret = 0;
            break;
        }

        ret = ff_filter_activate(&ctxi->p);
        if (ret < 0 && ret != AVERROR_EOF && ret != AVERROR(EAGAIN)) {
            av_log(graph, AV_LOG_ERROR, "%s %s activate failed, ret %d.\n", __func__, ctxi->p.name, ret);
            break;
        }
    }

    return ret;
}

static int audio_graph_run_once(MediadPlugin* ctx)
{
    MediaGraphPriv* priv = ctx->priv;
    int ret;

    ret = audio_graph_run_all(priv->graph);
    if (ret < 0)
        return ret;

    do {
        ret = audio_graph_dequeue_command(priv, true);
    } while (ret >= 0);

    return ret == -EAGAIN ? 0 : ret;
}

static int audio_graph_dump_link(AVBPrint* buf, AVFilterLink* link, const char* filter_ext_info)
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
        av_bprintf(buf, "[%dHz %s: status_in:%d status_out: %d fifo:%d wt:%d icnt:%" PRId64 " ocnt:%" PRId64 " ",
            (int)link->sample_rate, format, li->status_in, li->status_out, (int)ff_framequeue_queued_frames(&li->fifo),
            li->frame_wanted_out, li->l.frame_count_in, li->l.frame_count_out);
        av_channel_layout_describe_bprint(&link->ch_layout, buf);
        if (filter_ext_info && filter_ext_info[0]) {
            av_bprintf(buf, " ext:%s", filter_ext_info);
        }
        av_bprint_chars(buf, ']', 1);
        break;

    default:
        av_bprintf(buf, "?");
        break;
    }
    return buf->len;
}

static void audio_graph_dump_to_buf(AVBPrint* buf, AVFilterGraph* graph)
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
        char filter_ext_info[256] = { 0 };
        int ret;

        /* Send dump command to filter to get extra info */
        if (filter->filter->process_command) {
            ret = avfilter_process_command(filter, "dump", NULL, filter_ext_info, sizeof(filter_ext_info), 0);
            if (ret < 0) {
                filter_ext_info[0] = '\0';
            }
        }

        for (j = 0; j < filter->nb_inputs; j++) {
            AVFilterLink* l = filter->inputs[j];
            unsigned ln = strlen(l->src->name) + 1 + strlen(l->srcpad->name);
            max_src_name = FFMAX(max_src_name, ln);
            max_in_name = FFMAX(max_in_name, strlen(l->dstpad->name));
            max_in_fmt = FFMAX(max_in_fmt, audio_graph_dump_link(NULL, l, filter_ext_info));
        }
        for (j = 0; j < filter->nb_outputs; j++) {
            AVFilterLink* l = filter->outputs[j];
            unsigned ln = strlen(l->dst->name) + 1 + strlen(l->dstpad->name);
            max_dst_name = FFMAX(max_dst_name, ln);
            max_out_name = FFMAX(max_out_name, strlen(l->srcpad->name));
            max_out_fmt = FFMAX(max_out_fmt, audio_graph_dump_link(NULL, l, filter_ext_info));
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
                audio_graph_dump_link(buf, l, filter_ext_info);
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
                audio_graph_dump_link(buf, l, filter_ext_info);
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

static char* audio_graph_server_dump(AVFilterGraph* graph, const char* options)
{
    AVBPrint buf;
    char* dump;

    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_COUNT_ONLY);
    audio_graph_dump_to_buf(&buf, graph);
    dump = av_malloc(buf.len + 1);
    if (!dump)
        return NULL;
    av_bprint_init_for_buffer(&buf, dump, buf.len + 1);
    audio_graph_dump_to_buf(&buf, graph);
    return dump;
}

static int audio_graph_handler(MediadPlugin* ctx, struct media_server_conn* conn, const char* target, const char* cmd,
    const char* arg, int flags, char* res, int res_len)
{
    MediaGraphPriv* priv = ctx->priv;
    int i, ret = 0;
    char* dump;

    MEDIA_INFO("cookie %p target %s cmd %s arg %s flags %d res %p res_len %d\n",
        conn, target, cmd, arg, flags, res, res_len);

    if (!target && !strcmp(cmd, "dump")) {
        dump = audio_graph_server_dump(priv->graph, NULL);
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

        if (!strcmp(target, filter->name)) {
            ret = audio_graph_queue_command(priv, filter, cmd, arg, res, res_len, flags);
        } else {
            const char* tmp = strchr(filter->name, '@');

            if (tmp && !strncmp(tmp + 1, target, strlen(target)))
                ret = audio_graph_queue_command(priv, filter, cmd, arg, res, res_len, flags);
        }

        if (ret < 0)
            return ret;
    }

    return 0;
}

MediadPlugin audio_graph_plugin = {
    .name = "audio_graph",
    .priv_size = sizeof(MediaGraphPriv),
    .priv = NULL,
    .init = audio_graph_init,
    .get = audio_graph_get_pollfds,
    .available = audio_graph_poll_available,
    .run_once = audio_graph_run_once,
    .uninit = audio_graph_uninit,
    .process_command = audio_graph_handler,
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int audio_graph_open(AVFilterContext** src, const char* stream)
{
    MediaGraphPriv* priv = audio_graph_plugin.priv;
    AVFilterGraph* graph = priv->graph;
    char stream_name[64] = { 0 };
    int ret;

    ret = media_stub_get_stream_name(stream, stream_name, sizeof(stream_name));
    if (ret >= 0)
        stream = stream_name;
    else {
        goto fail;
    }

    pthread_mutex_lock(&priv->qlock);
    for (int i = 0; i < graph->nb_filters; i++) {
        if (graph->filters[i]->name && !priv->occupied[i] && !strncmp(stream, graph->filters[i]->name, strlen(stream))) {
            priv->occupied[i] = 1;
            *src = graph->filters[i];
            break;
        }
    }
    pthread_mutex_unlock(&priv->qlock);

    if (!*src) {
        MEDIA_ERR("%s stream is not found\n", ret >= 0 ? stream_name : stream);
        ret = -EINVAL;
        goto fail;
    }

    return 0;

fail:
    return ret;
}

int audio_graph_start(AVFilterContext* src, int format, int sample_rate, int channels,
    int (*on_event_cb)(void* udata, int evt, int64_t args), void* udata)
{
    MediaGraphPriv* priv = audio_graph_plugin.priv;
    char msg[128] = { 0 };
    int ret;

    snprintf(msg, sizeof(msg), "%p %p fmt=%d:rate=%d:ch=%d", on_event_cb,
        udata, format, sample_rate, channels);
    ret = audio_graph_queue_command(priv, src, "link", msg, NULL, 0, 0);
    if (ret < 0) {
        MEDIA_ERR("%s link failed ret:%d\n", src->name, ret);
        return ret;
    }

    return 0;
}

int audio_graph_stop(AVFilterContext* src)
{
    MediaGraphPriv* priv = audio_graph_plugin.priv;
    int ret;

    ret = audio_graph_queue_command(priv, src, "unlink",
        NULL, NULL, 0, 0);
    if (ret < 0)
        MEDIA_ERR("unlink %s failed: %d\n", src->name, ret);

    return ret;
}

int audio_graph_close(AVFilterContext** src)
{
    MediaGraphPriv* priv = audio_graph_plugin.priv;

    pthread_mutex_lock(&priv->qlock);
    for (int i = 0; i < priv->graph->nb_filters; i++) {
        AVFilterContext* filter = priv->graph->filters[i];
        if (filter == *src) {
            priv->occupied[i] = 0;
            break;
        }
    }
    pthread_mutex_unlock(&priv->qlock);
    *src = NULL;

    return 0;
}

int audio_graph_pause(AVFilterContext* src)
{
    MediaGraphPriv* priv = audio_graph_plugin.priv;
    int ret;

    ret = audio_graph_queue_command(priv, src, "pause", NULL, NULL, 0, 0);
    if (ret < 0)
        MEDIA_ERR("pause %s failed: %d\n", src->name, ret);

    return ret;
}

int audio_graph_resume(AVFilterContext* src)
{
    MediaGraphPriv* priv = audio_graph_plugin.priv;
    int ret;

    ret = audio_graph_queue_command(priv, src, "resume", NULL, NULL, 0, 0);
    if (ret < 0) {
        MEDIA_ERR("%s resume failed ret:%d\n", src->name, ret);
        return ret;
    }

    return 0;
}

int audio_graph_set_parameter(AVFilterContext* src, const char* param, const char* value)
{
    MediaGraphPriv* priv = audio_graph_plugin.priv;
    char msg[128];
    int ret;

    if (!param || !value)
        return -EINVAL;

    snprintf(msg, sizeof(msg), "%s=%s", param, value);

    ret = audio_graph_queue_command(priv, src, "set_parameter", msg, NULL, 0, 0);
    if (ret < 0)
        MEDIA_ERR("%s set_parameter failed ret:%d\n", src->name, ret);

    return ret;
}

int audio_graph_get_parameter(AVFilterContext* src, const char* key, char* res, int res_len)
{
    MediaGraphPriv* priv = audio_graph_plugin.priv;
    char msg[128];
    int ret;

    if (!key || !res || res_len <= 0)
        return -EINVAL;

    snprintf(msg, sizeof(msg), "%s", key);

    ret = audio_graph_queue_command(priv, src, "get_parameter", msg, res, res_len, 0);
    if (ret < 0)
        MEDIA_ERR("%s get_parameter failed ret:%d\n", src->name, ret);

    return ret;
}
