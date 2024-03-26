/****************************************************************************
 * frameworks/media/client/media_graph.c
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
#include <media_api.h>
#include <netpacket/rpmsg.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/un.h>
#include <unistd.h>

#include "media_common.h"
#include "media_proxy.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct MediaIOPriv {
    MEDIA_COMMON_FIELDS
    void* cookie;
    media_event_callback event;
    atomic_int refs;
    sem_t sem;
    int socket;
    int result;
} MediaIOPriv;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void media_recorder_take_picture_cb(void* cookie, int event, int result,
    const char* extra)
{
    (void)extra;

    MediaIOPriv* priv = cookie;

    priv->result = result;
    if (result) {
        sem_post(&priv->sem);
        return;
    }

    if (event == MEDIA_EVENT_COMPLETED) {
        media_recorder_finish_picture(priv->cookie);
        sem_post(&priv->sem);
    }
}

/**
 * @brief Player/Recorder release method.
 */
static void media_release_cb(void* handle)
{
    MediaIOPriv* priv = handle;

    if (atomic_fetch_sub(&priv->refs, 1) == 1) {
        free(priv->cpu);
        free(priv);
    }
}

static void* media_open(int control, const char* params)
{
    MediaIOPriv* priv;

    priv = calloc(1, sizeof(MediaIOPriv));
    if (!priv)
        return NULL;

    if (media_proxy(control, priv, NULL, "open", params, 0, NULL, 0) < 0)
        goto err;

    atomic_store(&priv->refs, 1);
    media_proxy_set_release_cb(priv->proxy, media_default_release_cb, priv);
    return priv;

err:
    media_default_release_cb(priv);
    return NULL;
}

static void media_close_socket(void* handle)
{
    MediaIOPriv* priv = handle;

    if (atomic_load(&priv->refs) == 1 && priv->socket) {
        close(priv->socket);
        priv->socket = 0;
    }
}

static int media_close(void* handle, int pending_stop)
{
    MediaIOPriv* priv = handle;
    char tmp[32];
    int ret;

    snprintf(tmp, sizeof(tmp), "%d", pending_stop);
    ret = media_proxy_once(priv, NULL, "close", tmp, 0, NULL, 0);
    if (ret < 0)
        return ret;

    media_close_socket(priv);
    media_proxy_disconnect(priv->proxy);
    return ret;
}

static int media_get_sockaddr(void* handle, struct sockaddr_storage* addr_)
{
    MediaIOPriv* priv = handle;

    if (!handle || !addr_)
        return -EINVAL;

    if (!strcmp(priv->cpu, CONFIG_RPMSG_LOCAL_CPUNAME)) {
        struct sockaddr_un* addr = (struct sockaddr_un*)addr_;

        addr->sun_family = AF_UNIX;
        snprintf(addr->sun_path, UNIX_PATH_MAX, MEDIA_GRAPH_SOCKADDR_NAME, priv);
    } else {
        struct sockaddr_rpmsg* addr = (struct sockaddr_rpmsg*)addr_;

        addr->rp_family = AF_RPMSG;
        snprintf(addr->rp_name, RPMSG_SOCKET_NAME_SIZE, MEDIA_GRAPH_SOCKADDR_NAME, priv);
        snprintf(addr->rp_cpu, RPMSG_SOCKET_CPU_SIZE, "%s", priv->cpu);
    }

    return 0;
}

