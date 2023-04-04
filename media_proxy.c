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

#include <assert.h>
#include <errno.h>
#include <malloc.h>
#include <media_api.h>
#include <netpacket/rpmsg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <sys/un.h>
#include <syslog.h>

#include "media_client.h"
#include "media_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MEDIA_DELIM " ,;|"

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct MediaProxyPriv {
    void* proxy;
    char* cpu;
    char* stream_type;
    uint64_t handle;
    atomic_int refs;
    int socket;
    void* cookie;
    media_event_callback event;
} MediaProxyPriv;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/**
 * @brief Transact a command to server through existed long connection.
 *
 * @param control   Type ID.
 * @param handle    Instance handle.
 * @param target
 * @param cmd
 * @param arg
 * @param apply     For Policy: wether apply to hw,
 *                  For Session: event sent to player client.
 * @param res       Response address.
 * @param res_len   Response max lenghth.
 * @return int      Zero on success; a negated errno value on failure.
 */
static int media_transact_once(int control, void* handle, const char* target,
    const char* cmd, const char* arg, int apply, char* res, int res_len)
{
    MediaProxyPriv* priv = handle;
    media_parcel in, out;
    const char* response;
    int ret = -EINVAL;
    int32_t resp = 0;
    const char* name;

    if (!priv || !priv->proxy)
        return ret;

    media_parcel_init(&in);
    media_parcel_init(&out);

    switch (control) {
    case MEDIA_GRAPH_CONTROL:
        name = "graph";
        ret = media_parcel_append_printf(&in, "%i%s%s%s%i", control,
            target, cmd, arg, res_len);
        break;

    case MEDIA_POLICY_CONTROL:
        name = "policy";
        ret = media_parcel_append_printf(&in, "%i%s%s%s%i%i", control,
            target, cmd, arg, apply, res_len);
        break;

    case MEDIA_PLAYER_CONTROL:
        name = "player";
        ret = media_parcel_append_printf(&in, "%i%l%s%s%s%i", control,
            priv->handle, target, cmd, arg, res_len);
        break;

    case MEDIA_RECORDER_CONTROL:
        name = "recorder";
        ret = media_parcel_append_printf(&in, "%i%l%s%s%s%i", control,
            priv->handle, target, cmd, arg, res_len);
        break;

    case MEDIA_SESSION_CONTROL:
        name = "session";
        ret = media_parcel_append_printf(&in, "%i%l%s%s%s%i", control,
            priv->handle, target, cmd, arg, res_len);
        break;
    }

    if (ret < 0)
        goto out;

    ret = media_client_send_with_ack(priv->proxy, &in, &out);
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

    syslog(LOG_INFO, "%s:%s:%p %s %s %s %s ret:%d resp:%d\n",
        name, priv->cpu, (void*)(uintptr_t)priv->handle, target ? target : "_",
        cmd, arg ? arg : "_", apply ? "apply" : "_", ret, (int)resp);
    return ret < 0 ? ret : resp;
}

/**
 * @brief Transact a command to server.
 *
 * @param control   Type ID.
 * @param handle    Instance handle.
 * @param target
 * @param cmd
 * @param arg
 * @param apply     For Policy: wether apply to hw,
 *                  For Session: event sent to player client.
 * @param res       Response address.
 * @param res_len   Response max lenghth.
 * @param remote    Only transact to remote servers if true
 *                  (for server transacts to other servers)
 * @return int      Zero on success; a negated errno value on failure.
 */
