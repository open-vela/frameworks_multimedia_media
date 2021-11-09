/****************************************************************************
 * frameworks/media/media_client.c
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

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netpacket/rpmsg.h>
#include <sys/un.h>

#include "media_client.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct media_client_priv {
    int                   fd;
    void                  *cookie;
    media_client_event_cb event_cb;
    pthread_t             thread;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/


static void media_client_get_sockaddr(int *family, socklen_t *len, void *addr, char *key)
{
#ifdef CONFIG_MEDIA_SERVER
    struct sockaddr_un *un_addr = addr;
    *family = PF_LOCAL;
    *len = sizeof(struct sockaddr_un);
    un_addr->sun_family = AF_LOCAL;
    strcpy(un_addr->sun_path, key);
#else
    struct sockaddr_rpmsg *rp_addr = addr;
    *family = AF_RPMSG;
    *len = sizeof(struct sockaddr_rpmsg);
    rp_addr->rp_family = AF_RPMSG;
    strcpy(rp_addr->rp_name, key);
    strcpy(rp_addr->rp_cpu, CONFIG_MEDIA_SERVER_CPUNAME);
#endif
}

static void *media_client_listen_thread(pthread_addr_t pvarg)
{
    struct media_client_priv *priv = pvarg;
    media_parcel parcel;
#ifdef CONFIG_MEDIA_SERVER
    struct sockaddr_un addr;
#else
    struct sockaddr_rpmsg addr;
#endif
    socklen_t socklen;
    char key[16];
    uint32_t code;
    int acceptfd;
    int listenfd;
    int family;
    int ret;

    sprintf(key, "md_%p", priv);
    media_client_get_sockaddr(&family, &socklen, &addr, key);
    listenfd = socket(family, SOCK_STREAM, 0);
    if (listenfd <= 0)
        return NULL;

    if (bind(listenfd, (struct sockaddr *)&addr, socklen) < 0)
        goto thread_error;

    if (listen(listenfd, 2) < 0)
        goto thread_error;

    media_parcel_init(&parcel);
    media_parcel_append_string(&parcel, key);
#ifdef CONFIG_MEDIA_SERVER
    media_parcel_append_string(&parcel, "");
#else
    getsockname(listenfd, (struct sockaddr *)&addr, &socklen);
    media_parcel_append_string(&parcel, addr.rp_cpu);
#endif
    ret = media_parcel_send(&parcel, priv->fd, MEDIA_PARCEL_CREATE_NOTIFY);
    media_parcel_deinit(&parcel);
    if(ret < 0)
        goto thread_error;

    acceptfd = accept(listenfd, NULL, NULL);
    if (acceptfd <= 0)
        goto thread_error;

    while (1) {
        media_parcel_init(&parcel);
        ret = media_parcel_recv(&parcel, acceptfd, NULL);
        if (ret < 0)
            break;

        code = media_parcel_get_code(&parcel);
        if (code != MEDIA_PARCEL_NOTIFY)
            break;

        priv->event_cb(priv->cookie, &parcel);
        media_parcel_deinit(&parcel);
    }

    media_parcel_deinit(&parcel);
    close(acceptfd);
thread_error:
    close(listenfd);
    return NULL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void *media_client_connect(void)
{
    struct media_client_priv *priv;
#ifdef CONFIG_MEDIA_SERVER
    struct sockaddr_un addr;
#else
    struct sockaddr_rpmsg addr;
#endif
    socklen_t len;
    int family;

    priv = zalloc(sizeof(struct media_client_priv));
    if (priv == NULL)
        return NULL;

    media_client_get_sockaddr(&family, &len, &addr, "mediad");
    priv->fd = socket(family, SOCK_STREAM, 0);
    if (priv->fd <= 0)
        goto socket_error;

    if (connect(priv->fd, (struct sockaddr *)&addr, len) < 0)
        goto connect_error;

    return priv;

connect_error:
    close(priv->fd);
socket_error:
    free(priv);
    return NULL;
}

int media_client_disconnect(void *handle)
{
    struct media_client_priv *priv = handle;

    if (priv == NULL)
        return -EINVAL;

    if (priv->fd > 0)
        close(priv->fd);
    if(priv->thread > 0)
        pthread_join(priv->thread, NULL);
    free(priv);
    return 0;
}

int media_client_send(void *handle, media_parcel *in)
{
    struct media_client_priv *priv = handle;

    if (priv == NULL || in == NULL)
        return -EINVAL;

    return media_parcel_send(in, priv->fd, MEDIA_PARCEL_SEND);
}

int media_client_send_with_ack(void *handle, media_parcel *in, media_parcel *out)
{
    struct media_client_priv *priv = handle;
    int ret;

    if (priv == NULL || in == NULL || out == NULL)
        return -EINVAL;

    ret = media_parcel_send(in, priv->fd, MEDIA_PARCEL_SEND_ACK);
    if (ret < 0)
        return ret;

    ret = media_parcel_recv(out, priv->fd, NULL);
    if (ret < 0)
        return ret;

    if (media_parcel_get_code(out) != MEDIA_PARCEL_REPLY)
        return -EIO;
    return ret;
}

int media_client_set_event_cb(void *handle, void *event_cb, void *cookie)
{
    struct media_client_priv *priv = handle;
    if (priv == NULL)
        return -EINVAL;

    priv->event_cb = event_cb;
    priv->cookie = cookie;
    if (priv->thread > 0)
        return 0;
    return pthread_create(&priv->thread, NULL, media_client_listen_thread, (pthread_addr_t)priv);
}