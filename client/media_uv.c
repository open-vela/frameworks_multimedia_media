/****************************************************************************
 * frameworks/media/client/media_uv.c
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

#include <stdlib.h>
#include <sys/queue.h>
#include <uv.h>

#include "media_common.h"
#include "media_uv.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MEDIA_PROXYFLAG_CONNECTING 1
#define MEDIA_PROXYFLAG_LISTENING 2
#define MEDIA_PROXYFLAG_RECONNECT 4
#define MEDIA_PROXYFLAG_DISCONNECT 8

#define MEDIA_MSGFLAG_WRITTEN 1
#define MEDIA_MSGFLAG_RESPONSED 2

#define MEDIA_CPU_DELIM " ,;\t\n"

#define MEDIA_DEBUG_PROXY(p_)                                          \
    do {                                                               \
        if (p_)                                                        \
            MEDIA_DEBUG("%p f:%d pipe:%p,%p q:%d,%d done:%d,%d",       \
                p_, p_->flags, p_->cpipe, p_->epipe,                   \
                p_->nb_pendq, p_->nb_sentq, p_->nb_sent, p_->nb_recv); \
        else                                                           \
            MEDIA_DEBUG("null proxy\n");                               \
    } while (0)

#define MEDIA_ERR_PROXY(p_, err_)                                            \
    do {                                                                     \
        if (p_)                                                              \
            MEDIA_ERR("%p f:%d pipe:%p,%p q:%d,%d done:%d,%d err:%d",        \
                p_, p_->flags, p_->cpipe, p_->epipe,                         \
                p_->nb_pendq, p_->nb_sentq, p_->nb_sent, p_->nb_recv, err_); \
        else                                                                 \
            MEDIA_ERR("null proxy\n");                                       \
    } while (0)

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct MediaPipePriv MediaPipePriv;
typedef struct MediaProxyPriv MediaProxyPriv;
typedef struct MediaWritePriv MediaWritePriv;
typedef TAILQ_ENTRY(MediaWritePriv) MediaWriteEntry;
typedef TAILQ_HEAD(MediaWriteQueue, MediaWritePriv) MediaWriteQueue;

struct MediaPipePriv {
    MediaProxyPriv* proxy;
    uv_pipe_t handle;
    media_parcel parcel;
    size_t offset;
};

struct MediaWritePriv {
    MediaProxyPriv* proxy;
    MediaWriteEntry entry;
    uv_write_t req;
    media_parcel parcel;
    media_uv_parcel_callback on_receive;
    void* cookies[2]; /* One-time private context. */
    int flags;
};

struct MediaProxyPriv {
    uv_loop_t* loop;
    const char* cpu;
    char* cpus;
    MediaPipePriv* cpipe; /* To send command and receive result. */
    MediaPipePriv* epipe; /* To receive event notification. */
    media_uv_callback on_connect;
    media_uv_callback on_release;
    media_uv_callback on_listen;
    media_uv_parcel_callback on_event;
    void* cookie; /* Long-term private context. */
    MediaWriteQueue pendq; /* Writings to send. */
    MediaWriteQueue sentq; /* Writings to recv after sending. */
    int nb_pendq;
    int nb_sentq;
    int nb_sent;
    int nb_recv;
    int flags;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* Alloc and free. */
static void media_uv_free_writing(MediaWritePriv* writing);
static MediaWritePriv* media_uv_alloc_writing(int code, const media_parcel* parcel);
static void media_uv_free_pipe(MediaPipePriv* pipe);
static MediaPipePriv* media_uv_alloc_pipe(MediaProxyPriv* proxy);
static void media_uv_free_proxy(MediaProxyPriv* proxy);

/* clear queue and call cb. */
static void media_uv_clear_queue(MediaProxyPriv* proxy);

/* Shutdown and close. */
static void media_uv_close_cb(uv_handle_t* handle);
static void media_uv_close(MediaPipePriv* pipe);
static void media_uv_shutdown_cb(uv_shutdown_t* req, int status);
static void media_uv_shutdown(MediaPipePriv* pipe);

/* Connect to server. */
static void media_uv_reconnect_one(MediaProxyPriv* proxy);
static void media_uv_connect_one_cb(uv_connect_t* req, int status);
static int media_uv_connect_one(MediaProxyPriv* proxy);

/* Read receive result. */
static void media_uv_alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
static void media_uv_read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);
static int media_uv_handle_parcel(MediaPipePriv* pipe);

