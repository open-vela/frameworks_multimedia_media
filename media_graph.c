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
#include <sys/eventfd.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

#include "media_internal.h"
#include <media_api.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MAX_GRAPH_SIZE 4096
#define MAX_POLL_FILTERS 16

#define MEDIA_FILTER_FIELDS  \
    AVFilterContext* filter; \
    void* cookie;            \
    bool event;

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
    MEDIA_FILTER_FIELDS
} MediaFilterPriv;

typedef struct MediaPlayerPriv {
    MEDIA_FILTER_FIELDS
    LIST_ENTRY(MediaPlayerPriv)
    entry;
    char* name;
} MediaPlayerPriv;

typedef struct MediaRecorderPriv {
    MEDIA_FILTER_FIELDS
} MediaRecorderPriv;

typedef struct MediaSessionPriv {
    LIST_ENTRY(MediaSessionPriv)
    entry;
    void* cookie;
    bool event;
    char* name;
} MediaSessionPriv;

typedef struct MediaCommand {
    AVFilterContext* filter;
    char* cmd;
    char* arg;
    int flags;
    struct MediaCommand* next;
} MediaCommand;

LIST_HEAD(MediaPlayerPrivHead, MediaPlayerPriv);
LIST_HEAD(MediaSessionPrivHead, MediaSessionPriv);

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

static struct MediaPlayerPrivHead g_players;
static struct MediaSessionPrivHead g_sessions;
static pthread_mutex_t g_media_mutex;

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

