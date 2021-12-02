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

#include <stdlib.h>
#include <poll.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <netpacket/rpmsg.h>
#include <sys/un.h>

#include "media_server.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MEDIA_SERVER_MAXCONN      10

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct media_server_conn {
    int              tran_fd;
    int              notify_fd;
    media_parcel     parcel;
    uint32_t         offset;
};

struct media_server_priv {
    int                        local_fd;
    int                        rpmsg_fd;
    media_server_onreceive     onreceive;
    struct media_server_conn   conns[MEDIA_SERVER_MAXCONN];
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int media_server_create_notify(struct media_server_priv *priv, media_parcel *parcel)
{
    struct sockaddr_un local_addr;
    struct sockaddr_rpmsg rpmsg_addr;
    struct sockaddr *addr;
    const char *key;
    const char *rp_cpu;
    int fd;
    int family;
    int len;
    int ret;

    key = media_parcel_read_string(parcel);
    rp_cpu = media_parcel_read_string(parcel);

    if (key == NULL)
        return -EINVAL;

    if (rp_cpu) {
        family = AF_RPMSG;
        rpmsg_addr.rp_family = AF_RPMSG;
        strcpy(rpmsg_addr.rp_name, key);
        strcpy(rpmsg_addr.rp_cpu, rp_cpu);
        addr = (struct sockaddr *)&rpmsg_addr;
        len = sizeof(struct sockaddr_rpmsg);
    } else {
        family = PF_LOCAL;
        local_addr.sun_family = AF_LOCAL;
        strcpy(local_addr.sun_path, key);
        addr = (struct sockaddr *)&local_addr;
        len = sizeof(struct sockaddr_un);
    }

    fd = socket(family, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd <= 0)
        return -errno;

    ret = connect(fd, addr, len);
    if (ret < 0 && errno != EINPROGRESS) {
        close(fd);
        return -errno;
    }

    return fd;
}

static int media_server_receive(void *handle, struct pollfd *fd, struct media_server_conn *conn)
{
    struct media_server_priv *priv = handle;
    media_parcel ack;
    uint32_t code;
    int ret;

    if (priv == NULL || fd->fd <= 0)
        return -EINVAL;

    if (fd->revents == POLLERR || fd->revents == POLLHUP) {
        close(fd->fd);
        if (conn->notify_fd > 0)
            close(conn->notify_fd);
        media_parcel_deinit(&conn->parcel);
        memset(conn, 0, sizeof(*conn));
        return 0;
    }

    ret = media_parcel_recv(&conn->parcel, fd->fd, &conn->offset);
    if (ret < 0)
        return ret;

    code = media_parcel_get_code(&conn->parcel);
    switch (code) {
    case MEDIA_PARCEL_SEND:
        priv->onreceive(conn, &conn->parcel, NULL);
        break;

    case MEDIA_PARCEL_SEND_ACK:
        media_parcel_init(&ack);
        priv->onreceive(conn, &conn->parcel, &ack);
        ret = media_parcel_send(&ack, fd->fd, MEDIA_PARCEL_REPLY);
        media_parcel_deinit(&ack);
        break;

    case MEDIA_PARCEL_CREATE_NOTIFY:
        conn->notify_fd = media_server_create_notify(priv, &conn->parcel);
        break;

    default:
        break;
    }

    conn->offset = 0;
    media_parcel_reinit(&conn->parcel);
    return ret;
}

static int media_server_accept(void *handle, struct pollfd *fd)
{
    struct media_server_priv *priv = handle;
    int new_fd;
    int i;
    char ack = 0;

    if (priv == NULL || fd->fd <= 0 || fd->revents == 0)
        return -EINVAL;

    new_fd = accept(fd->fd, NULL, NULL);
    if (new_fd <= 0)
        return -errno;

    for (i = 0; i < MEDIA_SERVER_MAXCONN; i++) {
        if (priv->conns[i].tran_fd <= 0 && send(new_fd, &ack, sizeof(ack), 0) > 0) {
            priv->conns[i].tran_fd = new_fd;
            media_parcel_init(&priv->conns[i].parcel);
            return 0;
        }
    }

    ack = -1;
    send(new_fd, &ack, sizeof(ack), 0);
    close(new_fd);
    return -EMFILE;
}

static int media_server_listen(struct media_server_priv *priv, int family)
{
    struct sockaddr *addr;
    struct sockaddr_un local_addr;
    struct sockaddr_rpmsg rpmsg_addr;
    int fd;
    int len;

    fd = socket(family, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd <= 0)
        return -errno;

    if (family == PF_LOCAL) {
        priv->local_fd = fd;
        local_addr.sun_family = AF_LOCAL;
        strcpy(local_addr.sun_path, "mediad");
        addr = (struct sockaddr *)&local_addr;
        len = sizeof(struct sockaddr_un);
    } else {
        priv->rpmsg_fd = fd;
        rpmsg_addr.rp_family = AF_RPMSG;
        strcpy(rpmsg_addr.rp_name, "mediad");
        strcpy(rpmsg_addr.rp_cpu, "");
        addr = (struct sockaddr *)&rpmsg_addr;
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

void *media_server_create(void *cb)
{
    struct media_server_priv *priv;
    int ret1, ret2;

    if (cb == NULL)
        return NULL;

    priv = zalloc(sizeof(struct media_server_priv));
    if (priv == NULL)
        return NULL;

    ret1 = media_server_listen(priv, PF_LOCAL);
    ret2 = media_server_listen(priv, AF_RPMSG);
    if (ret1 < 0 && ret2 < 0) {
        media_server_destroy(priv);
        return NULL;
    }
    priv->onreceive = cb;
    return priv;
}

int media_server_destroy(void *handle)
{
    struct media_server_priv *priv = handle;
    int i;

    if (priv == NULL)
        return -EINVAL;

    if (priv->local_fd > 0)
        close(priv->local_fd);
    if(priv->rpmsg_fd > 0)
        close(priv->rpmsg_fd);

    for (i = 0; i < MEDIA_SERVER_MAXCONN; i++) {
        if (priv->conns[i].tran_fd > 0) {
            media_parcel_deinit(&priv->conns[i].parcel);
            close(priv->conns[i].tran_fd);
        }
        if (priv->conns[i].notify_fd > 0)
            close(priv->conns[i].notify_fd);
    }

    free(priv);
    return 0;
}

int media_server_get_pollfds(void *handle, struct pollfd *fds, void **conns, int count)
{
    struct media_server_priv *priv = handle;
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

int media_server_poll_available(void *handle, struct pollfd *fd, void *conn)
{
    if (fd == NULL)
        return -EINVAL;

    if (conn)
        return media_server_receive(handle, fd, conn);
    else
        return media_server_accept(handle, fd);
}

int media_server_notify(void *handle, void *cookie, media_parcel *parcel)
{
    struct media_server_priv *priv = handle;
    struct media_server_conn *conn = cookie;

    if (priv == NULL || conn == NULL || conn->notify_fd <= 0)
        return -EINVAL;

    return media_parcel_send(parcel, conn->notify_fd, MEDIA_PARCEL_NOTIFY);
}