static int media_transact(int control, void* handle, const char* target, const char* cmd,
    const char* arg, int apply, char* res, int res_len, bool remote)
{
    MediaProxyPriv *priv, priv_ = { 0 };
    char *saveptr, *cpu;
    int ret = -ENOSYS;
    char tmp[128];

    priv = handle ? handle : &priv_;

    if (priv->proxy)
        return media_transact_once(control, priv, target, cmd, arg, apply, res, res_len);

    strlcpy(tmp, CONFIG_MEDIA_SERVER_CPUNAME, sizeof(tmp));
    for (cpu = strtok_r(tmp, MEDIA_DELIM, &saveptr); cpu;
         cpu = strtok_r(NULL, MEDIA_DELIM, &saveptr)) {
        if (remote && !strcmp(cpu, CONFIG_RPTUN_LOCAL_CPUNAME))
            continue;

        priv->proxy = media_client_connect(cpu);
        if (!priv->proxy)
            continue;

        priv->cpu = cpu;
        ret = media_transact_once(control, priv, target, cmd, arg, apply, res, res_len);
        switch (control) {
        case MEDIA_GRAPH_CONTROL:
            media_client_disconnect(priv->proxy);
            priv->proxy = NULL;
            ret = 0;
            break;

        case MEDIA_POLICY_CONTROL:
            media_client_disconnect(priv->proxy);
            priv->proxy = NULL;
            if (ret != -ENOSYS) // return once found policy
                return ret;

            break;

        case MEDIA_PLAYER_CONTROL:
        case MEDIA_RECORDER_CONTROL:
        case MEDIA_SESSION_CONTROL:
            if (ret < 0) {
                media_client_disconnect(priv->proxy);
                priv->proxy = NULL;
                break;
            }

            /* keep the connection */

            priv->cpu = strdup(cpu);
            DEBUGASSERT(priv->cpu);
            return 0;
        }
    }

    return ret;
}

static void* media_open(int control, const char* params)
{
    MediaProxyPriv* priv;
    char tmp[32];

    priv = zalloc(sizeof(MediaProxyPriv));
    if (!priv)
        return NULL;

    if (media_transact(control, priv, NULL, "open", params, 0, tmp, sizeof(tmp), 0) < 0)
        goto error1;

    sscanf(tmp, "%llu", &priv->handle);
    if (!priv->handle)
        goto error2;

    atomic_store(&priv->refs, 1);
    syslog(LOG_INFO, "%s:%s handle:%p\n", __func__, params, (void*)(uintptr_t)priv->handle);
    return priv;

error2:
    free(priv->cpu);
error1:
    free(priv);
    return NULL;
}

static void media_close_socket(void* handle)
{
    MediaProxyPriv* priv = handle;

    if (atomic_load(&priv->refs) == 1 && priv->socket) {
        close(priv->socket);
        priv->socket = 0;
    }
}

static int media_close(int control, void* handle, int pending_stop)
{
    MediaProxyPriv* priv = handle;
    char tmp[32];
    int ret;

    snprintf(tmp, sizeof(tmp), "%d", pending_stop);
    ret = media_transact_once(control, priv, NULL, "close", tmp, 0, NULL, 0);
    if (ret < 0)
        return ret;

    ret = media_client_disconnect(priv->proxy);
    if (ret < 0)
        return ret;

    media_close_socket(priv);
    if (atomic_fetch_sub(&priv->refs, 1) == 1) {
        free(priv->cpu);
        free(priv);
    }

    return ret;
}

static int media_get_sockaddr(void* handle, struct sockaddr_storage* addr_)
{
    MediaProxyPriv* priv = handle;

    if (!handle || !addr_)
        return -EINVAL;

    if (!strcmp(priv->cpu, CONFIG_RPTUN_LOCAL_CPUNAME)) {
        struct sockaddr_un* addr = (struct sockaddr_un*)addr_;

        addr->sun_family = AF_UNIX;
        snprintf(addr->sun_path, UNIX_PATH_MAX, "med%llx", priv->handle);
    } else {
        struct sockaddr_rpmsg* addr = (struct sockaddr_rpmsg*)addr_;

        addr->rp_family = AF_RPMSG;
        snprintf(addr->rp_name, RPMSG_SOCKET_NAME_SIZE, "med%llx", priv->handle);
        snprintf(addr->rp_cpu, RPMSG_SOCKET_CPU_SIZE, "%s", priv->cpu);
    }

    return 0;
}

