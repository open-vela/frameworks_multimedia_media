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

static int media_control(int control, void *handle, const char *target,
                         const char *cmd, const char *arg, int apply,
                         char *res, int res_len)
{
    MediaProxyPriv *priv = handle;
    media_parcel in, out;
    const char *response;
    int ret = -EINVAL;
    int32_t resp;
    void *proxy;

    proxy = priv ? priv->proxy : media_client_connect(CONFIG_MEDIA_SERVER_CPUNAME);
    if (!proxy)
        return ret;

    media_parcel_init(&in);
    media_parcel_init(&out);

    switch (control) {
        case MEDIA_GRAPH_CONTROL:
            ret = media_parcel_append_printf(&in, "%i%s%s%s%i", control,
                                             target, cmd, arg, res_len);
            break;

        case MEDIA_POLICY_CONTROL:
            ret = media_parcel_append_printf(&in, "%i%s%s%s%i%i", control,
                                             target, cmd, arg, apply, res_len);
            break;

        case MEDIA_PLAYER_CONTROL:
        case MEDIA_RECORDER_CONTROL:
            ret = media_parcel_append_printf(&in, "%i%l%s%s%s%i", control,
                                             priv->handle, target, cmd,
                                             arg, res_len);
            break;

    }

    if (ret < 0)
        goto out;

    ret = media_client_send_with_ack(proxy, &in, &out);
    if (ret < 0)
        goto out;

    ret = media_parcel_read_scanf(&out, "%i%s", &resp, &response);
    if (ret < 0 || resp < 0)
        goto out;

    if (res_len > 0)
        strlcpy(res, response, res_len);
    else if (response && strlen(response) > 0)
        syslog(LOG_INFO, "\n%s\n", response);

out:
    media_parcel_deinit(&in);
    media_parcel_deinit(&out);
    if (!priv)
        media_client_disconnect(proxy);

    return ret < 0 ? ret : resp;
}

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

static int media_prepare(int control, void *handle,
                         const char *url, const char *options)
{
    MediaProxyPriv *priv = handle;
    int ret = -EINVAL;
    char tmp[64];
    int fd = 0;

    if (!priv || priv->socket > 0)
        return ret;

    if (!url) {
        fd = media_bind_socket(handle, tmp, sizeof(tmp));
        if (fd < 0)
            return fd;

        url = tmp;
    }

    if (options && options[0] != '\0') {
        ret = media_control(control, priv, NULL, "set_options", options, 0, NULL, 0);
        if (ret < 0)
            goto out;
    }

    ret = media_control(control, priv, NULL, "prepare", url, 0, NULL, 0);
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
    return ret;
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

static int media_set_event_cb(int control, void *handle, void *cookie,
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
                                    control, priv->handle, &resp);
    if (ret < 0)
        return ret;

    if (resp < 0)
        return resp;

    priv->event  = event_cb;
    priv->cookie = cookie;

    return ret;
}