/* Write control message. */
static void media_uv_write_cb(uv_write_t* req, int status);
static int media_uv_send_writing(MediaProxyPriv* proxy, MediaWritePriv* writing);
static MediaWritePriv* media_uv_dequeue_writing(MediaProxyPriv* proxy);
static void media_uv_delivery_writing(MediaProxyPriv* proxy);
static int media_uv_queue_writing(MediaProxyPriv* proxy, MediaWritePriv* writing);

/* Create listener. */
static int media_uv_create_notify(MediaProxyPriv* proxy);
static void media_uv_listen_one_cb(uv_stream_t* stream, int status);
static int media_uv_listen_one(MediaProxyPriv* proxy);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/**
 * @brief free writing.
 *
 * Will release only after 2 case are all statisfied:
 * 1. uv_write_cb is called, which means write is done.
 * 2. received response; or uv_write failed, which means there won't be response.
 */
static void media_uv_free_writing(MediaWritePriv* writing)
{
    if (writing && (writing->flags & MEDIA_MSGFLAG_WRITTEN)
        && (writing->flags & MEDIA_MSGFLAG_RESPONSED)) {
        media_parcel_deinit(&writing->parcel);
        free(writing);
    }
}

static MediaWritePriv* media_uv_alloc_writing(int code, const media_parcel* parcel)
{
    MediaWritePriv* writing;

    writing = zalloc(sizeof(MediaWritePriv));
    if (!writing)
        return NULL;

    uv_req_set_data((uv_req_t*)&writing->req, writing);
    media_parcel_init(&writing->parcel);
    if (parcel)
        media_parcel_clone(&writing->parcel, parcel);

    writing->parcel.chunk->code = code;
    return writing;
}

static void media_uv_free_pipe(MediaPipePriv* pipe)
{
    media_parcel_deinit(&pipe->parcel);
    free(pipe);
}

static MediaPipePriv* media_uv_alloc_pipe(MediaProxyPriv* proxy)
{
    MediaPipePriv* pipe;

    pipe = zalloc(sizeof(MediaPipePriv));
    if (!pipe)
        return NULL;

    if (uv_pipe_init(proxy->loop, &pipe->handle, 0) < 0) {
        free(pipe);
        return NULL;
    }

    pipe->proxy = proxy;
    media_parcel_init(&pipe->parcel);
    uv_handle_set_data((uv_handle_t*)&pipe->handle, pipe);
    return pipe;
}

static void media_uv_clear_queue(MediaProxyPriv* proxy)
{
    MediaWritePriv* writing;

    TAILQ_CONCAT(&proxy->sentq, &proxy->pendq, entry);
    while ((writing = TAILQ_FIRST(&proxy->sentq))) {
        TAILQ_REMOVE(&proxy->sentq, writing, entry);
        writing->flags = MEDIA_MSGFLAG_WRITTEN | MEDIA_MSGFLAG_RESPONSED;
        media_uv_free_writing(writing);
    }
}

/**
 * @brief free proxy.
 *
 * Will release only after 3 case are all statisfied:
 * 1. Command uv_pipe closed; @see media_uv_read_cb.
 * 2. Event uv_pipe closed; @see media_uv_read_cb.
 * 3. Called `media_uv_disconnect`.
 */
static void media_uv_free_proxy(MediaProxyPriv* proxy)
{
    MEDIA_DEBUG_PROXY(proxy);
    if (proxy->cpipe || proxy->epipe)
        return; /* Sockets still active. */

    if (proxy->flags & MEDIA_PROXYFLAG_DISCONNECT) {
        /* Concat 2 queue and clear all. */
        media_uv_clear_queue(proxy);
        if (proxy->on_release)
            proxy->on_release(proxy->cookie, 0);

        free(proxy);
    }
}

