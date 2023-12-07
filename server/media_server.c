/****************************************************************************
 * frameworks/media/server/media_server.c
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

#include <errno.h>
#include <netinet/in.h>
#include <netpacket/rpmsg.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "media_log.h"
#include "media_proxy.h"
#include "media_server.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MEDIA_SERVER_MAXCONN 10
#define SAFE_CLOSE(fd) \
    do {               \
        if (fd > 0) {  \
            close(fd); \
            fd = 0;    \
        }              \
    } while (0)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct media_server_conn {
    int tran_fd;
    int notify_fd;
    media_parcel parcel;
    uint32_t offset;
    pthread_mutex_t mutex;
    void* data;
};

struct media_server_priv {
    int local_fd;
    int rpmsg_fd;
#if CONFIG_MEDIA_SERVER_PORT >= 0
    int inet_fd;
#endif
    media_server_onreceive onreceive;
    struct media_server_conn conns[MEDIA_SERVER_MAXCONN];
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int media_server_create_notify(struct media_server_priv* priv, media_parcel* parcel)
{
    struct sockaddr_un local_addr;
    struct sockaddr_rpmsg rpmsg_addr;
    struct sockaddr* addr;
    const char* key;
    const char* cpu;
    int fd;
    int family;
    int len;
    int ret;

    key = media_parcel_read_string(parcel);
    cpu = media_parcel_read_string(parcel);

    if (key == NULL)
        return -EINVAL;

    if (strcmp(cpu, CONFIG_RPTUN_LOCAL_CPUNAME)) {
        family = AF_RPMSG;
        rpmsg_addr.rp_family = AF_RPMSG;
        strlcpy(rpmsg_addr.rp_name, key, RPMSG_SOCKET_NAME_SIZE);
        strlcpy(rpmsg_addr.rp_cpu, cpu, RPMSG_SOCKET_CPU_SIZE);
        addr = (struct sockaddr*)&rpmsg_addr;
        len = sizeof(struct sockaddr_rpmsg);
    } else {
        family = PF_LOCAL;
        local_addr.sun_family = AF_LOCAL;
        strlcpy(local_addr.sun_path, key, UNIX_PATH_MAX);
        addr = (struct sockaddr*)&local_addr;
        len = sizeof(struct sockaddr_un);
    }

    fd = socket(family, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -errno;

    ret = connect(fd, addr, len);
    if (ret < 0) {
        close(fd);
        return -errno;
    }

    return fd;
}

static void media_server_conn_close(struct media_server_conn* conn)
{
    close(conn->tran_fd);
    conn->tran_fd = -EPERM;
    conn->offset = 0;
    media_parcel_deinit(&conn->parcel);
}

static int media_server_receive(void* handle, struct pollfd* fd, struct media_server_conn* conn)
{
    struct media_server_priv* priv = handle;
    media_parcel ack;
    uint32_t code;
    int ret;

    if (priv == NULL || fd->fd <= 0)
        return -EINVAL;

    if (fd->revents & POLLERR) {
        MEDIA_DEBUG("fd:%d revent:%d\n", fd->fd, (int)fd->revents);
        media_server_conn_close(conn);
        return 0;
    }

    while (1) {
        conn->offset = 0;
        media_parcel_reinit(&conn->parcel);
        ret = media_parcel_recv(&conn->parcel, fd->fd, &conn->offset, MSG_DONTWAIT);
        if (ret < 0)
            break;

        code = media_parcel_get_code(&conn->parcel);
        switch (code) {
        case MEDIA_PARCEL_SEND:
            priv->onreceive(conn, &conn->parcel, NULL);
            break;

        case MEDIA_PARCEL_SEND_ACK:
            media_parcel_init(&ack);
            priv->onreceive(conn, &conn->parcel, &ack);
            ret = media_parcel_send(&ack, fd->fd, MEDIA_PARCEL_REPLY, 0);
            media_parcel_deinit(&ack);
            break;

        case MEDIA_PARCEL_CREATE_NOTIFY:
            conn->notify_fd = media_server_create_notify(priv, &conn->parcel);
            break;

        default:
            break;
        }
    }

    if (fd->revents & POLLHUP) {
        MEDIA_DEBUG("fd:%d revent:%d\n", fd->fd, (int)fd->revents);
        media_server_conn_close(conn);
        return 0;
    }

    return ret;
}

/**
 * @brief Try to enable an entry for a new connection.
 *
 * There are 3 conditions:
 * 1. busy:      trans/notify socket are all in use.
 * 2. closing:   trans socket has disconnected, while notify socket not.
 * 3. available: trans/notify socket have all been disconnected,
 *               or the entry has not been initialized.
 */