static int media_get_socket(void *handle)
{
    MediaProxyPriv *priv = handle;

    if (!priv || priv->socket <= 0)
        return -EINVAL;

    return priv->socket;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int media_process_command(const char *target, const char *cmd,
                          const char *arg, char *res, int res_len)
{
    int ret;

    if (!target || !cmd)
        return -EINVAL;

    ret = media_control(MEDIA_GRAPH_CONTROL, NULL, target, cmd, arg, 0, res, res_len);
    syslog(LOG_INFO, "%s target %s cmd %s args %s ret %d\n", __func__, target, cmd, arg, ret);
    return ret;
}

void media_graph_dump(const char *options)
{
    media_control(MEDIA_GRAPH_CONTROL, NULL, NULL, "dump", options, 0, NULL, 0);
}

void *media_player_open(const char *params)
{
    MediaProxyPriv *priv;
    char tmp[32];
    int ret;

    priv = zalloc(sizeof(MediaProxyPriv));
    if (!priv)
        return NULL;

    priv->proxy = media_client_connect(CONFIG_MEDIA_SERVER_CPUNAME);
    if (!priv->proxy)
        goto error;

    ret = media_control(MEDIA_PLAYER_CONTROL, priv, NULL, "open", params, 0, tmp, sizeof(tmp));
    if (ret < 0)
        goto disconnect;

    sscanf(tmp, "%llu", &priv->handle);
    if (!priv->handle)
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
    char tmp[32];
    int ret;

    if (!priv)
        return -EINVAL;

    syslog(LOG_INFO, "%s handle %p. \n", __func__, handle);

    snprintf(tmp, sizeof(tmp), "%d", pending_stop);
    ret = media_control(MEDIA_PLAYER_CONTROL, priv, NULL, "close", tmp, 0, NULL, 0);
    if (ret < 0)
        return ret;

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
    return media_set_event_cb(MEDIA_PLAYER_SET_CALLBACK, handle, cookie, event_cb);
}

int media_player_prepare(void *handle, const char *url, const char *options)
{
    return media_prepare(MEDIA_PLAYER_CONTROL, handle, url, options);
}

int media_player_reset(void *handle)
{
    if (!handle)
        return -EINVAL;

    return media_control(MEDIA_PLAYER_CONTROL, handle, NULL, "reset", NULL, 0, NULL, 0);
}

ssize_t media_player_write_data(void *handle, const void *data, size_t len)
{
    return media_process_data(handle, true, (void *)data, len);
}

int media_player_get_sockaddr(void *handle, struct sockaddr_storage *addr)
{
    return media_get_sockaddr(handle, addr);
}

int media_player_get_socket(void *handle)
{
    return media_get_socket(handle);
}

int media_player_start(void *handle)
{
    if (!handle)
        return -EINVAL;
    syslog(LOG_INFO, "%s handle %p. \n", __func__, handle);
    return media_control(MEDIA_PLAYER_CONTROL, handle, NULL, "start", NULL, 0, NULL, 0);
}

int media_player_stop(void *handle)
{
    MediaProxyPriv *priv = handle;

    if (!priv)
        return -EINVAL;

    if (atomic_load(&priv->refs) == 1 && priv->socket) {
        close(priv->socket);
        priv->socket = 0;
    }

    syslog(LOG_INFO, "%s handle %p. \n", __func__, handle);
    return media_control(MEDIA_PLAYER_CONTROL, priv, NULL, "stop", NULL, 0, NULL, 0);
}

int media_player_pause(void *handle)
{
    if (!handle)
        return -EINVAL;

    return media_control(MEDIA_PLAYER_CONTROL, handle, NULL, "pause", NULL, 0, NULL, 0);
}

int media_player_seek(void *handle, unsigned int msec)
{
    char tmp[32];

    if (!handle)
        return -EINVAL;

    snprintf(tmp, sizeof(tmp), "%u", msec);
    return media_control(MEDIA_PLAYER_CONTROL, handle, NULL, "seek", tmp, 0, NULL, 0);
}

int media_player_set_looping(void *handle, int loop)
{
    char tmp[32];

    if (!handle)
        return -EINVAL;

    snprintf(tmp, sizeof(tmp), "%d", loop);
    return media_control(MEDIA_PLAYER_CONTROL, handle, NULL, "set_loop", tmp, 0, NULL, 0);
}

int media_player_is_playing(void *handle)
{
    char tmp[32];
    int ret;

    if (!handle)
        return -EINVAL;

    ret = media_control(MEDIA_PLAYER_CONTROL, handle, NULL, "get_playing", NULL, 0, tmp, sizeof(tmp));
    return ret < 0 ? ret : !!atoi(tmp);
}

int media_player_get_position(void *handle, unsigned int *msec)
{
    char tmp[32];
    int ret;

    if (!handle || !msec)
        return -EINVAL;

    ret = media_control(MEDIA_PLAYER_CONTROL, handle, NULL, "get_position", NULL, 0, tmp, sizeof(tmp));
    if (ret >= 0)
        *msec = strtoul(tmp, NULL, 0);

    return ret;
}

int media_player_get_duration(void *handle, unsigned int *msec)
{
    char tmp[32];
    int ret;

    if (!handle || !msec)
        return -EINVAL;

    ret = media_control(MEDIA_PLAYER_CONTROL, handle, NULL, "get_duration", NULL, 0, tmp, sizeof(tmp));
    if (ret >= 0)
        *msec = strtoul(tmp, NULL, 0);

    return ret;
}

int media_player_set_volume(void *handle, float volume)
{
    char tmp[32];

    if (!handle)
        return -EINVAL;

    snprintf(tmp, sizeof(tmp), "%f", volume);
    return media_control(MEDIA_PLAYER_CONTROL, handle, "volume", "volume", tmp, 0, NULL, 0);
}

int media_player_get_volume(void *handle, float *volume)
{
    char tmp[32];
    int ret;

    if (!handle || !volume)
        return -EINVAL;

    ret = media_control(MEDIA_PLAYER_CONTROL, handle, "volume", "dump", NULL, 0, tmp, sizeof(tmp));
    if (ret >= 0) {
        sscanf(tmp, "vol:%f", volume);
        ret = 0;
    }

    return ret;
}

int media_player_set_property(void *handle, const char *target, const char *key, const char *value)
{
    if (!handle)
        return -EINVAL;

    return media_control(MEDIA_PLAYER_CONTROL, handle, target, key, value, 0, NULL, 0);
}

int media_player_get_property(void *handle, const char *target, const char *key, char *value, int value_len)
{
    if (!handle || !value)
        return -EINVAL;

    return media_control(MEDIA_PLAYER_CONTROL, handle, target, key, NULL, 0, value, value_len);
}

void *media_recorder_open(const char *params)
{
    MediaProxyPriv *priv;
    char tmp[32];
    int ret;

    priv = zalloc(sizeof(MediaProxyPriv));
    if (!priv)
        return NULL;

    priv->proxy = media_client_connect(CONFIG_MEDIA_SERVER_CPUNAME);
    if (!priv->proxy)
        goto error;

    ret = media_control(MEDIA_RECORDER_CONTROL, priv, 0, "open", params, 0, tmp, sizeof(tmp));
    if (ret < 0)
        goto disconnect;

    sscanf(tmp, "%llu", &priv->handle);
    if (!priv->handle)
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
    int ret;

    if (!priv)
        return -EINVAL;

    syslog(LOG_INFO, "%s handle %p. \n", __func__, handle);
    ret = media_control(MEDIA_RECORDER_CONTROL, priv, NULL, "close", NULL, 0, NULL, 0);
    if (ret < 0)
        return ret;

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
    return media_set_event_cb(MEDIA_RECORDER_SET_CALLBACK, handle, cookie, event_cb);
}

int media_recorder_prepare(void *handle, const char *url, const char *options)
{
    return media_prepare(MEDIA_RECORDER_CONTROL, handle, url, options);
}

int media_recorder_reset(void *handle)
{
    if (!handle)
        return -EINVAL;

    return media_control(MEDIA_RECORDER_CONTROL, handle, NULL, "reset", NULL, 0, NULL, 0);
}

ssize_t media_recorder_read_data(void *handle, void *data, size_t len)
{
    return media_process_data(handle, false, data, len);
}

int media_recorder_get_sockaddr(void *handle, struct sockaddr_storage *addr)
{
    return media_get_sockaddr(handle, addr);
}

int media_recorder_get_socket(void *handle)
{
    return media_get_socket(handle);
}

int media_recorder_start(void *handle)
{
    if (!handle)
        return -EINVAL;

    return media_control(MEDIA_RECORDER_CONTROL, handle, NULL, "start", NULL, 0, NULL, 0);
}

int media_recorder_pause(void *handle)
{
    if (!handle)
        return -EINVAL;

    return media_control(MEDIA_RECORDER_CONTROL, handle, NULL, "pause", NULL, 0, NULL, 0);
}

int media_recorder_stop(void *handle)
{
    if (!handle)
        return -EINVAL;

    return media_control(MEDIA_RECORDER_CONTROL, handle, NULL, "stop", NULL, 0, NULL, 0);
}

int media_recorder_set_property(void *handle, const char *target, const char *key, const char *value)
{
    if (!handle)
        return -EINVAL;

    return media_control(MEDIA_RECORDER_CONTROL, handle, target, key, value, 0, NULL, 0);
}

int media_recorder_get_property(void *handle, const char *target, const char *key, char *value, int value_len)
{
    if (!handle)
        return -EINVAL;

    return media_control(MEDIA_RECORDER_CONTROL, handle, target, key, NULL, 0, value, value_len);
}

int media_policy_set_int(const char *name, int value, int apply)
{
    char tmp[32];

    snprintf(tmp, sizeof(tmp), "%d", value);
    return media_control(MEDIA_POLICY_CONTROL, NULL, name, "set_int", tmp, apply, NULL, 0);
}

int media_policy_get_int(const char *name, int *value)
{
    char tmp[32];
    int ret;

    if (!value)
        return -EINVAL;

    ret = media_control(MEDIA_POLICY_CONTROL, NULL, name, "get_int", NULL, 0, tmp, sizeof(tmp));
    if (ret >= 0) {
        *value = atoi(tmp);
        ret = 0;
    }

    return ret;
}

int media_policy_contain(const char *name, const char *values, int *result)
{
    char tmp[32];
    int ret;

    if (!result)
        return -EINVAL;

    ret = media_control(MEDIA_POLICY_CONTROL, NULL, name, "contain", values, 0, tmp, sizeof(tmp));
    if (ret >= 0) {
        *result = atoi(tmp);
        ret = 0;
    }

    return ret;
}

int media_policy_set_string(const char *name, const char *value, int apply)
{
    return media_control(MEDIA_POLICY_CONTROL, NULL, name, "set_string", value, apply, NULL, 0);
}

int media_policy_get_string(const char *name, char *value, int len)
{
    return media_control(MEDIA_POLICY_CONTROL, NULL, name, "get_string", NULL, 0, value, len);
}

int media_policy_include(const char *name, const char *values, int apply)
{
    return media_control(MEDIA_POLICY_CONTROL, NULL, name, "include", values, apply, NULL, 0);
}

int media_policy_exclude(const char *name, const char *values, int apply)
{
    return media_control(MEDIA_POLICY_CONTROL, NULL, name, "exclude", values, apply, NULL, 0);
}

int media_policy_increase(const char *name, int apply)
{
    return media_control(MEDIA_POLICY_CONTROL, NULL, name, "increase", NULL, apply, NULL, 0);
}

int media_policy_decrease(const char *name, int apply)
{
    return media_control(MEDIA_POLICY_CONTROL, NULL, name, "decrease", NULL, apply, NULL, 0);
}

void media_policy_dump(const char *options)
{
    media_control(MEDIA_POLICY_CONTROL, NULL, NULL, "dump", options, 0, NULL, 0);
}