static void media_uv_close_cb(uv_handle_t* handle)
{
    MediaPipePriv* pipe = uv_handle_get_data(handle);
    MediaProxyPriv* proxy = pipe->proxy;

    MEDIA_DEBUG_PROXY(proxy);
    if (pipe == proxy->cpipe)
        proxy->cpipe = NULL;
    else if (pipe == proxy->epipe)
        proxy->epipe = NULL;

    media_uv_free_pipe(pipe);
    media_uv_free_proxy(proxy);
}

static void media_uv_close(MediaPipePriv* pipe)
{
    uv_read_stop((uv_stream_t*)&pipe->handle);
    if (!uv_is_closing((uv_handle_t*)&pipe->handle))
        uv_close((uv_handle_t*)&pipe->handle, media_uv_close_cb);
}

static void media_uv_shutdown_cb(uv_shutdown_t* req, int status)
{
    MediaPipePriv* pipe = uv_req_get_data((uv_req_t*)req);

    media_uv_close(pipe);
    free(req);
}

/**
 * @brief Shutdown command uv_pipe.
 *
 * After all `uv_write` is done, posix `shutdown` will be called,
 * server will monitor `POLLHUP` and close 2 sockets.
 */
static void media_uv_shutdown(MediaPipePriv* pipe)
{
    uv_shutdown_t* req;
    int ret;

    MEDIA_DEBUG_PROXY(pipe->proxy);
    req = zalloc(sizeof(uv_shutdown_t));
    if (!req)
        return;

    uv_req_set_data((uv_req_t*)req, pipe);
    ret = uv_shutdown((uv_shutdown_t*)req, (uv_stream_t*)&pipe->handle, media_uv_shutdown_cb);
    if (ret < 0) {
        MEDIA_ERR_PROXY(pipe->proxy, ret);
        media_uv_close(pipe);
        free(req);
    }
}

static void media_uv_reconnect_one(MediaProxyPriv* proxy)
{
    int ret;
    MEDIA_DEBUG_PROXY(proxy);

    /* Try connect to next cpu. */
    proxy->cpu = strtok_r(NULL, MEDIA_CPU_DELIM, &proxy->cpus);
    if (!proxy->cpu) {
        media_uv_clear_queue(proxy);
        proxy->on_connect(proxy->cookie, -ENOENT);
        return;
    }

    if (proxy->cpipe)
        media_uv_close(proxy->cpipe);

    ret = media_uv_connect_one(proxy);
    if (ret < 0)
        proxy->on_connect(proxy->cookie, ret);
}

/**
 * @brief Handle connect result.
 */
static void media_uv_connect_one_cb(uv_connect_t* req, int status)
{
    MediaPipePriv* pipe = uv_handle_get_data((uv_handle_t*)req->handle);
    MediaProxyPriv* proxy = pipe->proxy;
    int flags;

    MEDIA_DEBUG_PROXY(proxy);
    free(req);

    /* Cancel the connect on error and user's cancelation */
    if (status >= 0 && (proxy->flags & MEDIA_PROXYFLAG_DISCONNECT))
        status = -ECANCELED;

    if (status < 0) {
        MEDIA_ERR_PROXY(proxy, status);
        proxy->on_connect(proxy->cookie, status);
        media_uv_close(pipe);
        return;
    }

    /* Temp clear flags so that on_connect can always send control message. */
    flags = proxy->flags;
    proxy->flags = 0;

    media_parcel_init(&pipe->parcel);
    uv_read_start((uv_stream_t*)&pipe->handle, media_uv_alloc_cb, media_uv_read_cb);
    proxy->on_connect(proxy->cookie, status);

    /* Restore flags. */
    proxy->flags |= flags;
}

/**
 * @brief Try connect once
 *
 * Try head of server cpu list, the list is kept in `MediaProxyPriv::cpus`.
 * Before we receive response:
 *  1. avoid shutdown.
 *  2. pend all writting except in `MediaProxyPriv::on_connect`.
 */
