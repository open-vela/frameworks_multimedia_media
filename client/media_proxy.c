/****************************************************************************
 * frameworks/media/client/media_proxy.c
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

#include <assert.h>
#include <netpacket/rpmsg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "media_common.h"
#include "media_proxy.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct MediaClientPriv {
    int fd;
    int listenfd;
    pthread_t thread;
    pthread_mutex_t mutex;
    void* event_cookie;
    void* release_cookie;
    media_proxy_event_cb event_cb;
    media_proxy_release_cb release_cb;
    atomic_int refs;
} MediaClientPriv;

typedef struct MediaProxyPriv {
    MEDIA_COMMON_FIELDS
} MediaProxyPriv;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void media_proxy_ref(void* handle)
{
    MediaClientPriv* priv = handle;

    atomic_fetch_add(&priv->refs, 1);
}

static void media_proxy_unref(void* handle)
{
    MediaClientPriv* priv = handle;

    if (atomic_fetch_sub(&priv->refs, 1) == 1) {
        if (priv->release_cb)
            priv->release_cb(priv->release_cookie);

        pthread_mutex_destroy(&priv->mutex);
        free(priv);
    }
}

static void media_proxy_get_sockaddr(int* family, socklen_t* len, void* addr,
    const char* cpu, const char* key)
{
    if (!strcmp(cpu, CONFIG_RPMSG_LOCAL_CPUNAME)) {
        struct sockaddr_un* un_addr = addr;

        *family = PF_LOCAL;
        *len = sizeof(struct sockaddr_un);
        un_addr->sun_family = AF_LOCAL;
        strlcpy(un_addr->sun_path, key, UNIX_PATH_MAX);
    } else {
        struct sockaddr_rpmsg* rp_addr = addr;

        *family = AF_RPMSG;
        *len = sizeof(struct sockaddr_rpmsg);
        rp_addr->rp_family = AF_RPMSG;
        strlcpy(rp_addr->rp_name, key, RPMSG_SOCKET_NAME_SIZE);
        strlcpy(rp_addr->rp_cpu, cpu, RPMSG_SOCKET_CPU_SIZE);
    }
}

static int media_proxy_create_listenfd(MediaClientPriv* priv, const char* cpu)
{
    struct sockaddr_storage addr;
    socklen_t socklen;
    media_parcel parcel;
    char key[32];
    int family;
    int ret;

    snprintf(key, sizeof(key), "md_%p", priv);
    media_proxy_get_sockaddr(&family, &socklen, &addr, cpu, key);

    priv->listenfd = socket(family, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (priv->listenfd < 0)
        return priv->listenfd;

    media_parcel_init(&parcel);

    ret = bind(priv->listenfd, (struct sockaddr*)&addr, socklen);
    if (ret < 0)
        goto err;

    ret = listen(priv->listenfd, 2);
    if (ret < 0)
        goto err;

    media_parcel_append_string(&parcel, key);
    media_parcel_append_string(&parcel, CONFIG_RPMSG_LOCAL_CPUNAME);
    ret = media_parcel_send(&parcel, priv->fd, MEDIA_PARCEL_CREATE_NOTIFY, 0);

err:
    media_parcel_deinit(&parcel);
    if (ret < 0)
        close(priv->listenfd);
    return ret;
}

static void* media_proxy_listen_thread(pthread_addr_t pvarg)
{
    MediaClientPriv* priv = pvarg;
    media_parcel parcel;
    uint32_t code;
    int acceptfd;
    int ret;

    acceptfd = accept4(priv->listenfd, NULL, NULL, SOCK_CLOEXEC);
    if (acceptfd <= 0)
        goto thread_error;

    while (1) {
        media_parcel_init(&parcel);
        ret = media_parcel_recv(&parcel, acceptfd, NULL, 0);
        if (ret == -EINTR)
            continue;
        else if (ret < 0)
            break;

        code = media_parcel_get_code(&parcel);
        if (code != MEDIA_PARCEL_NOTIFY)
            break;

        priv->event_cb(priv->event_cookie, &parcel);
        media_parcel_deinit(&parcel);
    }

    media_parcel_deinit(&parcel);
    close(acceptfd);

thread_error:
    close(priv->listenfd);
    media_proxy_unref(priv);
    return NULL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void* media_proxy_connect(const char* cpu)
{
    MediaClientPriv* priv;
    struct sockaddr_storage addr;
    socklen_t len;
    char key[32];
    int family;

    priv = zalloc(sizeof(MediaClientPriv));
    if (priv == NULL)
        return NULL;

    snprintf(key, sizeof(key), MEDIA_SOCKADDR_NAME, cpu);
    media_proxy_get_sockaddr(&family, &len, &addr, cpu, key);
    priv->fd = socket(family, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (priv->fd <= 0)
        goto socket_error;

    if (connect(priv->fd, (struct sockaddr*)&addr, len) < 0)
        goto connect_error;

    pthread_mutex_init(&priv->mutex, NULL);
    atomic_store(&priv->refs, 1);
    return priv;

connect_error:
    close(priv->fd);
socket_error:
    free(priv);
    return NULL;
}

int media_proxy_disconnect(void* handle)
{
    MediaClientPriv* priv = handle;
    int ret;

    if (priv == NULL)
        return -EINVAL;

    ret = pthread_join(priv->thread, NULL);
    if (ret == EDEADLK)
        pthread_detach(priv->thread);

    if (priv->fd > 0) {
        close(priv->fd);
        priv->fd = 0;
        media_proxy_unref(handle);
    }

    return 0;
}

int media_proxy_send(void* handle, media_parcel* in)
{
    MediaClientPriv* priv = handle;
    int ret;

    if (priv == NULL || in == NULL)
        return -EINVAL;

    pthread_mutex_lock(&priv->mutex);

    ret = media_parcel_send(in, priv->fd, MEDIA_PARCEL_SEND, 0);

    pthread_mutex_unlock(&priv->mutex);
    return ret;
}

int media_proxy_send_with_ack(void* handle, media_parcel* in, media_parcel* out)
{
    MediaClientPriv* priv = handle;
    int ret;

    if (priv == NULL || in == NULL || out == NULL)
        return -EINVAL;

    pthread_mutex_lock(&priv->mutex);

    ret = media_parcel_send(in, priv->fd, MEDIA_PARCEL_SEND_ACK, 0);
    if (ret < 0)
        goto out;

    ret = media_parcel_recv(out, priv->fd, NULL, 0);
    if (ret < 0)
        goto out;

    if (media_parcel_get_code(out) != MEDIA_PARCEL_REPLY) {
        ret = -EIO;
        goto out;
    }
out:
    pthread_mutex_unlock(&priv->mutex);
    return ret;
}

int media_proxy_set_event_cb(void* handle, const char* cpu, void* event_cb, void* cookie)
{
    MediaClientPriv* priv = handle;
    struct sched_param param;
    pthread_attr_t pattr;
    int ret;

    if (priv == NULL || event_cb == NULL)
        return -EINVAL;

    pthread_mutex_lock(&priv->mutex);

    priv->event_cb = event_cb;
    priv->event_cookie = cookie;
    if (priv->thread > 0) {
        pthread_mutex_unlock(&priv->mutex);
        return 0;
    }

    ret = media_proxy_create_listenfd(priv, cpu);
    if (ret < 0) {
        pthread_mutex_unlock(&priv->mutex);
        return ret;
    }

    media_proxy_ref(handle);

    pthread_attr_init(&pattr);
    pthread_attr_setstacksize(&pattr, CONFIG_MEDIA_PROXY_LISTEN_STACKSIZE);
    param.sched_priority = CONFIG_MEDIA_PROXY_LISTEN_PRIORITY;
    pthread_attr_setschedparam(&pattr, &param);
    ret = pthread_create(&priv->thread, &pattr, media_proxy_listen_thread, (pthread_addr_t)priv);
    if (ret < 0) {
        close(priv->listenfd);
        media_proxy_unref(handle);
    }
    pthread_setname_np(priv->thread, "proxy_listen");
    pthread_attr_destroy(&pattr);
    pthread_mutex_unlock(&priv->mutex);

    return ret;
}

int media_proxy_set_release_cb(void* handle, void* release_cb, void* cookie)
{
    MediaClientPriv* priv = handle;

    if (priv == NULL || release_cb == NULL)
        return -EINVAL;

    priv->release_cb = release_cb;
    priv->release_cookie = cookie;

    return 0;
}

int media_proxy_send_recieve(void* handle, const char* in_fmt, const char* out_fmt, ...)
{
    media_parcel in, out;
    va_list ap;
    int ret;

    media_parcel_init(&in);
    media_parcel_init(&out);

    va_start(ap, out_fmt);
    ret = media_parcel_append_vprintf(&in, in_fmt, &ap);
    if (ret < 0)
        goto out;

    ret = media_proxy_send_with_ack(handle, &in, &out);
    if (ret < 0)
        goto out;

    ret = media_parcel_read_vscanf(&out, out_fmt, &ap);

out:
    media_parcel_deinit(&in);
    media_parcel_deinit(&out);

    va_end(ap);
    return ret;
}

int media_proxy_once(void* handle, const char* target, const char* cmd,
    const char* arg, int apply, char* res, int res_len)
{
    MediaProxyPriv* priv = handle;
    media_parcel in, out;
    const char* response;
    int ret = -EINVAL;
    int32_t resp = 0;

    if (!priv || !priv->proxy)
        return ret;

    media_parcel_init(&in);
    media_parcel_init(&out);

    switch (priv->type) {
    case MEDIA_ID_FOCUS:
        ret = media_parcel_append_printf(&in, "%i%s%s%i", priv->type,
            target, cmd, res_len);
        break;

    case MEDIA_ID_GRAPH:
        ret = media_parcel_append_printf(&in, "%i%s%s%s%i", priv->type,
            target, cmd, arg, res_len);
        break;

    case MEDIA_ID_POLICY:
        ret = media_parcel_append_printf(&in, "%i%s%s%s%i%i", priv->type,
            target, cmd, arg, apply, res_len);
        break;

    case MEDIA_ID_PLAYER:
        ret = media_parcel_append_printf(&in, "%i%s%s%s%i", priv->type,
            target, cmd, arg, res_len);
        break;

    case MEDIA_ID_RECORDER:
        ret = media_parcel_append_printf(&in, "%i%s%s%s%i", priv->type,
            target, cmd, arg, res_len);
        break;

    case MEDIA_ID_SESSION:
        ret = media_parcel_append_printf(&in, "%i%s%s%s%i", priv->type,
            target, cmd, arg, res_len);
        break;

    default:
        goto out;
    }

    if (ret < 0)
        goto out;

    ret = media_proxy_send_with_ack(priv->proxy, &in, &out);
    if (ret < 0)
        goto out;

    ret = media_parcel_read_scanf(&out, "%i%s", &resp, &response);
    if (ret < 0 || resp < 0)
        goto out;

    if (res_len > 0)
        strlcpy(res, response, res_len);
    else if (response && strlen(response) > 0)
        MEDIA_INFO("\n%s\n", response);

out:
    media_parcel_deinit(&in);
    media_parcel_deinit(&out);

    MEDIA_INFO("%s:%s:%p %s %s %s %s ret:%d resp:%d\n",
        media_id_get_name(priv->type), priv->cpu, (void*)(uintptr_t)priv, target ? target : "_",
        cmd, arg ? arg : "_", apply ? "apply" : "_", ret, (int)resp);
    return ret < 0 ? ret : resp;
}

int media_proxy(int id, void* handle, const char* target, const char* cmd,
    const char* arg, int apply, char* res, int res_len)
{
    MediaProxyPriv *priv, priv_ = { 0 };
    char *saveptr, *cpu;
    int ret = -ENOSYS;
    char tmp[128];

    priv = handle ? handle : &priv_;
    priv->type = id;

    if (priv->proxy)
        return media_proxy_once(priv, target, cmd, arg, apply, res, res_len);

    strlcpy(tmp, CONFIG_MEDIA_SERVER_CPUNAME, sizeof(tmp));
    for (cpu = strtok_r(tmp, " ,;|", &saveptr); cpu;
         cpu = strtok_r(NULL, " ,;|", &saveptr)) {
        priv->proxy = media_proxy_connect(cpu);
        if (!priv->proxy)
            continue;

        priv->cpu = cpu;
        ret = media_proxy_once(priv, target, cmd, arg, apply, res, res_len);

        /* keep the connection in need. */
        if (handle && ret >= 0) {
            priv->cpu = strdup(cpu);
            DEBUGASSERT(priv->cpu);
            return ret;
        }

        media_proxy_disconnect(priv->proxy);
    }

    priv->proxy = NULL;
    priv->cpu = NULL;
    return ret;
}

void media_default_release_cb(void* handle)
{
    MediaProxyPriv* priv = handle;

    free(priv->cpu);
    free(priv);
}