static int media_bind_socket(void* handle)
{
    struct sockaddr_storage addr;
    int fd, ret;

    ret = media_get_sockaddr(handle, &addr);
    if (ret < 0)
        return ret;

    fd = socket(addr.ss_family, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -errno;

    ret = bind(fd, (const struct sockaddr*)&addr, sizeof(addr));
    if (ret < 0)
        goto out;

    ret = listen(fd, 1);
    if (ret < 0)
        goto out;

    return fd;

out:
    close(fd);
    return -errno;
}

static int media_prepare(void* handle, const char* url, const char* options)
{
    MediaIOPriv* priv = handle;
    const char* cpu = NULL;
    int ret = -EINVAL;
    char addr[32];
    int fd = 0;

    if (!priv || priv->socket > 0)
        return ret;

    if (!url || !url[0]) {
        /* Buffer mode, create listener and send cpuname and sockname. */
        fd = media_bind_socket(handle);
        if (fd < 0)
            return fd;

        cpu = CONFIG_RPMSG_LOCAL_CPUNAME;
        snprintf(addr, sizeof(addr), MEDIA_GRAPH_SOCKADDR_NAME, priv);
        url = addr;
    }

    if (options && options[0] != '\0') {
        ret = media_proxy_once(priv, NULL, "set_options", options, 0, NULL, 0);
        if (ret < 0)
            goto out;
    }

    ret = media_proxy_once(priv, cpu, "prepare", url, 0, NULL, 0);
    if (ret < 0)
        goto out;

    if (fd > 0) {
        priv->socket = accept4(fd, NULL, NULL, SOCK_CLOEXEC);
        if (priv->socket < 0) {
            priv->socket = 0;
            ret = -errno;
        }
    }

out:
    if (fd > 0)
        close(fd);

    return ret;
}

static ssize_t media_process_data(void* handle, bool player,
    void* data, size_t len)
{
    MediaIOPriv* priv = handle;
    int ret = -EINVAL;

    if (!handle || !data || !len)
        return ret;

    atomic_fetch_add(&priv->refs, 1);

    if (priv->socket <= 0)
        goto out;

    if (player) {
        ret = send(priv->socket, data, len, 0);
        if (ret == len)
            goto out;
        else if (ret == 0)
            errno = ECONNRESET;
        else if (ret < 0 && errno == EINTR) {
            ret = -errno;
            goto out;
        }
    } else {
        ret = recv(priv->socket, data, len, 0);
        if (ret > 0)
            goto out;
        else if (ret == 0)
            errno = ECONNRESET;
        else if (ret < 0 && errno == EINTR) {
            ret = -errno;
            goto out;
        }
    }

    close(priv->socket);
    priv->socket = 0;
    ret = -errno;

out:
    media_release_cb(priv);
    return ret;
}

static void media_event_cb(void* cookie, media_parcel* msg)
{
    MediaIOPriv* priv = cookie;
    const char* extra;
    int32_t event;
    int32_t ret;

    if (priv->event) {
        media_parcel_read_scanf(msg, "%i%i%s", &event, &ret, &extra);
        priv->event(priv->cookie, event, ret, extra);
    }
}

static int media_set_event_cb(void* handle, void* cookie,
    media_event_callback event_cb)
{
    MediaIOPriv* priv = handle;
    int ret;

    if (!priv)
        return -EINVAL;

    ret = media_proxy_set_event_cb(priv->proxy, priv->cpu, media_event_cb, priv);
    if (ret < 0)
        return ret;

    ret = media_proxy_once(handle, NULL, "set_event", NULL, 0, NULL, 0);
    if (ret < 0)
        return ret;

    priv->event = event_cb;
    priv->cookie = cookie;

    return ret;
}

static int media_get_socket(void* handle)
{
    MediaIOPriv* priv = handle;

    if (!priv || priv->socket <= 0)
        return -EINVAL;

    return priv->socket;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int media_process_command(const char* target, const char* cmd,
    const char* arg, char* res, int res_len)
{
    return media_proxy(MEDIA_ID_GRAPH, NULL, target, cmd, arg, 0, res, res_len);
}

void media_graph_dump(const char* options)
{
    media_proxy(MEDIA_ID_GRAPH, NULL, NULL, "dump", options, 0, NULL, 0);
}

void* media_player_open(const char* params)
{
    return media_open(MEDIA_ID_PLAYER, params);
}

int media_player_close(void* handle, int pending_stop)
{
    return media_close(handle, pending_stop);
}

int media_player_set_event_callback(void* handle, void* cookie,
    media_event_callback event_cb)
{
    return media_set_event_cb(handle, cookie, event_cb);
}

int media_player_prepare(void* handle, const char* url, const char* options)
{
    return media_prepare(handle, url, options);
}

int media_player_reset(void* handle)
{
    if (!handle)
        return -EINVAL;

    media_close_socket(handle);

    return media_proxy_once(handle, NULL, "reset", NULL, 0, NULL, 0);
}

ssize_t media_player_write_data(void* handle, const void* data, size_t len)
{
    return media_process_data(handle, true, (void*)data, len);
}

int media_player_get_sockaddr(void* handle, struct sockaddr_storage* addr)
{
    return media_get_sockaddr(handle, addr);
}

void media_player_close_socket(void* handle)
{
    media_close_socket(handle);
}

int media_player_get_socket(void* handle)
{
    return media_get_socket(handle);
}

int media_player_start(void* handle)
{
    if (!handle)
        return -EINVAL;

    return media_proxy_once(handle, NULL, "start", NULL, 0, NULL, 0);
}

int media_player_stop(void* handle)
{
    if (!handle)
        return -EINVAL;

    media_close_socket(handle);

    return media_proxy_once(handle, NULL, "stop", NULL, 0, NULL, 0);
}

int media_player_pause(void* handle)
{
    return media_proxy_once(handle, NULL, "pause", NULL, 0, NULL, 0);
}

int media_player_seek(void* handle, unsigned int msec)
{
    char tmp[32];

    snprintf(tmp, sizeof(tmp), "%u", msec);
    return media_proxy_once(handle, NULL, "seek", tmp, 0, NULL, 0);
}

int media_player_set_looping(void* handle, int loop)
{
    char tmp[32];

    snprintf(tmp, sizeof(tmp), "%d", loop);
    return media_proxy_once(handle, NULL, "set_loop", tmp, 0, NULL, 0);
}

int media_player_is_playing(void* handle)
{
    char tmp[32];
    int ret;

    ret = media_proxy_once(handle, NULL, "get_playing", NULL, 0, tmp, sizeof(tmp));
    return ret < 0 ? ret : !!atoi(tmp);
}

int media_player_get_position(void* handle, unsigned int* msec)
{
    char tmp[32];
    int ret;

    if (!msec)
        return -EINVAL;

    ret = media_proxy_once(handle, NULL, "get_position", NULL, 0, tmp, sizeof(tmp));
    if (ret >= 0)
        *msec = strtoul(tmp, NULL, 0);

    return ret;
}

int media_player_get_duration(void* handle, unsigned int* msec)
{
    char tmp[32];
    int ret;

    if (!msec)
        return -EINVAL;

    ret = media_proxy_once(handle, NULL, "get_duration", NULL, 0, tmp, sizeof(tmp));
    if (ret >= 0)
        *msec = strtoul(tmp, NULL, 0);

    return ret;
}

int media_player_get_latency(void* handle, unsigned int* latency)
{
    char tmp[32];
    int ret;

    if (!latency)
        return -EINVAL;

    ret = media_proxy_once(handle, NULL, "get_latency", NULL, 0, tmp, sizeof(tmp));
    if (ret >= 0)
        *latency = strtoul(tmp, NULL, 0);

    return ret;
}

int media_player_set_volume(void* handle, float volume)
{
    char tmp[32];

    snprintf(tmp, sizeof(tmp), "%f", volume);
    return media_proxy_once(handle, NULL, "volume", tmp, 0, NULL, 0);
}

int media_player_get_volume(void* handle, float* volume)
{
    char tmp[32];
    int ret;

    if (!volume)
        return -EINVAL;

    ret = media_proxy_once(handle, NULL, "get_volume", NULL, 0, tmp, sizeof(tmp));
    if (ret >= 0) {
        sscanf(tmp, "vol:%f", volume);
        ret = 0;
    }

    return ret;
}

int media_player_set_property(void* handle, const char* target, const char* key, const char* value)
{
    return media_proxy_once(handle, target, key, value, 0, NULL, 0);
}

int media_player_get_property(void* handle, const char* target, const char* key, char* value, int value_len)
{
    return media_proxy_once(handle, target, key, NULL, 0, value, value_len);
}

void* media_recorder_open(const char* params)
{
    return media_open(MEDIA_ID_RECORDER, params);
}

int media_recorder_close(void* handle)
{
    return media_close(handle, 0);
}

int media_recorder_set_event_callback(void* handle, void* cookie,
    media_event_callback event_cb)
{
    return media_set_event_cb(handle, cookie, event_cb);
}

int media_recorder_prepare(void* handle, const char* url, const char* options)
{
    return media_prepare(handle, url, options);
}

int media_recorder_reset(void* handle)
{
    if (!handle)
        return -EINVAL;

    media_close_socket(handle);

    return media_proxy_once(handle, NULL, "reset", NULL, 0, NULL, 0);
}

ssize_t media_recorder_read_data(void* handle, void* data, size_t len)
{
    return media_process_data(handle, false, data, len);
}

int media_recorder_get_sockaddr(void* handle, struct sockaddr_storage* addr)
{
    return media_get_sockaddr(handle, addr);
}

int media_recorder_get_socket(void* handle)
{
    return media_get_socket(handle);
}

void media_recorder_close_socket(void* handle)
{
    media_close_socket(handle);
}

int media_recorder_start(void* handle)
{
    return media_proxy_once(handle, NULL, "start", NULL, 0, NULL, 0);
}

int media_recorder_pause(void* handle)
{
    return media_proxy_once(handle, NULL, "pause", NULL, 0, NULL, 0);
}

int media_recorder_stop(void* handle)
{
    if (!handle)
        return -EINVAL;

    media_close_socket(handle);

    return media_proxy_once(handle, NULL, "stop", NULL, 0, NULL, 0);
}

int media_recorder_set_property(void* handle, const char* target, const char* key, const char* value)
{
    return media_proxy_once(handle, target, key, value, 0, NULL, 0);
}

int media_recorder_get_property(void* handle, const char* target, const char* key, char* value, int value_len)
{
    return media_proxy_once(handle, target, key, NULL, 0, value, value_len);
}

int media_recorder_take_picture(char* params, char* filename, size_t number)
{
    int ret = 0;
    MediaIOPriv* priv;

    if (!number || number > INT_MAX)
        return -EINVAL;

    priv = calloc(1, sizeof(MediaIOPriv));
    if (!priv)
        return -ENOMEM;

    sem_init(&priv->sem, 0, 0);
    priv->cookie = media_recorder_start_picture(params, filename, number, media_recorder_take_picture_cb, priv);
    if (!priv->cookie) {
        free(priv);
        return -EINVAL;
    }

    sem_wait(&priv->sem);
    sem_destroy(&priv->sem);
    ret = priv->result;

    free(priv);
    return ret;
}

void* media_recorder_start_picture(char* params, char* filename, size_t number,
    media_event_callback event_cb, void* cookie)
{
    char option[32];
    void* handle = NULL;
    int ret;

    if (!number || number > INT_MAX)
        return NULL;

    handle = media_recorder_open(params);
    if (!handle)
        return NULL;

    ret = media_recorder_set_event_callback(handle, cookie, event_cb);
    if (ret < 0)
        goto error;

    snprintf(option, sizeof(option), "total_number=%zu", number);

    ret = media_recorder_prepare(handle, filename, option);
    if (ret < 0)
        goto error;

    ret = media_recorder_start(handle);
    if (ret < 0)
        goto error;

    return handle;

error:
    media_recorder_close(handle);
    return NULL;
}

int media_recorder_finish_picture(void* handle)
{
    return media_recorder_close(handle);
}