static int media_uv_connect_one(MediaProxyPriv* proxy)
{
    char addr[PATH_MAX];
    uv_connect_t* req;
    int ret = -ENOMEM;

    /* Prepare uv_pipe. */
    req = zalloc(sizeof(uv_connect_t));
    if (!req)
        goto err1;

    proxy->cpipe = media_uv_alloc_pipe(proxy);
    if (!proxy->cpipe)
        goto err2;

    /* Connect to media server. */
    snprintf(addr, sizeof(addr), MEDIA_SOCKADDR_NAME, proxy->cpu);
    proxy->flags |= MEDIA_PROXYFLAG_CONNECTING;
    if (!strcmp(proxy->cpu, CONFIG_RPMSG_LOCAL_CPUNAME))
        uv_pipe_connect(req, &proxy->cpipe->handle, addr,
            media_uv_connect_one_cb);
    else {
#ifdef CONFIG_NET_RPMSG
        uv_pipe_rpmsg_connect(req, &proxy->cpipe->handle, addr, proxy->cpu,
            media_uv_connect_one_cb);
#else
        ret = -ENOSYS;
        media_uv_close(proxy->cpipe);
        goto err2;
#endif
    }

    MEDIA_DEBUG_PROXY(proxy);
    return 0;

err2:
    free(req);

err1:
    MEDIA_ERR_PROXY(proxy, ret);
    return ret;
}

static void media_uv_alloc_cb(uv_handle_t* handle,
    size_t suggested_size, uv_buf_t* buf)
{
    MediaPipePriv* pipe = uv_handle_get_data(handle);

    buf->base = (char*)pipe->parcel.chunk + pipe->offset;
    if (pipe->offset < MEDIA_PARCEL_HEADER_LEN)
        buf->len = MEDIA_PARCEL_HEADER_LEN - pipe->offset;
    else
        buf->len = MEDIA_PARCEL_HEADER_LEN + pipe->parcel.chunk->len - pipe->offset;
}

static void media_uv_read_cb(uv_stream_t* stream, ssize_t ret,
    const uv_buf_t* buf)
{
    MediaPipePriv* pipe = uv_handle_get_data((uv_handle_t*)stream);

    if (ret == 0)
        return;

    if (ret < 0)
        goto err;

    pipe->offset += ret;
    if (pipe->offset == MEDIA_PARCEL_HEADER_LEN) {
        ret = media_parcel_grow(&pipe->parcel, 0, pipe->parcel.chunk->len);
        if (ret < 0)
            goto err;
    }

    if (MEDIA_PARCEL_HEADER_LEN + pipe->parcel.chunk->len == pipe->offset) {
        ret = media_uv_handle_parcel(pipe);
        if (ret < 0)
            goto err;

        media_parcel_reinit(&pipe->parcel);
        pipe->offset = 0;
    }

    return;

err:
    if (ret != UV_EOF)
        MEDIA_ERR_PROXY(pipe->proxy, (int)ret);

    media_uv_close(pipe);
}

/**
 * @brief Handle parcel and flags after on_receive callback
 */
static int media_uv_handle_parcel(MediaPipePriv* pipe)
{
    MediaProxyPriv* proxy = pipe->proxy;
    MediaWritePriv* writing;

    if (pipe == proxy->epipe) {
        proxy->on_event(pipe->proxy->cookie, NULL, NULL, &pipe->parcel);
        return 0;
    }

    /* dequeue writing context and notify user. */
    writing = media_uv_dequeue_writing(proxy);
    assert(writing);
    writing->on_receive(proxy->cookie,
        writing->cookies[0], writing->cookies[1], &pipe->parcel);
    writing->flags |= MEDIA_MSGFLAG_RESPONSED;
    media_uv_free_writing(writing);
    proxy->nb_recv++;
    MEDIA_DEBUG_PROXY(proxy);

    /* Handle flags after first on_receive */
    if (proxy->flags & MEDIA_PROXYFLAG_CONNECTING) {
        proxy->flags &= ~MEDIA_PROXYFLAG_CONNECTING;

        if (proxy->flags == MEDIA_PROXYFLAG_LISTENING)
            media_uv_listen_one(proxy); /* Might be new flags. */

        /* Apply writings if user recognize current proxy */
        if (!(proxy->flags & MEDIA_PROXYFLAG_LISTENING)
            && !(proxy->flags & MEDIA_PROXYFLAG_RECONNECT))
            media_uv_delivery_writing(proxy);

        if (proxy->flags & MEDIA_PROXYFLAG_DISCONNECT) {
            media_uv_delivery_writing(proxy);
            return UV_EOF;
        }

        if (proxy->flags & MEDIA_PROXYFLAG_RECONNECT) {
            proxy->flags &= ~MEDIA_PROXYFLAG_RECONNECT;
            media_uv_reconnect_one(proxy);
            return UV_EOF;
        }
    }

    return 0;
}