static int media_graph_queue_command(AVFilterContext* filter,
    const char* cmd, const char* arg,
    char** res, int res_len, int flags)
{
    MediaGraphPriv* priv;
    MediaCommand* tmp;

    priv = media_get_graph();
    if (!priv)
        return -EINVAL;

    if (res_len && res && !*res) {
        *res = malloc(res_len);
        if (!*res)
            return -ENOMEM;
    }

    if (res && *res)
        return avfilter_process_command(filter, cmd, arg, *res, res_len, flags);

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

static int media_graph_dequeue_command(bool process)
{
    MediaGraphPriv* priv;
    MediaCommand* tmp;
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

static void* media_find_filter(const char* prefix, bool input, bool available)
{
    AVFilterContext* filter = NULL;
    MediaGraphPriv* media;
    char name[64];
    int i, j;

    media = media_get_graph();
    if (!media || !media->graph)
        return NULL;

    for (i = 0; i < media->graph->nb_filters; i++) {
        filter = media->graph->filters[i];

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

static bool media_player_is_most_active(void* handle)
{
    MediaPlayerPriv* priv = handle;
    MediaPlayerPriv* tmp;

    if (!priv->name)
        return false;

    LIST_FOREACH(tmp, &g_players, entry)
    {
        if (tmp->name && !strcmp(tmp->name, priv->name))
            return tmp == priv;
    }

    return false;
}

static void media_session_notify_event(const char* name, int event,
    int result, const char* extra)
{
    MediaSessionPriv* tmp;

    if (!name)
        return;

    LIST_FOREACH(tmp, &g_sessions, entry)
    {
        if (!strcmp(tmp->name, name) && tmp->event)
            media_stub_notify_event(tmp->cookie, event, result, extra);
    }
}

static void media_player_event_cb(void* cookie, int event,
    int result, const char* extra)
{
    MediaPlayerPriv* priv = cookie;

    switch (event) {
    case MEDIA_EVENT_STARTED:
        pthread_mutex_lock(&g_media_mutex);
        LIST_REMOVE(priv, entry);
        LIST_INSERT_HEAD(&g_players, priv, entry);
        media_session_notify_event(priv->name, event, result, extra);
        pthread_mutex_unlock(&g_media_mutex);
        break;

    case MEDIA_EVENT_PAUSED:
    case MEDIA_EVENT_STOPPED:
    case MEDIA_EVENT_COMPLETED:
        pthread_mutex_lock(&g_media_mutex);
        if (media_player_is_most_active(priv))
            media_session_notify_event(priv->name, event, result, extra);
        pthread_mutex_unlock(&g_media_mutex);
        break;

    case AVMOVIE_ASYNC_EVENT_CLOSED:
        pthread_mutex_lock(&g_media_mutex);
        LIST_REMOVE(priv, entry);
        pthread_mutex_unlock(&g_media_mutex);
        priv->filter->opaque = NULL;
        free(priv->name);
        free(priv);
        return;
    }

    if (priv->event)
        media_stub_notify_event(priv->cookie, event, result, extra);
}

static void media_recorder_event_cb(void* cookie, int event,
    int result, const char* extra)
{
    MediaRecorderPriv* priv = cookie;

    if (event == AVMOVIE_ASYNC_EVENT_CLOSED) {
        priv->filter->opaque = NULL;
        free(priv);
        return;
    }

    if (priv->event)
        media_stub_notify_event(priv->cookie, event, result, extra);
}

static void* media_common_open(const char* arg, void* cookie, bool player)
{
    AVMovieAsyncEventCookie event;
    MediaFilterPriv* priv;

    priv = zalloc(player ? sizeof(MediaPlayerPriv) : sizeof(MediaRecorderPriv));
    if (!priv)
        return NULL;

    priv->filter = media_find_filter(arg, player, true);
    if (!priv->filter)
        goto err;

    if (avfilter_process_command(priv->filter, "open", NULL, NULL, 0, 0) < 0)
        goto err;

    event.cookie = priv;
    event.event = player ? media_player_event_cb : media_recorder_event_cb;

    if (avfilter_process_command(priv->filter, "set_event", (const char*)&event, NULL, 0, 0) < 0) {
        avfilter_process_command(priv->filter, "close", NULL, NULL, 0, 0);
        goto err;
    }

    priv->cookie = cookie;
    priv->filter->opaque = priv;

    return priv;

err:
    free(priv);
    return NULL;
}

static int media_common_handler(void* handle, const char* target, const char* cmd,
    const char* arg, char** res, int res_len, bool player)
{
    MediaFilterPriv* priv = handle;
    AVFilterContext* filter;
    bool start, quit;
    int ret;

    if (!strcmp(cmd, "set_event")) {
        priv->event = true;

        return 0;
    }

    start = !strcmp(cmd, "start");
    quit = !strcmp(cmd, "close") || !strcmp(cmd, "pause") || !strcmp(cmd, "stop");
    if (start || quit) {
        ret = media_graph_queue_command(priv->filter, cmd, arg, NULL, 0, 0);
        if (ret >= 0)
            media_policy_set_stream_status(priv->filter->name, start);

        return ret;
    }

    if (target) {
        filter = avfilter_find_on_link(priv->filter, target, NULL, player, NULL);
        if (!filter)
            return -EINVAL;
    } else
        filter = priv->filter;

    return media_graph_queue_command(filter, cmd, arg, res, res_len, AV_OPT_SEARCH_CHILDREN);
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

    priv->fd = eventfd(0, 0);
    if (priv->fd < 0)
        goto err;

    ret = fs_getfilep(priv->fd, &priv->filep);
    if (ret < 0)
        goto err;

    ret = media_graph_load(priv, file);
    if (ret < 0)
        goto err;

    LIST_INIT(&g_players);
    LIST_INIT(&g_sessions);
    pthread_mutex_init(&g_media_mutex, NULL);

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

int media_graph_destroy(void* handle)
{
    MediaGraphPriv* priv = handle;
    int ret;

    if (!priv || !priv->graph)
        return -EINVAL;

    do {
        ret = media_graph_dequeue_command(false);
    } while (ret >= 0);

    pthread_mutex_destroy(&g_media_mutex);

    avfilter_graph_free(&priv->graph);
    free(priv);

    return 0;
}

int media_graph_get_pollfds(void* handle, struct pollfd* fds,
    void** cookies, int count)
{
    MediaGraphPriv* priv = handle;
    int ret, nfd, i;

    if (!priv || !priv->graph || !fds || count < 2)
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

int media_graph_poll_available(void* handle, struct pollfd* fd, void* cookie)
{
    MediaGraphPriv* priv = handle;
    eventfd_t unuse;

    if (!priv || !priv->graph || !fd)
        return -EINVAL;

    if (cookie)
        avfilter_process_command(cookie, "poll_available", NULL,
            (char*)fd, sizeof(struct pollfd),
            AV_OPT_SEARCH_CHILDREN);
    else
        eventfd_read(priv->fd, &unuse);

    return 0;
}

int media_graph_run_once(void* handle)
{
    MediaGraphPriv* priv = handle;
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

int media_graph_handler(void* handle, const char* target, const char* cmd,
    const char* arg, char** res, int res_len)
{
    MediaGraphPriv* priv = handle;
    int i, ret = 0;

    if (!priv || !priv->graph)
        return -EINVAL;

    if (!strcmp(cmd, "dump")) {
        if (!res)
            return -EINVAL;

        *res = avfilter_graph_dump_ext(priv->graph, arg);
        return 0;
    }

    for (i = 0; i < priv->graph->nb_filters; i++) {
        AVFilterContext* filter = priv->graph->filters[i];

        if (!strcmp(target, filter->name))
            ret = media_graph_queue_command(filter, cmd, arg, res, res_len, 0);
        else {
            const char* tmp = strchr(filter->name, '@');

            if (tmp && !strncmp(tmp + 1, target, strlen(target)))
                ret = media_graph_queue_command(filter, cmd, arg, res, res_len, 0);
        }

        if (ret < 0)
            return ret;
    }

    return 0;
}

int media_player_handler(void* handle, const char* target, const char* cmd,
    const char* arg, char** res, int res_len)
{
    MediaPlayerPriv* priv;
    char* name;

    if (!strcmp(cmd, "open")) {
        if (!res)
            return -EINVAL;

        *res = zalloc(res_len);
        if (!*res)
            return -ENOMEM;

        if (arg) {
            name = strdup(arg);
            if (!name)
                return -ENOMEM;
        } else
            name = NULL;

        priv = media_common_open(arg, handle, true);
        if (!priv) {
            free(name);
            return -EINVAL;
        }

        priv->name = name;

        pthread_mutex_lock(&g_media_mutex);
        LIST_INSERT_HEAD(&g_players, priv, entry);
        pthread_mutex_unlock(&g_media_mutex);

        return snprintf(*res, res_len, "%llu", (uint64_t)(uintptr_t)priv);
    }

    return media_common_handler(handle, target, cmd, arg, res, res_len, true);
}

int media_recorder_handler(void* handle, const char* target, const char* cmd,
    const char* arg, char** res, int res_len)
{
    MediaRecorderPriv* priv;

    if (!strcmp(cmd, "open")) {
        if (!res)
            return -EINVAL;

        *res = zalloc(res_len);
        if (!*res)
            return -ENOMEM;

        priv = media_common_open(arg, handle, false);
        if (!priv)
            return -EINVAL;

        return snprintf(*res, res_len, "%llu", (uint64_t)(uintptr_t)priv);
    }

    return media_common_handler(handle, target, cmd, arg, res, res_len, false);
}

int media_session_handler(void* handle, const char* target, const char* cmd,
    const char* arg, char** res, int res_len)
{
    MediaPlayerPriv *tmp, *player = NULL;
    AVFilterContext* filter;
    MediaSessionPriv* priv;
    int ret;

    if (!strcmp(cmd, "open")) {
        if (!res || !arg)
            return -EINVAL;

        filter = media_find_filter(arg, true, false);
        if (!filter)
            return -EINVAL;

        priv = zalloc(sizeof(MediaSessionPriv));
        if (!priv)
            return -ENOMEM;

        priv->cookie = handle;
        priv->event = false;

        priv->name = strdup(arg);
        if (!priv->name) {
            free(priv);
            return -ENOMEM;
        }

        *res = malloc(res_len);
        if (!*res) {
            free(priv->name);
            free(priv);
            return -ENOMEM;
        }

        pthread_mutex_lock(&g_media_mutex);
        LIST_INSERT_HEAD(&g_sessions, priv, entry);
        pthread_mutex_unlock(&g_media_mutex);

        return snprintf(*res, res_len, "%llu", (uint64_t)(uintptr_t)priv);
    }

    priv = handle;

    if (!strcmp(cmd, "close")) {
        pthread_mutex_lock(&g_media_mutex);
        LIST_REMOVE(priv, entry);
        pthread_mutex_unlock(&g_media_mutex);

        free(priv->name);
        free(priv);

        return 0;
    }

    if (!strcmp(cmd, "set_event")) {
        priv->event = true;

        return 0;
    }

    /* Find the most active player. */

    pthread_mutex_lock(&g_media_mutex);
    LIST_FOREACH(tmp, &g_players, entry)
    {
        if (tmp->name && !strcmp(tmp->name, priv->name)) {
            player = tmp;
            break;
        }
    }
    pthread_mutex_unlock(&g_media_mutex);

    if (!player)
        return -ENOENT;

    /* Try process command and send control message to player client. */

    ret = media_common_handler(player, target, cmd, arg, res, res_len, true);
    if (player->event) {
        if (!strcmp(cmd, "start"))
            ret = MEDIA_EVENT_START;
        else if (!strcmp(cmd, "pause"))
            ret = MEDIA_EVENT_PAUSE;
        else if (!strcmp(cmd, "stop"))
            ret = MEDIA_EVENT_STOP;
        else if (!strcmp(cmd, "prev"))
            ret = MEDIA_EVENT_PREV;
        else if (!strcmp(cmd, "next"))
            ret = MEDIA_EVENT_NEXT;
        else
            return ret;
        media_stub_notify_event(player->cookie, ret, 0, arg);
    }

    return ret;
}
