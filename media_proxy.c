/****************************************************************************
 * frameworks/media/media_proxy.c
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
#include <malloc.h>
#include <stdatomic.h>
#include <stdio.h>
#include <syslog.h>
#include <sys/un.h>
#include <netpacket/rpmsg.h>
#include <media_api.h>

#include "media_internal.h"
#include "media_client.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct MediaProxyPriv {
    void                  *proxy;
    uint64_t              handle;
    atomic_int            refs;
    int                   socket;
    void                  *cookie;
    media_event_callback  event;
} MediaProxyPriv;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int media_get_sockaddr(void *handle, struct sockaddr_storage *addr_)
{
    MediaProxyPriv *priv = handle;
#ifdef CONFIG_MEDIA_SERVER
    struct sockaddr_un *addr = (struct sockaddr_un *)addr_;
#else
    struct sockaddr_rpmsg *addr = (struct sockaddr_rpmsg *)addr_;
#endif

    if (!handle || !addr)
        return -EINVAL;

#ifdef CONFIG_MEDIA_SERVER
    addr->sun_family = AF_UNIX;
    snprintf(addr->sun_path, UNIX_PATH_MAX, "med%llx", priv->handle);
#else
    addr->rp_family = AF_RPMSG;
    snprintf(addr->rp_name, RPMSG_SOCKET_NAME_SIZE, "med%llx", priv->handle);
    snprintf(addr->rp_cpu, RPMSG_SOCKET_CPU_SIZE, CONFIG_MEDIA_SERVER_CPUNAME);
#endif

    return 0;
}

static int media_bind_socket(void *handle, char *url, size_t len)
{
    MediaProxyPriv *priv = handle;
    struct sockaddr_storage addr;
    int fd, ret;

    ret = media_get_sockaddr(handle, &addr);
    if (ret < 0)
        return ret;

    fd = socket(addr.ss_family, SOCK_STREAM, 0);
    if (fd < 0)
        return -errno;

    ret = bind(fd, (const struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0)
        goto out;

    ret = listen(fd, 1);
    if (ret < 0)
        goto out;

    if (addr.ss_family == AF_UNIX)
        snprintf(url, len, "unix:med%llx?listen=0", priv->handle);
    else
        snprintf(url, len, "rpmsg:med%llx:%s?listen=0", priv->handle,
                 CONFIG_RPTUN_LOCAL_CPUNAME);

    return fd;

out:
    close(fd);
    return -errno;
}

static int media_prepare(void *handle, int32_t cmd,
                         const char *url, const char *options)
{
    MediaProxyPriv *priv = handle;
    int ret = -EINVAL;
    char tmp[64];
    int32_t resp;
    int fd = 0;

    if (!priv)
        return -EINVAL;

    if (priv->socket > 0)
        return ret;

    if (!url) {
        fd = media_bind_socket(handle, tmp, sizeof(tmp));
        if (fd < 0)
            return fd;

        url = tmp;
    }

    ret = media_client_send_recieve(priv->proxy, "%i%l%s%s", "%i",
                                    cmd, priv->handle, url, options, &resp);
    if (ret < 0)
        goto out;

    if (fd > 0) {
        priv->socket = accept(fd, NULL, NULL);
        if (priv->socket < 0) {
            priv->socket = 0;
            ret = -errno;
        }
    }

out:
    if (fd > 0)
        close(fd);

    syslog(LOG_INFO, "%s %p url %s. ret %d\n", __func__, handle, url, ret);
    return ret < 0 ? ret : resp;
}

static ssize_t media_process_data(void *handle, bool player,
                                  void *data, size_t len)
{
    MediaProxyPriv *priv = handle;
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
        else if (ret >= 0)
            errno = ECONNRESET;
    } else {
        ret = recv(priv->socket, data, len, 0);
        if (ret > 0)
            goto out;
        else if (ret == 0)
            errno = ECONNRESET;
    }

    close(priv->socket);
    priv->socket = 0;
    ret = -errno;

out:
    if (atomic_fetch_sub(&priv->refs, 1) == 1)
        free(priv);

    return ret;
}

static void media_proxy_event_cb(void *cookie, media_parcel *msg)
{
    MediaProxyPriv *priv = cookie;
    const char *extra;
    uint32_t event;
    int32_t ret;

    if (priv->event) {
        media_parcel_read_scanf(msg, "%i%i%s", &event, &ret, &extra);
        priv->event(priv->cookie, event, ret, extra);
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int media_process_command(const char *target, const char *cmd,
                          const char *arg, char *res, int res_len)
{
    media_parcel in, out;
    const char *response;
    int32_t resp;
    void *proxy;
    int ret;

    if (!target || !cmd)
        return -EINVAL;

    proxy = media_client_connect();
    if (!proxy)
        return -EINVAL;

    media_parcel_init(&in);
    media_parcel_init(&out);

    ret = media_parcel_append_printf(&in, "%i%s%s%s%i", MEDIA_PROCESS_COMMAND,
                                     target, cmd, arg, res_len);
    if (ret < 0)
        goto out;

    ret = media_client_send_with_ack(proxy, &in, &out);
    if (ret < 0)
        goto out;

    ret = media_parcel_read_scanf(&out, "%i%s", &resp, &response);
    if (ret < 0)
        goto out;

    if (resp < 0)
        goto out;

    if (res_len)
        strcpy(res, response);

out:
    media_parcel_deinit(&in);
    media_parcel_deinit(&out);
    media_client_disconnect(proxy);
    syslog(LOG_INFO, "%s target %s cmd %s args %s ret %d\n", __func__, target, cmd, arg, ret);
    return ret;
}

void media_graph_dump(const char *options)
{
    media_parcel in, out;
    const char *response;
    void *proxy;
    int ret;

    proxy = media_client_connect();
    if (!proxy)
        return;

    media_parcel_init(&in);
    media_parcel_init(&out);

    ret = media_parcel_append_printf(&in, "%i%s", MEDIA_GRAPH_DUMP, options);
    if (ret < 0)
        goto out;

    ret = media_client_send_with_ack(proxy, &in, &out);
    if (ret < 0)
        goto out;

    ret = media_parcel_read_scanf(&out, "%s", &response);
    if (ret < 0)
        goto out;

    syslog(LOG_INFO, "\n%s\n", response);

out:
    media_parcel_deinit(&in);
    media_parcel_deinit(&out);
    media_client_disconnect(proxy);
}

void *media_player_open(const char *params)
{
    MediaProxyPriv *priv;
    int ret;

    priv = zalloc(sizeof(MediaProxyPriv));
    if (!priv)
        return NULL;

    priv->proxy = media_client_connect();
    if (!priv->proxy)
        goto error;

    ret = media_client_send_recieve(priv->proxy, "%i%s", "%l",
                                    MEDIA_PLAYER_OPEN, params, &priv->handle);
    if (ret < 0 || !priv->handle)
        goto disconnect;

    atomic_store(&priv->refs, 1);
    syslog(LOG_INFO, "%s %s return %p. \n", __func__, params, priv);
    return priv;

disconnect:
    media_client_disconnect(priv->proxy);
error:
    free(priv);
    return NULL;
}

int media_player_close(void *handle, int pending_stop)
{
    MediaProxyPriv *priv = handle;
    int32_t resp;
    int ret;

    if (!priv)
        return -EINVAL;

    syslog(LOG_INFO, "%s handle %p. \n", __func__, handle);
    ret = media_client_send_recieve(priv->proxy, "%i%l%i", "%i",
                                    MEDIA_PLAYER_CLOSE, priv->handle, pending_stop, &resp);
    if (ret < 0)
        return ret;

    if (resp < 0)
        return resp;

    ret = media_client_disconnect(priv->proxy);
    if (ret < 0)
        return ret;

    if (atomic_fetch_sub(&priv->refs, 1) == 1) {
        if (priv->socket)
            close(priv->socket);

        free(priv);
    }

    return ret;
}

int media_player_set_event_callback(void *handle, void *cookie,
                                    media_event_callback event_cb)
{
    MediaProxyPriv *priv = handle;
    int32_t resp;
    int ret;

    if (!priv)
        return -EINVAL;

    ret = media_client_set_event_cb(priv->proxy, media_proxy_event_cb, priv);
    if (ret < 0)
        return ret;

    ret = media_client_send_recieve(priv->proxy, "%i%l", "%i",
                                    MEDIA_PLAYER_SET_CALLBACK, priv->handle, &resp);

    if (ret < 0)
        return ret;

    if (resp < 0)
        return resp;

    priv->event  = event_cb;
    priv->cookie = cookie;

    return ret;
}

int media_player_prepare(void *handle, const char *url, const char *options)
{
    return media_prepare(handle, MEDIA_PLAYER_PREPARE, url, options);
}

int media_player_reset(void *handle)
{
    MediaProxyPriv *priv = handle;
    int32_t resp;
    int ret;

    if (!priv)
        return -EINVAL;

    ret = media_client_send_recieve(priv->proxy, "%i%l", "%i",
                                    MEDIA_PLAYER_RESET, priv->handle, &resp);

    return ret < 0 ? ret : resp;
}

ssize_t media_player_write_data(void *handle, const void *data, size_t len)
{
    return media_process_data(handle, true, (void *)data, len);
}

int media_player_get_sockaddr(void *handle, struct sockaddr_storage *addr)
{
    return media_get_sockaddr(handle, addr);
}

int media_player_start(void *handle)
{
    MediaProxyPriv *priv = handle;
    int32_t resp;
    int ret;

    if (!priv)
        return -EINVAL;

    syslog(LOG_INFO, "%s handle %p. \n", __func__, handle);
    ret = media_client_send_recieve(priv->proxy, "%i%l", "%i",
                                    MEDIA_PLAYER_START, priv->handle, &resp);

    return ret < 0 ? ret : resp;
}

int media_player_stop(void *handle)
{
    MediaProxyPriv *priv = handle;
    int32_t resp;
    int ret;

    if (!priv)
        return -EINVAL;

    if (atomic_load(&priv->refs) == 1 && priv->socket) {
        close(priv->socket);
        priv->socket = 0;
    }

    ret = media_client_send_recieve(priv->proxy, "%i%l", "%i",
                                    MEDIA_PLAYER_STOP, priv->handle, &resp);

    syslog(LOG_INFO, "%s handle %p. \n", __func__, handle);
    return ret < 0 ? ret : resp;
}

int media_player_pause(void *handle)
{
    MediaProxyPriv *priv = handle;
    int32_t resp;
    int ret;

    if (!priv)
        return -EINVAL;

    ret = media_client_send_recieve(priv->proxy, "%i%l", "%i",
                                    MEDIA_PLAYER_PAUSE, priv->handle, &resp);
    return ret < 0 ? ret : resp;
}

int media_player_seek(void *handle, unsigned int msec)
{
    MediaProxyPriv *priv = handle;
    int32_t resp;
    int ret;

    if (!priv)
        return -EINVAL;

    ret = media_client_send_recieve(priv->proxy, "%i%l%i", "%i",
                                    MEDIA_PLAYER_SEEK, priv->handle, msec, &resp);

    return ret < 0 ? ret : resp;
}

int media_player_set_looping(void *handle, int loop)
{
    MediaProxyPriv *priv = handle;
    int32_t resp;
    int ret;

    if (!priv)
        return -EINVAL;

    ret = media_client_send_recieve(priv->proxy, "%i%l%i", "%i",
                                    MEDIA_PLAYER_LOOP, priv->handle, loop, &resp);

    return ret < 0 ? ret : resp;
}

int media_player_is_playing(void *handle)
{
    MediaProxyPriv *priv = handle;
    int32_t playing;
    int ret;

    if (!priv)
        return -EINVAL;

    ret = media_client_send_recieve(priv->proxy, "%i%l", "%i",
                                    MEDIA_PLAYER_ISPLAYING, priv->handle, &playing);
    if (ret < 0)
        return ret;

    return playing;
}

int media_player_get_position(void *handle, unsigned int *msec)
{
    MediaProxyPriv *priv = handle;
    int32_t resp;
    int ret;

    if (!priv || !msec)
        return -EINVAL;

    ret = media_client_send_recieve(priv->proxy, "%i%l", "%i%i",
                                    MEDIA_PLAYER_POSITION, priv->handle, &resp, msec);

    return ret < 0 ? ret : resp;
}

int media_player_get_duration(void *handle, unsigned int *msec)
{
    MediaProxyPriv *priv = handle;
    int32_t resp;
    int ret;

    if (!priv || !msec)
        return -EINVAL;

    ret = media_client_send_recieve(priv->proxy, "%i%l", "%i%i",
                                    MEDIA_PLAYER_DURATION, priv->handle, &resp, msec);

    return ret < 0 ? ret : resp;
}

int media_player_set_volume(void *handle, float volume)
{
    MediaProxyPriv *priv = handle;
    int32_t resp;
    int ret;

    if (!priv)
        return -EINVAL;

    ret = media_client_send_recieve(priv->proxy, "%i%l%f", "%i",
                                    MEDIA_PLAYER_SET_VOLUME, priv->handle, volume, &resp);

    return ret < 0 ? ret : resp;
}

int media_player_get_volume(void *handle, float *volume)
{
    MediaProxyPriv *priv = handle;
    int32_t resp;
    int ret;

    if (!priv || !volume)
        return -EINVAL;

    ret = media_client_send_recieve(priv->proxy, "%i%l", "%i%f",
                                    MEDIA_PLAYER_GET_VOLUME, priv->handle, &resp, volume);

    return ret < 0 ? ret : resp;
}

int media_player_set_property(void *handle, const char *target, const char *key, const char *value)
{
    MediaProxyPriv *priv = handle;
    int32_t resp;
    int ret;

    if (!priv)
        return -EINVAL;

    ret = media_client_send_recieve(priv->proxy, "%i%l%s%s%s", "%i",
                                    MEDIA_PLAYER_SET_PROPERTY, priv->handle, target, key, value, &resp);

    return ret < 0 ? ret : resp;
}

int media_player_get_property(void *handle, const char *target, const char *key, char *value, int value_len)
{
    MediaProxyPriv *priv = handle;
    media_parcel in, out;
    const char *response;
    int32_t resp;
    int ret;

    if (!priv)
        return -EINVAL;

    media_parcel_init(&in);
    media_parcel_init(&out);

    ret = media_parcel_append_printf(&in, "%i%l%s%s%i", MEDIA_PLAYER_GET_PROPERTY,
                                     priv->handle, target, key, value_len);
    if (ret < 0)
        goto out;

    ret = media_client_send_with_ack(priv->proxy, &in, &out);
    if (ret < 0)
        goto out;

    ret = media_parcel_read_scanf(&out, "%i%s", &resp, &response);
    if (ret < 0)
        goto out;

    if (resp < 0)
        goto out;

    if (value_len)
        strcpy(value, response);

out:
    media_parcel_deinit(&in);
    media_parcel_deinit(&out);
    return ret;
}

void *media_recorder_open(const char *params)
{
    MediaProxyPriv *priv;
    int ret;

    priv = zalloc(sizeof(MediaProxyPriv));
    if (!priv)
        return NULL;

    priv->proxy = media_client_connect();
    if (!priv->proxy)
        goto error;

    ret = media_client_send_recieve(priv->proxy, "%i%s", "%l",
                                    MEDIA_RECORDER_OPEN, params, &priv->handle);
    if (ret < 0 || !priv->handle)
        goto disconnect;

    atomic_store(&priv->refs, 1);

    syslog(LOG_INFO, "%s %s return %p. \n", __func__, params, priv);
    return priv;

disconnect:
    media_client_disconnect(priv->proxy);
error:
    free(priv);
    return NULL;
}

int media_recorder_close(void *handle)
{
    MediaProxyPriv *priv = handle;
    int32_t resp;
    int ret;

    if (!priv)
        return -EINVAL;

    syslog(LOG_INFO, "%s handle %p. \n", __func__, handle);
    ret = media_client_send_recieve(priv->proxy, "%i%l", "%i",
                                    MEDIA_RECORDER_CLOSE, priv->handle, &resp);
    if (ret < 0)
        return ret;

    if (resp < 0)
        return resp;

    ret = media_client_disconnect(priv->proxy);
    if (ret < 0)
        return ret;

    if (atomic_fetch_sub(&priv->refs, 1) == 1) {
        if (priv->socket)
            close(priv->socket);

        free(priv);
    }

    return ret;
}

int media_recorder_set_event_callback(void *handle, void *cookie,
                                      media_event_callback event_cb)
{
    MediaProxyPriv *priv = handle;
    int32_t resp;
    int ret;

    if (!priv)
        return -EINVAL;

    ret = media_client_set_event_cb(priv->proxy, media_proxy_event_cb, priv);
    if (ret < 0)
        return ret;

    ret = media_client_send_recieve(priv->proxy, "%i%l", "%i",
                                    MEDIA_RECORDER_SET_CALLBACK, priv->handle, &resp);
    if (ret < 0)
        return ret;

    if (resp < 0)
        return resp;

    priv->event  = event_cb;
    priv->cookie = cookie;

    return ret;
}

int media_recorder_prepare(void *handle, const char *url, const char *options)
{
    return media_prepare(handle, MEDIA_RECORDER_PREPARE, url, options);
}

int media_recorder_reset(void *handle)
{
    MediaProxyPriv *priv = handle;
    int32_t resp;
    int ret;

    if (!priv)
        return -EINVAL;

    ret = media_client_send_recieve(priv->proxy, "%i%l", "%i",
                                    MEDIA_RECORDER_RESET, priv->handle, &resp);

    return ret < 0 ? ret : resp;
}

ssize_t media_recorder_read_data(void *handle, void *data, size_t len)
{
    return media_process_data(handle, false, data, len);
}

int media_recorder_get_sockaddr(void *handle, struct sockaddr_storage *addr)
{
    return media_get_sockaddr(handle, addr);
}

int media_recorder_start(void *handle)
{
    MediaProxyPriv *priv = handle;
    int32_t resp;
    int ret;

    if (!priv)
        return -EINVAL;

    ret = media_client_send_recieve(priv->proxy, "%i%l", "%i",
                                    MEDIA_RECORDER_START, priv->handle, &resp);

    syslog(LOG_INFO, "%s handle %p. \n", __func__, handle);
    return ret < 0 ? ret : resp;
}

int media_recorder_pause(void *handle)
{
    MediaProxyPriv *priv = handle;
    int32_t resp;
    int ret;

    if (!priv)
        return -EINVAL;

    ret = media_client_send_recieve(priv->proxy, "%i%l", "%i",
                                    MEDIA_RECORDER_PAUSE, priv->handle, &resp);

    return ret < 0 ? ret : resp;
}

int media_recorder_stop(void *handle)
{
    MediaProxyPriv *priv = handle;
    int32_t resp;
    int ret;

    if (!priv)
        return -EINVAL;

    if (atomic_load(&priv->refs) == 1 && priv->socket) {
        close(priv->socket);
        priv->socket = 0;
    }

    ret = media_client_send_recieve(priv->proxy, "%i%l", "%i",
                                    MEDIA_RECORDER_STOP, priv->handle, &resp);

    syslog(LOG_INFO, "%s handle %p. \n", __func__, handle);
    return ret < 0 ? ret : resp;
}

int media_recorder_set_property(void *handle, const char *target, const char *key, const char *value)
{
    MediaProxyPriv *priv = handle;
    int32_t resp;
    int ret;

    if (!priv)
        return -EINVAL;

    ret = media_client_send_recieve(priv->proxy, "%i%l%s%s%s", "%i",
                                    MEDIA_RECORDER_SET_PROPERTY, priv->handle, target, key, value, &resp);

    return ret < 0 ? ret : resp;
}

int media_recorder_get_property(void *handle, const char *target, const char *key, char *value, int value_len)
{
    MediaProxyPriv *priv = handle;
    media_parcel in, out;
    const char *response;
    int32_t resp;
    int ret;

    if (!priv)
        return -EINVAL;

    media_parcel_init(&in);
    media_parcel_init(&out);

    ret = media_parcel_append_printf(&in, "%i%l%s%s%i", MEDIA_RECORDER_GET_PROPERTY,
                                     priv->handle, target, key, value_len);
    if (ret < 0)
        goto out;

    ret = media_client_send_with_ack(priv->proxy, &in, &out);
    if (ret < 0)
        goto out;

    ret = media_parcel_read_scanf(&out, "%i%s", &resp, &response);
    if (ret < 0)
        goto out;

    if (resp < 0)
        goto out;

    if (value_len)
        strcpy(value, response);

out:
    media_parcel_deinit(&in);
    media_parcel_deinit(&out);
    return ret;
}

int media_policy_set_int(const char *name, int value, int apply)
{
    int32_t resp;
    void *proxy;
    int ret;

    proxy = media_client_connect();
    if (!proxy)
        return -EINVAL;

    ret = media_client_send_recieve(proxy, "%i%s%i%i", "%i", MEDIA_POLICY_SET_INT,
                                    name, value, apply, &resp);

    media_client_disconnect(proxy);

    return ret < 0 ? ret : resp;
}

int media_policy_get_int(const char *name, int *value)
{
    int32_t resp;
    void *proxy;
    int ret;

    if (!value)
        return -EINVAL;

    proxy = media_client_connect();
    if (!proxy)
        return -EINVAL;

    ret = media_client_send_recieve(proxy, "%i%s", "%i%i", MEDIA_POLICY_GET_INT,
                                    name, &resp, value);

    media_client_disconnect(proxy);

    return ret < 0 ? ret : resp;
}

int media_policy_set_string(const char *name, const char *value, int apply)
{
    int32_t resp;
    void *proxy;
    int ret;

    proxy = media_client_connect();
    if (!proxy)
        return -EINVAL;

    ret = media_client_send_recieve(proxy, "%i%s%s%i", "%i", MEDIA_POLICY_SET_STRING,
                                    name, value, apply, &resp);

    media_client_disconnect(proxy);

    return ret < 0 ? ret : resp;
}

int media_policy_get_string(const char *name, char *value, int len)
{
    media_parcel in, out;
    const char *response;
    int32_t resp;
    void *proxy;
    int ret;

    if (!value)
        return -EINVAL;

    proxy = media_client_connect();
    if (!proxy)
        return -EINVAL;

    media_parcel_init(&in);
    media_parcel_init(&out);

    ret = media_parcel_append_printf(&in, "%i%s%i", MEDIA_POLICY_GET_STRING,
                                     name, len);
    if (ret < 0)
        goto out;

    ret = media_client_send_with_ack(proxy, &in, &out);
    if (ret < 0)
        goto out;

    ret = media_parcel_read_scanf(&out, "%i%s", &resp, &response);
    if (ret < 0)
        goto out;

    if (resp < 0)
        goto out;

    if (len)
        strcpy(value, response);

out:
    media_parcel_deinit(&in);
    media_parcel_deinit(&out);
    media_client_disconnect(proxy);
    return ret;
}

int media_policy_include(const char *name, const char *values, int apply)
{
    int32_t resp;
    void *proxy;
    int ret;

    proxy = media_client_connect();
    if (!proxy)
        return -EINVAL;

    ret = media_client_send_recieve(proxy, "%i%s%s%i", "%i", MEDIA_POLICY_INCLUDE,
                                    name, values, apply, &resp);

    media_client_disconnect(proxy);

    return ret < 0 ? ret : resp;
}

int media_policy_exclude(const char *name, const char *values, int apply)
{
    int32_t resp;
    void *proxy;
    int ret;

    proxy = media_client_connect();
    if (!proxy)
        return -EINVAL;

    ret = media_client_send_recieve(proxy, "%i%s%s%i", "%i", MEDIA_POLICY_EXCLUDE,
                                    name, values, apply, &resp);

    media_client_disconnect(proxy);

    return ret < 0 ? ret : resp;
}

int media_policy_increase(const char *name, int apply)
{
    int32_t resp;
    void *proxy;
    int ret;

    proxy = media_client_connect();
    if (!proxy)
        return -EINVAL;

    ret = media_client_send_recieve(proxy, "%i%s%i", "%i", MEDIA_POLICY_INCREASE,
                                    name, apply, &resp);

    media_client_disconnect(proxy);

    return ret < 0 ? ret : resp;
}

int media_policy_decrease(const char *name, int apply)
{
    int32_t resp;
    void *proxy;
    int ret;

    proxy = media_client_connect();
    if (!proxy)
        return -EINVAL;

    ret = media_client_send_recieve(proxy, "%i%s%i", "%i", MEDIA_POLICY_DECREASE,
                                    name, apply, &resp);

    media_client_disconnect(proxy);

    return ret < 0 ? ret : resp;
}

void media_policy_dump(const char *options)
{
    media_parcel in, out;
    const char *response;
    void *proxy;
    int ret;

    proxy = media_client_connect();
    if (!proxy)
        return;

    media_parcel_init(&in);
    media_parcel_init(&out);

    ret = media_parcel_append_printf(&in, "%i%s", MEDIA_POLICY_DUMP, options);
    if (ret < 0)
        goto out;

    ret = media_client_send_with_ack(proxy, &in, &out);
    if (ret < 0)
        goto out;

    ret = media_parcel_read_scanf(&out, "%s", &response);
    if (ret < 0)
        goto out;

    syslog(LOG_INFO, "\n%s\n", response);

out:
    media_parcel_deinit(&in);
    media_parcel_deinit(&out);
    media_client_disconnect(proxy);
}