/**
 * @brief Would add writing to sentq on success.
 */
static void media_uv_write_cb(uv_write_t* req, int status)
{
    MediaWritePriv* writing = uv_req_get_data((uv_req_t*)req);
    MediaProxyPriv* proxy = writing->proxy;

    /* Should remove from queue because there won't be response. */
    if (status < 0) {
        MEDIA_ERR_PROXY(proxy, status);
        if (writing->parcel.chunk->code == MEDIA_PARCEL_SEND_ACK) {
            TAILQ_REMOVE(&proxy->sentq, writing, entry);
            writing->on_receive(proxy->cookie,
                writing->cookies[0], writing->cookies[1], NULL);
            writing->flags |= MEDIA_MSGFLAG_RESPONSED;
        }
    } else
        proxy->nb_sent++;

    /* Should release wiriting because there won't be response. */
    writing->flags |= MEDIA_MSGFLAG_WRITTEN;
    media_uv_free_writing(writing);
}

static int media_uv_send_writing(MediaProxyPriv* proxy, MediaWritePriv* writing)
{
    int ret;
    uv_buf_t buf = {
        .base = (void*)writing->parcel.chunk,
        .len = MEDIA_PARCEL_HEADER_LEN + writing->parcel.chunk->len
    };

    if (writing->parcel.chunk->code == MEDIA_PARCEL_SEND_ACK) {
        /* Move to sentq, and wait for response. */
        TAILQ_INSERT_TAIL(&proxy->sentq, writing, entry);
        proxy->nb_sentq++;
    } else
        writing->flags |= MEDIA_MSGFLAG_RESPONSED; /* won't be response. */

    MEDIA_DEBUG_PROXY(proxy);
    ret = uv_write(&writing->req, (uv_stream_t*)&proxy->cpipe->handle, &buf, 1,
        media_uv_write_cb);
    if (ret < 0)
        media_uv_write_cb(&writing->req, ret);

    MEDIA_DEBUG_PROXY(proxy);
    return ret;
}

/**
 * @brief Dequeue and free the head of sentq.
 */
static MediaWritePriv* media_uv_dequeue_writing(MediaProxyPriv* proxy)
{
    MediaWritePriv* writing;

    writing = TAILQ_FIRST(&proxy->sentq);
    if (writing) {
        TAILQ_REMOVE(&proxy->sentq, writing, entry);
        proxy->nb_sentq--;
    }

    return writing;
}

/**
 * @brief Write all buffer in pendq, should only call once.
 */
static void media_uv_delivery_writing(MediaProxyPriv* proxy)
{
    MediaWritePriv *writing, *tmp;

    TAILQ_FOREACH_SAFE(writing, &proxy->pendq, entry, tmp)
    {
        TAILQ_REMOVE(&proxy->pendq, writing, entry);
        proxy->nb_pendq--;
        media_uv_send_writing(proxy, writing);
    }
}

/**
 * @brief Enqueue writing to pendq or directly write.
 */
static int media_uv_queue_writing(MediaProxyPriv* proxy, MediaWritePriv* writing)
{
    writing->proxy = proxy;

    if (proxy->flags == 0)
        return media_uv_send_writing(proxy, writing);
    else {
        TAILQ_INSERT_TAIL(&proxy->pendq, writing, entry);
        proxy->nb_pendq++;
        return 0;
    }
}