static bool media_server_conn_init(struct media_server_conn* conn, int fd)
{
    bool available;

    if (conn->tran_fd > 0)
        return false;

    if (conn->tran_fd == 0) {
        pthread_mutex_init(&conn->mutex, NULL);
        available = true;
    } else {
        pthread_mutex_lock(&conn->mutex);
        available = conn->notify_fd <= 0;
        pthread_mutex_unlock(&conn->mutex);
    }

    if (available) {
        media_parcel_init(&conn->parcel);
        conn->tran_fd = fd;
        conn->data = NULL;
    }

    return available;
}

static int media_server_accept(void* handle, struct pollfd* fd)
{
    struct media_server_priv* priv = handle;
    int new_fd;
    int i;

    if (priv == NULL || fd->fd <= 0 || fd->revents == 0)
        return -EINVAL;

    new_fd = accept4(fd->fd, NULL, NULL, SOCK_CLOEXEC);
    if (new_fd < 0)
        return -errno;

    for (i = 0; i < MEDIA_SERVER_MAXCONN; i++) {
        if (media_server_conn_init(&priv->conns[i], new_fd))
            return 0;
    }

    close(new_fd);
    return -EMFILE;
}

static int media_server_listen(struct media_server_priv* priv, int family)
{
    struct sockaddr* addr;
    struct sockaddr_un local_addr;
    struct sockaddr_rpmsg rpmsg_addr;
#if CONFIG_MEDIA_SERVER_PORT >= 0
    struct sockaddr_in in_addr;
#endif
    int fd;
    int len;

    fd = socket(family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -errno;

    if (family == PF_LOCAL) {
        priv->local_fd = fd;
        local_addr.sun_family = AF_LOCAL;
        snprintf(local_addr.sun_path, UNIX_PATH_MAX,
            MEDIA_SOCKADDR_NAME, CONFIG_RPTUN_LOCAL_CPUNAME);
        addr = (struct sockaddr*)&local_addr;
        len = sizeof(struct sockaddr_un);
#if CONFIG_MEDIA_SERVER_PORT >= 0
    } else if (family == AF_INET) {
        int opt = 1;
        priv->inet_fd = fd;
        in_addr.sin_family = AF_INET;
        in_addr.sin_addr.s_addr = INADDR_ANY;
        in_addr.sin_port = htons(CONFIG_MEDIA_SERVER_PORT);
        addr = (struct sockaddr*)&in_addr;
        len = sizeof(in_addr);
        setsockopt(priv->inet_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif
    } else {
        priv->rpmsg_fd = fd;
        rpmsg_addr.rp_family = AF_RPMSG;
        snprintf(rpmsg_addr.rp_name, RPMSG_SOCKET_NAME_SIZE,
            MEDIA_SOCKADDR_NAME, CONFIG_RPTUN_LOCAL_CPUNAME);
        strcpy(rpmsg_addr.rp_cpu, "");
        addr = (struct sockaddr*)&rpmsg_addr;
        len = sizeof(struct sockaddr_rpmsg);
    }

    if (bind(fd, addr, len) < 0)
        return -errno;

    if (listen(fd, MEDIA_SERVER_MAXCONN) < 0)
        return -errno;

    return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void* media_server_create(void* cb)
{
    struct media_server_priv* priv;
    int ret1 = -1, ret2 = -1, ret3 = -1;

    if (cb == NULL)
        return NULL;

    priv = zalloc(sizeof(struct media_server_priv));
    if (priv == NULL)
        return NULL;

    ret1 = media_server_listen(priv, PF_LOCAL);
    ret2 = media_server_listen(priv, AF_RPMSG);
#if CONFIG_MEDIA_SERVER_PORT >= 0
    ret3 = media_server_listen(priv, AF_INET);
#endif
    if (ret1 < 0 && ret2 < 0 && ret3 < 0) {
        media_server_destroy(priv);
        return NULL;
    }
    priv->onreceive = cb;
    return priv;
}

int media_server_destroy(void* handle)
{
    struct media_server_priv* priv = handle;
    int i;

    if (priv == NULL)
        return -EINVAL;

    SAFE_CLOSE(priv->local_fd);
    SAFE_CLOSE(priv->rpmsg_fd);
#if CONFIG_MEDIA_SERVER_PORT >= 0
    SAFE_CLOSE(priv->inet_fd);
#endif

    for (i = 0; i < MEDIA_SERVER_MAXCONN; i++) {
        if (priv->conns[i].tran_fd > 0) {
            media_parcel_deinit(&priv->conns[i].parcel);
            close(priv->conns[i].tran_fd);
            pthread_mutex_destroy(&priv->conns[i].mutex);
        }
        if (priv->conns[i].notify_fd > 0)
            close(priv->conns[i].notify_fd);
    }

    free(priv);
    return 0;
}

int media_server_get_pollfds(void* handle, struct pollfd* fds, void** conns, int count)
{
    struct media_server_priv* priv = handle;
    int i = 0;
    int j;

    if (priv == NULL || fds == NULL || count <= 2)
        return -EINVAL;

    if (priv->local_fd > 0) {
        fds[i].fd = priv->local_fd;
        fds[i].events = POLLIN;
        conns[i++] = NULL;
    }

    if (priv->rpmsg_fd > 0) {
        fds[i].fd = priv->rpmsg_fd;
        fds[i].events = POLLIN;
        conns[i++] = NULL;
    }
#if CONFIG_MEDIA_SERVER_PORT >= 0
    if (priv->inet_fd > 0) {
        fds[i].fd = priv->inet_fd;
        fds[i].events = POLLIN;
        conns[i++] = NULL;
    }
#endif
    for (j = 0; j < MEDIA_SERVER_MAXCONN; j++) {
        if (priv->conns[j].tran_fd > 0) {
            fds[i].fd = priv->conns[j].tran_fd;
            fds[i].events = POLLIN;
            conns[i] = &priv->conns[j];
            if (++i > count)
                return -EINVAL;
        }
    }

    return i;
}

int media_server_poll_available(void* handle, struct pollfd* fd, void* conn)
{
    if (fd == NULL)
        return -EINVAL;

    if (conn)
        return media_server_receive(handle, fd, conn);
    else
        return media_server_accept(handle, fd);
}

int media_server_notify(void* handle, void* cookie, media_parcel* parcel)
{
    struct media_server_priv* priv = handle;
    struct media_server_conn* conn = cookie;
    int ret = -EINVAL;

    if (priv == NULL || conn == NULL)
        return ret;

    pthread_mutex_lock(&conn->mutex);

    if (conn->notify_fd > 0)
        ret = media_parcel_send(parcel, conn->notify_fd,
            MEDIA_PARCEL_NOTIFY, MSG_DONTWAIT);

    pthread_mutex_unlock(&conn->mutex);

    return ret;
}

void media_server_finalize(void* handle, void* cookie)
{
    struct media_server_priv* priv = handle;
    struct media_server_conn* conn = cookie;

    if (priv == NULL || conn == NULL)
        return;

    pthread_mutex_lock(&conn->mutex);

    if (conn->notify_fd > 0) {
        close(conn->notify_fd);
        conn->notify_fd = 0;
    }

    pthread_mutex_unlock(&conn->mutex);
}

void media_server_set_data(void* cookie, void* data)
{
    struct media_server_conn* conn = cookie;

    if (conn != NULL)
        conn->data = data;
}

void* media_server_get_data(void* cookie)
{
    struct media_server_conn* conn = cookie;

    return conn ? conn->data : NULL;
}