static int media_bind_socket(void* handle, char* url, size_t len)
{
    MediaProxyPriv* priv = handle;
    struct sockaddr_storage addr;
    int fd, ret;

    ret = media_get_sockaddr(handle, &addr);
    if (ret < 0)
        return ret;

    fd = socket(addr.ss_family, SOCK_STREAM, 0);
    if (fd < 0)
        return -errno;

    ret = bind(fd, (const struct sockaddr*)&addr, sizeof(addr));
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

static int media_prepare(int control, void* handle,
    const char* url, const char* options)
{
    MediaProxyPriv* priv = handle;
    int ret = -EINVAL;
    char tmp[32];
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
        ret = media_transact_once(control, priv, NULL, "set_options", options, 0, NULL, 0);
        if (ret < 0)
            goto out;
    }

    ret = media_transact_once(control, priv, NULL, "prepare", url, 0, NULL, 0);
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

    return ret;
}

static ssize_t media_process_data(void* handle, bool player,
    void* data, size_t len)
{
    MediaProxyPriv* priv = handle;
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

static void media_proxy_event_cb(void* cookie, media_parcel* msg)
{
    MediaProxyPriv* priv = cookie;
    const char* extra;
    int32_t event;
    int32_t ret;

    if (priv->event) {
        media_parcel_read_scanf(msg, "%i%i%s", &event, &ret, &extra);
        priv->event(priv->cookie, event, ret, extra);
    }
}

static int media_set_event_cb(int control, void* handle, void* cookie,
    media_event_callback event_cb)
{
    MediaProxyPriv* priv = handle;
    int ret;

    if (!priv)
        return -EINVAL;

    ret = media_client_set_event_cb(priv->proxy, priv->cpu, media_proxy_event_cb, priv);
    if (ret < 0)
        return ret;

    ret = media_transact(control, handle, NULL, "set_event", NULL, 0, NULL, 0, false);
    if (ret < 0)
        return ret;

    priv->event = event_cb;
    priv->cookie = cookie;

    return ret;
}

static int media_get_socket(void* handle)
{
    MediaProxyPriv* priv = handle;

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
    return media_transact(MEDIA_GRAPH_CONTROL, NULL, target, cmd, arg, 0, res, res_len, false);
}

void media_graph_dump(const char* options)
{
    media_transact(MEDIA_GRAPH_CONTROL, NULL, NULL, "dump", options, 0, NULL, 0, false);
}

void* media_player_open(const char* params)
{
    return media_open(MEDIA_PLAYER_CONTROL, params);
}

int media_player_close(void* handle, int pending_stop)
{
    return media_close(MEDIA_PLAYER_CONTROL, handle, pending_stop);
}

int media_player_set_event_callback(void* handle, void* cookie,
    media_event_callback event_cb)
{
    return media_set_event_cb(MEDIA_PLAYER_CONTROL, handle, cookie, event_cb);
}

int media_player_prepare(void* handle, const char* url, const char* options)
{
    syslog(LOG_INFO, "%s handle %p. \n", __func__, handle);
    return media_prepare(MEDIA_PLAYER_CONTROL, handle, url, options);
}

int media_player_reset(void* handle)
{
    media_close_socket(handle);

    return media_transact_once(MEDIA_PLAYER_CONTROL, handle, NULL, "reset", NULL, 0, NULL, 0);
}

ssize_t media_player_write_data(void* handle, const void* data, size_t len)
{
    return media_process_data(handle, true, (void*)data, len);
}

int media_player_get_sockaddr(void* handle, struct sockaddr_storage* addr)
{
    return media_get_sockaddr(handle, addr);
}

int media_player_get_socket(void* handle)
{
    return media_get_socket(handle);
}

int media_player_start(void* handle)
{
    if (!handle)
        return -EINVAL;
    return media_transact_once(MEDIA_PLAYER_CONTROL, handle, NULL, "start", NULL, 0, NULL, 0);
}

int media_player_stop(void* handle)
{
    MediaProxyPriv* priv = handle;

    if (!priv)
        return -EINVAL;

    if (atomic_load(&priv->refs) == 1 && priv->socket) {
        close(priv->socket);
        priv->socket = 0;
    }

    return media_transact_once(MEDIA_PLAYER_CONTROL, handle, NULL, "stop", NULL, 0, NULL, 0);
}

int media_player_pause(void* handle)
{
    return media_transact_once(MEDIA_PLAYER_CONTROL, handle, NULL, "pause", NULL, 0, NULL, 0);
}

int media_player_seek(void* handle, unsigned int msec)
{
    char tmp[32];

    snprintf(tmp, sizeof(tmp), "%u", msec);
    return media_transact_once(MEDIA_PLAYER_CONTROL, handle, NULL, "seek", tmp, 0, NULL, 0);
}

int media_player_set_looping(void* handle, int loop)
{
    char tmp[32];

    snprintf(tmp, sizeof(tmp), "%d", loop);
    return media_transact_once(MEDIA_PLAYER_CONTROL, handle, NULL, "set_loop", tmp, 0, NULL, 0);
}

int media_player_is_playing(void* handle)
{
    char tmp[32];
    int ret;

    ret = media_transact_once(MEDIA_PLAYER_CONTROL, handle, NULL, "get_playing", NULL, 0, tmp, sizeof(tmp));
    return ret < 0 ? ret : !!atoi(tmp);
}

int media_player_get_position(void* handle, unsigned int* msec)
{
    char tmp[32];
    int ret;

    if (!msec)
        return -EINVAL;

    ret = media_transact_once(MEDIA_PLAYER_CONTROL, handle, NULL, "get_position", NULL, 0, tmp, sizeof(tmp));
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

    ret = media_transact_once(MEDIA_PLAYER_CONTROL, handle, NULL, "get_duration", NULL, 0, tmp, sizeof(tmp));
    if (ret >= 0)
        *msec = strtoul(tmp, NULL, 0);

    return ret;
}

int media_player_set_volume(void* handle, float volume)
{
    char tmp[32];

    snprintf(tmp, sizeof(tmp), "%f", volume);
    return media_transact_once(MEDIA_PLAYER_CONTROL, handle, "volume", "volume", tmp, 0, NULL, 0);
}

int media_player_get_volume(void* handle, float* volume)
{
    char tmp[32];
    int ret;

    if (!volume)
        return -EINVAL;

    ret = media_transact_once(MEDIA_PLAYER_CONTROL, handle, "volume", "dump", NULL, 0, tmp, sizeof(tmp));
    if (ret >= 0) {
        sscanf(tmp, "vol:%f", volume);
        ret = 0;
    }

    return ret;
}

int media_player_set_property(void* handle, const char* target, const char* key, const char* value)
{
    return media_transact_once(MEDIA_PLAYER_CONTROL, handle, target, key, value, 0, NULL, 0);
}

int media_player_get_property(void* handle, const char* target, const char* key, char* value, int value_len)
{
    return media_transact_once(MEDIA_PLAYER_CONTROL, handle, target, key, NULL, 0, value, value_len);
}

void* media_recorder_open(const char* params)
{
    return media_open(MEDIA_RECORDER_CONTROL, params);
}

int media_recorder_close(void* handle)
{
    return media_close(MEDIA_RECORDER_CONTROL, handle, 0);
}

int media_recorder_set_event_callback(void* handle, void* cookie,
    media_event_callback event_cb)
{
    return media_set_event_cb(MEDIA_RECORDER_CONTROL, handle, cookie, event_cb);
}

int media_recorder_prepare(void* handle, const char* url, const char* options)
{
    return media_prepare(MEDIA_RECORDER_CONTROL, handle, url, options);
}

int media_recorder_reset(void* handle)
{
    media_close_socket(handle);

    return media_transact_once(MEDIA_RECORDER_CONTROL, handle, NULL, "reset", NULL, 0, NULL, 0);
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

int media_recorder_start(void* handle)
{
    return media_transact_once(MEDIA_RECORDER_CONTROL, handle, NULL, "start", NULL, 0, NULL, 0);
}

int media_recorder_pause(void* handle)
{
    return media_transact_once(MEDIA_RECORDER_CONTROL, handle, NULL, "pause", NULL, 0, NULL, 0);
}

int media_recorder_stop(void* handle)
{
    media_close_socket(handle);

    return media_transact_once(MEDIA_RECORDER_CONTROL, handle, NULL, "stop", NULL, 0, NULL, 0);
}

int media_recorder_set_property(void* handle, const char* target, const char* key, const char* value)
{
    return media_transact_once(MEDIA_RECORDER_CONTROL, handle, target, key, value, 0, NULL, 0);
}

int media_recorder_get_property(void* handle, const char* target, const char* key, char* value, int value_len)
{
    return media_transact_once(MEDIA_RECORDER_CONTROL, handle, target, key, NULL, 0, value, value_len);
}

int media_recorder_take_picture(char* params, char* filename, size_t number)
{
    char option[32];
    void* handle;
    int ret;

    if (!number || number > INT_MAX)
        return -EINVAL;

    handle = media_recorder_open(params);
    if (!handle)
        return -EINVAL;

    snprintf(option, sizeof(option), "total_number=%zu", number);

    ret = media_recorder_prepare(handle, filename, option);
    if (ret < 0)
        goto error;

    ret = media_recorder_start(handle);
    if (ret < 0)
        goto error;

    return media_close(MEDIA_RECORDER_CONTROL, handle, 1);

error:
    media_recorder_close(handle);
    return ret;
}

int media_policy_set_int(const char* name, int value, int apply)
{
    char tmp[32];

    snprintf(tmp, sizeof(tmp), "%d", value);
    return media_transact(MEDIA_POLICY_CONTROL, NULL, name, "set_int", tmp, apply, NULL, 0, false);
}

int media_policy_get_int(const char* name, int* value)
{
    char tmp[32];
    int ret;

    if (!value)
        return -EINVAL;

    ret = media_transact(MEDIA_POLICY_CONTROL, NULL, name, "get_int", NULL, 0, tmp, sizeof(tmp), false);
    if (ret >= 0) {
        *value = atoi(tmp);
        ret = 0;
    }

    return ret;
}

int media_policy_contain(const char* name, const char* values, int* result)
{
    char tmp[32];
    int ret;

    if (!result)
        return -EINVAL;

    ret = media_transact(MEDIA_POLICY_CONTROL, NULL, name, "contain", values, 0, tmp, sizeof(tmp), false);
    if (ret >= 0) {
        *result = atoi(tmp);
        ret = 0;
    }

    return ret;
}

int media_policy_set_string(const char* name, const char* value, int apply)
{
    return media_transact(MEDIA_POLICY_CONTROL, NULL, name, "set_string", value, apply, NULL, 0, false);
}

int media_policy_get_string(const char* name, char* value, int len)
{
    return media_transact(MEDIA_POLICY_CONTROL, NULL, name, "get_string", NULL, 0, value, len, false);
}

int media_policy_include(const char* name, const char* values, int apply)
{
    return media_transact(MEDIA_POLICY_CONTROL, NULL, name, "include", values, apply, NULL, 0, false);
}

int media_policy_exclude(const char* name, const char* values, int apply)
{
    return media_transact(MEDIA_POLICY_CONTROL, NULL, name, "exclude", values, apply, NULL, 0, false);
}

int media_policy_increase(const char* name, int apply)
{
    return media_transact(MEDIA_POLICY_CONTROL, NULL, name, "increase", NULL, apply, NULL, 0, false);
}

int media_policy_decrease(const char* name, int apply)
{
    return media_transact(MEDIA_POLICY_CONTROL, NULL, name, "decrease", NULL, apply, NULL, 0, false);
}

void media_policy_dump(const char* options)
{
    media_transact(MEDIA_POLICY_CONTROL, NULL, NULL, "dump", options, 0, NULL, 0, false);
}

int media_policy_get_stream_name(const char* stream, char* name, int len)
{
#ifdef CONFIG_PFW
    return media_policy_handler(media_get_policy(), stream, "get_string", NULL, 0, &name, len);
#else
    return media_transact(MEDIA_POLICY_CONTROL, NULL, stream, "get_string", NULL, 0, name, len, true);
#endif
}

int media_policy_set_stream_status(const char* name, bool active)
{
    const char* cmd = active ? "include" : "exclude";

    name = strchr(name, '@') + 1;

#ifdef CONFIG_PFW
    return media_policy_handler(media_get_policy(), "ActiveStreams", cmd, name, 1, NULL, 0);
#else
    return media_transact(MEDIA_POLICY_CONTROL, NULL, "ActiveStreams", cmd, name, 1, NULL, 0, true);
#endif
}

void media_policy_process_command(const char* target, const char* cmd, const char* arg)
{
#ifdef CONFIG_LIB_FFMPEG
    media_graph_handler(media_get_graph(), target, cmd, arg, NULL, 0);
#endif
    media_transact(MEDIA_GRAPH_CONTROL, NULL, target, cmd, arg, 0, NULL, 0, true);
}

void* media_session_open(const char* params)
{
    MediaProxyPriv* priv;
    char* stream_type;

    if (!params)
        return NULL;

    stream_type = strdup(params);
    if (!stream_type)
        return NULL;

    priv = media_open(MEDIA_SESSION_CONTROL, params);
    if (!priv) {
        free(stream_type);
        return NULL;
    }

    priv->stream_type = stream_type;
    return priv;
}

int media_session_set_event_callback(void* handle, void* cookie,
    media_event_callback event_cb)
{
    return media_set_event_cb(MEDIA_SESSION_CONTROL, handle, cookie, event_cb);
}

int media_session_close(void* handle)
{
    MediaProxyPriv* priv = handle;
    char* stream_type;
    int ret;

    if (!handle)
        return -EINVAL;

    stream_type = priv->stream_type;
    ret = media_close(MEDIA_SESSION_CONTROL, handle, 0);
    if (ret >= 0)
        free(stream_type);

    return ret;
}

int media_session_start(void* handle)
{
    return media_transact_once(MEDIA_SESSION_CONTROL, handle, NULL, "start", NULL, 0, NULL, 0);
}

int media_session_pause(void* handle)
{
    return media_transact_once(MEDIA_SESSION_CONTROL, handle, NULL, "pause", NULL, 0, NULL, 0);
}

int media_session_stop(void* handle)
{
    return media_transact_once(MEDIA_SESSION_CONTROL, handle, NULL, "stop", NULL, 0, NULL, 0);
}

int media_session_get_position(void* handle, unsigned int* msec)
{
    char tmp[32];
    int ret;

    if (!msec)
        return -EINVAL;

    ret = media_transact_once(MEDIA_SESSION_CONTROL, handle, NULL, "get_position", NULL, 0, tmp, sizeof(tmp));
    if (ret >= 0)
        *msec = strtoul(tmp, NULL, 0);

    return ret;
}

int media_session_get_duration(void* handle, unsigned int* msec)
{
    char tmp[32];
    int ret;

    if (!msec)
        return -EINVAL;

    ret = media_transact_once(MEDIA_SESSION_CONTROL, handle, NULL, "get_duration", NULL, 0, tmp, sizeof(tmp));
    if (ret >= 0)
        *msec = strtoul(tmp, NULL, 0);

    return ret;
}

int media_session_set_volume(void* handle, int volume)
{
    MediaProxyPriv* priv = handle;

    if (!handle)
        return -EINVAL;

    return media_policy_set_stream_volume(priv->stream_type, volume);
}

int media_session_get_volume(void* handle, int* volume)
{
    MediaProxyPriv* priv = handle;

    if (!handle)
        return -EINVAL;

    return media_policy_get_stream_volume(priv->stream_type, volume);
}

int media_session_increase_volume(void* handle)
{
    MediaProxyPriv* priv = handle;

    if (!handle)
        return -EINVAL;

    return media_policy_increase_stream_volume(priv->stream_type);
}

int media_session_decrease_volume(void* handle)
{
    MediaProxyPriv* priv = handle;

    if (!handle)
        return -EINVAL;

    return media_policy_decrease_stream_volume(priv->stream_type);
}

int media_session_next_song(void* handle)
{
    return media_transact_once(MEDIA_SESSION_CONTROL, handle, NULL, "next", NULL, 0, NULL, 0);
}

int media_session_prev_song(void* handle)
{
    return media_transact_once(MEDIA_SESSION_CONTROL, handle, NULL, "prev", NULL, 0, NULL, 0);
}