static int media_uv_create_notify(MediaProxyPriv* proxy)
{
    MediaWritePriv* writing;
    char addr[PATH_MAX];

    writing = media_uv_alloc_writing(MEDIA_PARCEL_CREATE_NOTIFY, NULL);
    if (!writing) {
        MEDIA_ERR_PROXY(proxy, -ENOMEM);
        return -ENOMEM;
    }

    snprintf(addr, sizeof(addr), "md_%p", proxy);
    media_parcel_append_printf(&writing->parcel, "%s%s", addr, CONFIG_RPMSG_LOCAL_CPUNAME);
    writing->proxy = proxy;
    return media_uv_send_writing(proxy, writing);
}

static void media_uv_listen_one_cb(uv_stream_t* stream, int ret)
{
    MediaPipePriv* server = uv_handle_get_data((uv_handle_t*)stream);
    MediaProxyPriv* proxy = server->proxy;

    proxy->flags &= ~MEDIA_PROXYFLAG_LISTENING;

    /* Handle error and user's cancelation. */
    if (ret < 0)
        goto err1;

    if (proxy->flags & MEDIA_PROXYFLAG_DISCONNECT) {
        ret = -ECANCELED;
        goto err1;
    }

    /* Prepare client stream and take place of server stream. */
    proxy->epipe = media_uv_alloc_pipe(proxy);
    if (!proxy->epipe)
        goto err1;

    ret = uv_accept(stream, (uv_stream_t*)&proxy->epipe->handle);
    if (ret < 0)
        goto err2;

    media_uv_close(server);
    MEDIA_DEBUG_PROXY(proxy);
    if (proxy->on_listen)
        proxy->on_listen(proxy->cookie, ret);
    media_uv_delivery_writing(proxy);
    uv_read_start((uv_stream_t*)&proxy->epipe->handle, media_uv_alloc_cb, media_uv_read_cb);
    return;

err2:
    media_uv_close(proxy->epipe);

err1:
    media_uv_close(server);
    MEDIA_ERR_PROXY(proxy, ret);
    if (proxy->on_listen)
        proxy->on_listen(proxy->cookie, ret);
}

/**
 * @brief Do real work of creating listener.
 */
