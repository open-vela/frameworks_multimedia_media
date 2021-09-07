/****************************************************************************
 * frameworks/media/media_server.c
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

#include <nuttx/config.h>
#include <nuttx/fs/fs.h>

#include <libavdevice/avdevice.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/internal.h>
#include <libavutil/opt.h>

#include <assert.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <syslog.h>
#include <sys/eventfd.h>

#include <media_api.h>
#include "media_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MAX_POLLFDS         32
#define MAX_GRAPH_SIZE      4096

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct MediaServerPriv {
    AVFilterGraph *graph;
    struct file   *filep;
} MediaServerPriv;

/****************************************************************************
 * Private Data
 ****************************************************************************/

static MediaServerPriv g_media;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void media_server_filter_ready(AVFilterContext *ctx)
{
    MediaServerPriv *priv = ctx->graph->opaque;
    eventfd_t val = 1;

    file_write(priv->filep, &val, sizeof(eventfd_t));
}

static int media_server_loop(MediaServerPriv *priv)
{
    AVFilterGraph *graph = priv->graph;
    AVFilterContext *filters[MAX_POLLFDS];
    struct pollfd pfds[MAX_POLLFDS];
    int ret, nfd, i;
    int fd;

    fd = eventfd(0, 0);
    if (fd < 0)
        return -errno;

    ret = fs_getfilep(fd, &priv->filep);
    if (ret < 0) {
        close(fd);
        return ret;
    }

    while (1) {
        nfd            = 1;
        pfds[0].fd     = fd;
        pfds[0].events = POLLIN;

        for (i = 0; i < graph->nb_filters; i++) {
            AVFilterContext *filter = graph->filters[i];

            ret = avfilter_process_command(filter, "get_pollfd", NULL,
                    (char *)&pfds[nfd], sizeof(struct pollfd) * (MAX_POLLFDS - nfd),
                    AV_OPT_SEARCH_CHILDREN);
            if (ret >= 0) {
                while (pfds[nfd].events)
                    filters[nfd++] = filter;

                assert(nfd < MAX_POLLFDS);
            }
        }

        poll(pfds, nfd, -1);

        for (i = 0; i < nfd; i++) {
            eventfd_t unuse;

            if (!pfds[i].revents)
                continue;

            if (i == 0)
                eventfd_read(fd, &unuse);
            else
                avfilter_process_command(filters[i], "poll_available", NULL,
                                         (char *)&pfds[i], sizeof(struct pollfd),
                                         AV_OPT_SEARCH_CHILDREN);
        }

        do {
            ret = ff_filter_graph_run_once(graph);
        } while (ret == 0);
    }

    return 0;
}

static void media_server_log_callback(void *avcl, int level,
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

static int media_server_loadgraph(MediaServerPriv *priv)
{
    char graph_desc[MAX_GRAPH_SIZE];
    AVFilterInOut *input = NULL;
    AVFilterInOut *output = NULL;
    int ret;
    int fd;

    fd = open(CONFIG_MEDIA_SERVER_CONFIG_PATH"graph.conf", O_RDONLY | O_BINARY);
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
    av_log_set_callback(media_server_log_callback);

    priv->graph = avfilter_graph_alloc();
    if (!priv->graph)
        return -ENOMEM;

    ret = avfilter_graph_parse2(priv->graph, graph_desc, &input, &output);
    if (ret < 0)
        goto out;

    avfilter_inout_free(&input);
    avfilter_inout_free(&output);

    ret = avfilter_graph_config(priv->graph, NULL);
    if (ret < 0)
        goto out;

    priv->graph->ready  = media_server_filter_ready;
    priv->graph->opaque = priv;

    return 0;
out:
    avfilter_graph_free(&priv->graph);
    return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void *media_server_get_graph_(void)
{
    MediaServerPriv *priv = &g_media;

    return priv->graph;
}

void media_server_dump_(const char *options)
{
    MediaServerPriv *priv = &g_media;
    char *tmp;

    if (!priv->graph)
        return;

    tmp = avfilter_graph_dump_ext(priv->graph, options);
    if (tmp) {
        av_log(NULL, AV_LOG_INFO, "%s\n%s", __func__, tmp);
        free(tmp);
    }
}

int main(int argc, FAR char *argv[])
{
    MediaServerPriv *priv = &g_media;
    int ret;

    ret = media_server_loadgraph(priv);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "%s, loadgraph failed\n", __func__);
        return ret;
    }

    ret = media_server_loop(priv);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "%s, server loop failed\n", __func__);
        avfilter_graph_free(&priv->graph);
    }

    return ret;
}