static int media_uv_listen_one(MediaProxyPriv* proxy)
{
    char addr[PATH_MAX];
    int ret = -ENOMEM;

    /* Prepare server stream. */
    proxy->epipe = media_uv_alloc_pipe(proxy);
    if (!proxy->epipe)
        goto err1;

    snprintf(addr, sizeof(addr), "md_%p", proxy);
    if (!strcmp(proxy->cpu, CONFIG_RPMSG_LOCAL_CPUNAME))
        ret = uv_pipe_bind(&proxy->epipe->handle, addr);
    else
#ifdef CONFIG_NET_RPMSG
        ret = uv_pipe_rpmsg_bind(&proxy->epipe->handle, addr, proxy->cpu);
#else
        ret = -ENOSYS;
#endif
    if (ret < 0)
        goto err2;

    ret = uv_listen((uv_stream_t*)&proxy->epipe->handle, 1, media_uv_listen_one_cb);
    if (ret < 0)
        goto err2;

    /* Notify media server to back connect. */
    ret = media_uv_create_notify(proxy);
    if (ret < 0)
        goto err2;

    proxy->flags |= MEDIA_PROXYFLAG_LISTENING;
    MEDIA_DEBUG_PROXY(proxy);
    return ret;

err2:
    media_uv_close(proxy->epipe);

err1:
    MEDIA_ERR_PROXY(proxy, ret);
    if (proxy->on_listen)
        proxy->on_listen(proxy->cookie, ret);

    return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void* media_uv_connect(void* loop, const char* cpus,
    media_uv_callback on_connect, void* cookie)
{
    MediaProxyPriv* proxy;
    int ret = -ENOMEM;

    if (!cpus || !loop || !on_connect) {
        ret = -EINVAL;
        goto err1;
    }

    proxy = zalloc(sizeof(MediaProxyPriv) + strlen(cpus) + 1);
    if (!proxy)
        goto err1;

    proxy->loop = loop;
    proxy->cookie = cookie;
    proxy->on_connect = on_connect;
    TAILQ_INIT(&proxy->pendq);
    TAILQ_INIT(&proxy->sentq);
    proxy->cpus = (char*)(proxy + 1);
    strcpy(proxy->cpus, cpus);
    proxy->cpu = strtok_r(proxy->cpus, MEDIA_CPU_DELIM, &proxy->cpus);
    if (!proxy->cpu)
        goto err2;

    MEDIA_DEBUG("%s:%p cookie:%p\n", cpus, proxy, cookie);
    ret = media_uv_connect_one(proxy);
    if (ret < 0)
        goto err2;

    return proxy;

err2:
    free(proxy);

err1:
    MEDIA_ERR("err:%d\n", ret);
    return NULL;
}

int media_uv_disconnect(void* handle, media_uv_callback on_release)
{
    MediaProxyPriv* proxy = handle;

    if (!proxy || (proxy->flags & MEDIA_PROXYFLAG_DISCONNECT))
        return -EINVAL;

    proxy->on_release = on_release;

    if (proxy->flags == 0) /* Needn't shutdown command socket if not ready. */
        media_uv_shutdown(proxy->cpipe);

    proxy->flags |= MEDIA_PROXYFLAG_DISCONNECT;
    media_uv_free_proxy(proxy); /* Do real work if uv_pipes already closed. */
    return 0;
}

int media_uv_reconnect(void* handle)
{
    MediaProxyPriv* proxy = handle;

    if (!proxy || proxy->epipe)
        return -EINVAL; /* Cannot reconnect if listener created. */

    if (proxy->flags == 0)
        media_uv_reconnect_one(proxy);
    else
        proxy->flags |= MEDIA_PROXYFLAG_RECONNECT;

    /* Notify reconnect result by on_connect callback. */
    return 0;
}

int media_uv_listen(void* handle, media_uv_callback on_listen,
    media_uv_parcel_callback on_event)
{
    MediaProxyPriv* proxy = handle;
    int ret = 0;

    if (!proxy || !on_event)
        return -EINVAL;

    if ((proxy->flags & MEDIA_PROXYFLAG_LISTENING) || proxy->epipe) {
        ret = -EPERM; /* Cannot create another listener. */
        goto err;
    }

    proxy->on_listen = on_listen;
    proxy->on_event = on_event;

    /* Directly do real work if proxy is working. */
    if (proxy->flags == 0)
        ret = media_uv_listen_one(proxy);
    else /* Set flag and do nothing if there are reconnect/disconnect to do. */
        proxy->flags |= MEDIA_PROXYFLAG_LISTENING;

    return ret;

err:
    MEDIA_ERR_PROXY(proxy, ret);
    return ret;
}

int media_uv_send(void* handle, media_uv_parcel_callback on_receive,
    void* cookie0, void* cookie1, const media_parcel* parcel)
{
    MediaProxyPriv* proxy = handle;
    MediaWritePriv* writing;
    int ret;

    if (!proxy)
        return -EINVAL;

    if (!proxy->cpu) {
        ret = -ECANCELED;
        goto err;
    }

    if (proxy->flags & MEDIA_PROXYFLAG_DISCONNECT) {
        ret = -EPERM;
        goto err;
    }

    if (!on_receive || !cookie0)
        writing = media_uv_alloc_writing(MEDIA_PARCEL_SEND, parcel);
    else
        writing = media_uv_alloc_writing(MEDIA_PARCEL_SEND_ACK, parcel);
    if (!writing) {
        ret = -ENOMEM;
        goto err;
    }

    writing->on_receive = on_receive;
    writing->cookies[0] = cookie0;
    writing->cookies[1] = cookie1;
    return media_uv_queue_writing(proxy, writing);

err:
    MEDIA_ERR_PROXY(proxy, ret);
    return ret;
}
