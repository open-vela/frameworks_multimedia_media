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

#define MEDIA_COMMON_FIELDS \
    int type;               \
    void* proxy;            \
    char* cpu;              \
    uint64_t handle;

#define MEDIA_EVENT_FIELDS \
    void* cookie;          \
    media_event_callback event;

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct media_proxy_priv_t {
    MEDIA_COMMON_FIELDS
} media_proxy_priv_t;

typedef struct media_event_priv_t {
    MEDIA_COMMON_FIELDS
    MEDIA_EVENT_FIELDS
} media_event_priv_t;

typedef struct media_focus_priv_t {
    MEDIA_COMMON_FIELDS
    void* cookie;
    media_focus_callback suggest;
} media_focus_priv_t;

typedef struct media_io_priv_t {
    MEDIA_COMMON_FIELDS
    MEDIA_EVENT_FIELDS
    atomic_int refs;
    int socket;
} media_io_priv_t;

typedef struct media_session_priv_t {
    MEDIA_COMMON_FIELDS
    MEDIA_EVENT_FIELDS
    char* stream_type;
} media_session_priv_t;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/**
 * @brief Transact a command to server through existed long connection.
 *
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
static int
media_transact_once(void* handle, const char* target, const char* cmd,
    const char* arg, int apply, char* res, int res_len)
{
    media_proxy_priv_t* priv = handle;
    media_parcel in, out;
    const char* response;
    int ret = -EINVAL;
    int32_t resp = 0;
    const char* name;

    if (!priv || !priv->proxy)
        return ret;

    media_parcel_init(&in);
    media_parcel_init(&out);

    switch (priv->type) {
    case MEDIA_ID_FOCUS:
        name = "focus";
        ret = media_parcel_append_printf(&in, "%i%l%s%s%i", priv->type,
            priv->handle, target, cmd, res_len);
        break;

    case MEDIA_ID_GRAPH:
        name = "graph";
        ret = media_parcel_append_printf(&in, "%i%s%s%s%i", priv->type,
            target, cmd, arg, res_len);
        break;

    case MEDIA_ID_POLICY:
        name = "policy";
        ret = media_parcel_append_printf(&in, "%i%s%s%s%i%i", priv->type,
            target, cmd, arg, apply, res_len);
        break;

    case MEDIA_ID_PLAYER:
        name = "player";
        ret = media_parcel_append_printf(&in, "%i%l%s%s%s%i", priv->type,
            priv->handle, target, cmd, arg, res_len);
        break;

    case MEDIA_ID_RECORDER:
        name = "recorder";
        ret = media_parcel_append_printf(&in, "%i%l%s%s%s%i", priv->type,
            priv->handle, target, cmd, arg, res_len);
        break;

    case MEDIA_ID_SESSION:
        name = "session";
        ret = media_parcel_append_printf(&in, "%i%l%s%s%s%i", priv->type,
            priv->handle, target, cmd, arg, res_len);
        break;

    default:
        name = "none";
        goto out;
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
    media_proxy_priv_t *priv, priv_ = { 0 };
    char *saveptr, *cpu;
    int ret = -ENOSYS;
    char tmp[128];

    priv = handle ? handle : &priv_;
    priv->type = control;

    if (priv->proxy)
        return media_transact_once(priv, target, cmd, arg, apply, res, res_len);

    strlcpy(tmp, CONFIG_MEDIA_SERVER_CPUNAME, sizeof(tmp));
    for (cpu = strtok_r(tmp, MEDIA_DELIM, &saveptr); cpu;
         cpu = strtok_r(NULL, MEDIA_DELIM, &saveptr)) {
        if (remote && !strcmp(cpu, CONFIG_RPTUN_LOCAL_CPUNAME))
            continue;

        priv->proxy = media_client_connect(cpu);
        if (!priv->proxy)
            continue;

        priv->cpu = cpu;
        ret = media_transact_once(priv, target, cmd, arg, apply, res, res_len);
        switch (control) {
        case MEDIA_ID_GRAPH:
            media_client_disconnect(priv->proxy);
            ret = 0;
            break;

        case MEDIA_ID_POLICY:
            media_client_disconnect(priv->proxy);
            if (ret != -ENOSYS)
                return ret; // return once found policy

            break;

        case MEDIA_ID_FOCUS:
        case MEDIA_ID_PLAYER:
        case MEDIA_ID_RECORDER:
        case MEDIA_ID_SESSION:
            if (ret < 0) {
                media_client_disconnect(priv->proxy);
                break;
            }

            /* keep the connection in need. */

            if (handle) {
                priv->cpu = strdup(cpu);
                DEBUGASSERT(priv->cpu);
            } else
                media_client_disconnect(priv->proxy);

            return 0;
        }
    }

    priv->proxy = NULL;
    priv->cpu = NULL;
    return ret;
}

/**
 * @brief Common release method.
 */
static void media_default_release_cb(void* handle)
{
    media_proxy_priv_t* priv = handle;

    free(priv->cpu);
    free(priv);
}

static void media_transact_finalize(void* handle, void* release_cb)
{
    media_proxy_priv_t* priv = handle;

    if (!release_cb)
        release_cb = media_default_release_cb;

    media_client_set_release_cb(priv->proxy, release_cb, handle);
    media_client_disconnect(priv->proxy);
}

/**
 * @brief Player/Recorder release method.
 */
static void media_release_cb(void* handle)
{
    media_io_priv_t* priv = handle;

    if (atomic_fetch_sub(&priv->refs, 1) == 1) {
        free(priv->cpu);
        free(priv);
    }
}

static void* media_open(int control, const char* params)
{
    media_io_priv_t* priv;
    char tmp[32];

    priv = zalloc(sizeof(media_io_priv_t));
    if (!priv)
        return NULL;

    if (media_transact(control, priv, NULL, "open", params, 0, tmp, sizeof(tmp), 0) < 0)
        goto err;

    sscanf(tmp, "%" PRIu64 "", &priv->handle);
    if (!priv->handle)
        goto err;

    atomic_store(&priv->refs, 1);
    syslog(LOG_INFO, "%s:%s handle:%p\n", __func__, params, (void*)(uintptr_t)priv->handle);
    return priv;

err:
    media_transact_finalize(priv, media_release_cb);
    return NULL;
}

static void media_close_socket(void* handle)
{
    media_io_priv_t* priv = handle;

    if (atomic_load(&priv->refs) == 1 && priv->socket) {
        close(priv->socket);
        priv->socket = 0;
    }
}

static int media_close(void* handle, int pending_stop)
{
    media_io_priv_t* priv = handle;
    char tmp[32];
    int ret;

    snprintf(tmp, sizeof(tmp), "%d", pending_stop);
    ret = media_transact_once(priv, NULL, "close", tmp, 0, NULL, 0);
    if (ret < 0)
        return ret;

    media_close_socket(priv);
    media_transact_finalize(priv, media_release_cb);
    return ret;
}

static int media_get_sockaddr(void* handle, struct sockaddr_storage* addr_)
{
    media_io_priv_t* priv = handle;

    if (!handle || !addr_)
        return -EINVAL;

    if (!strcmp(priv->cpu, CONFIG_RPTUN_LOCAL_CPUNAME)) {
        struct sockaddr_un* addr = (struct sockaddr_un*)addr_;

        addr->sun_family = AF_UNIX;
        snprintf(addr->sun_path, UNIX_PATH_MAX, "med%" PRIx64 "", priv->handle);
    } else {
        struct sockaddr_rpmsg* addr = (struct sockaddr_rpmsg*)addr_;

        addr->rp_family = AF_RPMSG;
        snprintf(addr->rp_name, RPMSG_SOCKET_NAME_SIZE, "med%" PRIx64 "", priv->handle);
        snprintf(addr->rp_cpu, RPMSG_SOCKET_CPU_SIZE, "%s", priv->cpu);
    }

    return 0;
}

static int media_bind_socket(void* handle, char* url, size_t len)
{
    media_io_priv_t* priv = handle;
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
        snprintf(url, len, "unix:med%" PRIx64 "?listen=0", priv->handle);
    else
        snprintf(url, len, "rpmsg:med%" PRIx64 ":%s?listen=0", priv->handle,
            CONFIG_RPTUN_LOCAL_CPUNAME);

    return fd;

out:
    close(fd);
    return -errno;
}

static int media_prepare(void* handle, const char* url, const char* options)
{
    media_io_priv_t* priv = handle;
    int ret = -EINVAL;
    char tmp[32];
    int fd = 0;

    if (!priv || priv->socket > 0)
        return ret;

    if (!url || !url[0]) {
        fd = media_bind_socket(handle, tmp, sizeof(tmp));
        if (fd < 0)
            return fd;

        url = tmp;
    }

    if (options && options[0] != '\0') {
        ret = media_transact_once(priv, NULL, "set_options", options, 0, NULL, 0);
        if (ret < 0)
            goto out;
    }

    ret = media_transact_once(priv, NULL, "prepare", url, 0, NULL, 0);
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
    media_io_priv_t* priv = handle;
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

static void media_suggest_cb(void* cookie, media_parcel* msg)
{
    media_focus_priv_t* priv = cookie;
    const char* extra;
    int32_t event;
    int32_t ret;

    media_parcel_read_scanf(msg, "%i%i%s", &event, &ret, &extra);
    priv->suggest(event, priv->cookie);
}

static void media_event_cb(void* cookie, media_parcel* msg)
{
    media_event_priv_t* priv = cookie;
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
    media_event_priv_t* priv = handle;
    int ret;

    if (!priv)
        return -EINVAL;

    ret = media_client_set_event_cb(priv->proxy, priv->cpu, media_event_cb, priv);
    if (ret < 0)
        return ret;

    ret = media_transact_once(handle, NULL, "set_event", NULL, 0, NULL, 0);
    if (ret < 0)
        return ret;

    priv->event = event_cb;
    priv->cookie = cookie;

    return ret;
}

static int media_get_socket(void* handle)
{
    media_io_priv_t* priv = handle;

    if (!priv || priv->socket <= 0)
        return -EINVAL;

    return priv->socket;
}

static int media_get_proper_stream(void* handle, char* stream_type, int len)
{
    media_session_priv_t* priv = handle;
    int ret;

    if (!handle)
        return -EINVAL;

    ret = media_transact(MEDIA_ID_FOCUS, NULL, NULL, "peek", NULL, 0, stream_type, len, false);
    if (ret <= 0)
        ret = snprintf(stream_type, len, "%s", priv->stream_type);

    return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void* media_focus_request(int* suggestion, const char* stream_type,
    media_focus_callback cb, void* cookie)
{
    media_focus_priv_t* priv;
    char tmp[64];
    int ret;

    if (!suggestion || !stream_type)
        return NULL;

    priv = zalloc(sizeof(media_focus_priv_t));
    if (!priv)
        return NULL;

    ret = media_transact(MEDIA_ID_FOCUS, priv, stream_type, "request", NULL, 0,
        tmp, sizeof(tmp), false);
    if (ret < 0)
        goto err;

    sscanf(tmp, "%" PRIu64 ":%d", &priv->handle, suggestion);
    if (!priv->handle)
        goto err;

    /**
     * NOTE: on STOP suggestion, we still receive a handle, but we actually
     * didn't enter the focus stack; thus there are no more suggestions from
     * server, we shall not create listener thread.
     */
    if (*suggestion != MEDIA_FOCUS_STOP) {
        priv->suggest = cb;
        priv->cookie = cookie;
        ret = media_client_set_event_cb(priv->proxy, priv->cpu, media_suggest_cb, priv);
        if (ret < 0)
            goto err;
    }

    return priv;

err:
    media_transact_finalize(priv, NULL);
    return NULL;
}

int media_focus_abandon(void* handle)
{
    media_focus_priv_t* priv = handle;
    int ret;

    ret = media_transact(MEDIA_ID_FOCUS, priv, NULL, "abandon", NULL, 0, NULL, 0, false);
    if (ret >= 0)
        media_transact_finalize(priv, NULL);

    return ret;
}

void media_focus_dump(const char* options)
{
    media_transact(MEDIA_ID_FOCUS, NULL, NULL, "dump", options, 0, NULL, 0, false);
}

int media_process_command(const char* target, const char* cmd,
    const char* arg, char* res, int res_len)
{
    return media_transact(MEDIA_ID_GRAPH, NULL, target, cmd, arg, 0, res, res_len, false);
}

void media_graph_dump(const char* options)
{
    media_transact(MEDIA_ID_GRAPH, NULL, NULL, "dump", options, 0, NULL, 0, false);
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
    syslog(LOG_INFO, "%s handle %p. \n", __func__, handle);
    return media_prepare(handle, url, options);
}

int media_player_reset(void* handle)
{
    if (!handle)
        return -EINVAL;

    media_close_socket(handle);

    return media_transact_once(handle, NULL, "reset", NULL, 0, NULL, 0);
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

    return media_transact_once(handle, NULL, "start", NULL, 0, NULL, 0);
}

int media_player_stop(void* handle)
{
    if (!handle)
        return -EINVAL;

    media_close_socket(handle);

    return media_transact_once(handle, NULL, "stop", NULL, 0, NULL, 0);
}

int media_player_pause(void* handle)
{
    return media_transact_once(handle, NULL, "pause", NULL, 0, NULL, 0);
}

int media_player_seek(void* handle, unsigned int msec)
{
    char tmp[32];

    snprintf(tmp, sizeof(tmp), "%u", msec);
    return media_transact_once(handle, NULL, "seek", tmp, 0, NULL, 0);
}

int media_player_set_looping(void* handle, int loop)
{
    char tmp[32];

    snprintf(tmp, sizeof(tmp), "%d", loop);
    return media_transact_once(handle, NULL, "set_loop", tmp, 0, NULL, 0);
}

int media_player_is_playing(void* handle)
{
    char tmp[32];
    int ret;

    ret = media_transact_once(handle, NULL, "get_playing", NULL, 0, tmp, sizeof(tmp));
    return ret < 0 ? ret : !!atoi(tmp);
}

int media_player_get_position(void* handle, unsigned int* msec)
{
    char tmp[32];
    int ret;

    if (!msec)
        return -EINVAL;

    ret = media_transact_once(handle, NULL, "get_position", NULL, 0, tmp, sizeof(tmp));
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

    ret = media_transact_once(handle, NULL, "get_duration", NULL, 0, tmp, sizeof(tmp));
    if (ret >= 0)
        *msec = strtoul(tmp, NULL, 0);

    return ret;
}

int media_player_set_volume(void* handle, float volume)
{
    char tmp[32];

    snprintf(tmp, sizeof(tmp), "%f", volume);
    return media_transact_once(handle, "volume", "volume", tmp, 0, NULL, 0);
}

int media_player_get_volume(void* handle, float* volume)
{
    char tmp[32];
    int ret;

    if (!volume)
        return -EINVAL;

    ret = media_transact_once(handle, "volume", "dump", NULL, 0, tmp, sizeof(tmp));
    if (ret >= 0) {
        sscanf(tmp, "vol:%f", volume);
        ret = 0;
    }

    return ret;
}

int media_player_set_property(void* handle, const char* target, const char* key, const char* value)
{
    return media_transact_once(handle, target, key, value, 0, NULL, 0);
}

int media_player_get_property(void* handle, const char* target, const char* key, char* value, int value_len)
{
    return media_transact_once(handle, target, key, NULL, 0, value, value_len);
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

    return media_transact_once(handle, NULL, "reset", NULL, 0, NULL, 0);
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
    return media_transact_once(handle, NULL, "start", NULL, 0, NULL, 0);
}

int media_recorder_pause(void* handle)
{
    return media_transact_once(handle, NULL, "pause", NULL, 0, NULL, 0);
}

int media_recorder_stop(void* handle)
{
    if (!handle)
        return -EINVAL;

    media_close_socket(handle);

    return media_transact_once(handle, NULL, "stop", NULL, 0, NULL, 0);
}

int media_recorder_set_property(void* handle, const char* target, const char* key, const char* value)
{
    return media_transact_once(handle, target, key, value, 0, NULL, 0);
}

int media_recorder_get_property(void* handle, const char* target, const char* key, char* value, int value_len)
{
    return media_transact_once(handle, target, key, NULL, 0, value, value_len);
}

int media_recorder_take_picture(char* params, char* filename, size_t number,
    media_event_callback event_cb, void* cookie)
{
    char option[32];
    void* handle;
    int ret;

    if (!number || number > INT_MAX)
        return -EINVAL;

    handle = media_recorder_open(params);
    if (!handle)
        return -EINVAL;

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

    return media_close(handle, 1);

error:
    media_recorder_close(handle);
    return ret;
}

int media_policy_set_int(const char* name, int value, int apply)
{
    char tmp[32];

    snprintf(tmp, sizeof(tmp), "%d", value);
    return media_transact(MEDIA_ID_POLICY, NULL, name, "set_int", tmp, apply, NULL, 0, false);
}

int media_policy_get_int(const char* name, int* value)
{
    char tmp[32];
    int ret;

    if (!value)
        return -EINVAL;

    ret = media_transact(MEDIA_ID_POLICY, NULL, name, "get_int", NULL, 0, tmp, sizeof(tmp), false);
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

    ret = media_transact(MEDIA_ID_POLICY, NULL, name, "contain", values, 0, tmp, sizeof(tmp), false);
    if (ret >= 0) {
        *result = atoi(tmp);
        ret = 0;
    }

    return ret;
}

int media_policy_set_string(const char* name, const char* value, int apply)
{
    return media_transact(MEDIA_ID_POLICY, NULL, name, "set_string", value, apply, NULL, 0, false);
}

int media_policy_get_string(const char* name, char* value, int len)
{
    return media_transact(MEDIA_ID_POLICY, NULL, name, "get_string", NULL, 0, value, len, false);
}

int media_policy_include(const char* name, const char* values, int apply)
{
    return media_transact(MEDIA_ID_POLICY, NULL, name, "include", values, apply, NULL, 0, false);
}

int media_policy_exclude(const char* name, const char* values, int apply)
{
    return media_transact(MEDIA_ID_POLICY, NULL, name, "exclude", values, apply, NULL, 0, false);
}

int media_policy_increase(const char* name, int apply)
{
    return media_transact(MEDIA_ID_POLICY, NULL, name, "increase", NULL, apply, NULL, 0, false);
}

int media_policy_decrease(const char* name, int apply)
{
    return media_transact(MEDIA_ID_POLICY, NULL, name, "decrease", NULL, apply, NULL, 0, false);
}

void media_policy_dump(const char* options)
{
    media_transact(MEDIA_ID_POLICY, NULL, NULL, "dump", options, 0, NULL, 0, false);
}

int media_policy_get_stream_name(const char* stream, char* name, int len)
{
#ifdef CONFIG_LIB_PFW
    return media_policy_handler(media_get_policy(), stream, "get_string", NULL, 0, name, len);
#else
    return media_transact(MEDIA_ID_POLICY, NULL, stream, "get_string", NULL, 0, name, len, true);
#endif
}

int media_policy_set_stream_status(const char* name, bool active)
{
    const char* cmd = active ? "include" : "exclude";

    name = strchr(name, '@') + 1;

#ifdef CONFIG_LIB_PFW
    return media_policy_handler(media_get_policy(), "ActiveStreams", cmd, name, 1, NULL, 0);
#else
    return media_transact(MEDIA_ID_POLICY, NULL, "ActiveStreams", cmd, name, 1, NULL, 0, true);
#endif
}

void media_policy_process_command(const char* target, const char* cmd, const char* arg)
{
#ifdef CONFIG_LIB_FFMPEG
    media_graph_handler(media_get_graph(), target, cmd, arg, NULL, 0);
#endif
    media_transact(MEDIA_ID_GRAPH, NULL, target, cmd, arg, 0, NULL, 0, true);
}

void* media_session_open(const char* params)
{
    media_session_priv_t* priv;
    char tmp[64];

    if (!params)
        return NULL;

    priv = zalloc(sizeof(media_session_priv_t));
    if (!priv)
        return NULL;

    if (media_transact(MEDIA_ID_SESSION, priv, NULL, "open", params, 0, tmp, sizeof(tmp), 0) < 0)
        goto err;

    sscanf(tmp, "%" PRIu64 "", &priv->handle);
    if (!priv->handle)
        goto err;

    priv->stream_type = strdup(params);
    if (!priv->stream_type)
        goto err;

    return priv;

err:
    media_transact_finalize(priv, NULL);
    return NULL;
}

int media_session_set_event_callback(void* handle, void* cookie,
    media_event_callback event_cb)
{
    return media_set_event_cb(handle, cookie, event_cb);
}

int media_session_close(void* handle)
{
    media_session_priv_t* priv = handle;
    int ret;

    if (!handle)
        return -EINVAL;

    ret = media_transact_once(priv, NULL, "close", NULL, 0, NULL, 0);
    if (ret < 0)
        return ret;

    free(priv->stream_type);
    media_transact_finalize(priv, NULL);
    return ret;
}

int media_session_start(void* handle)
{
    return media_transact_once(handle, NULL, "start", NULL, 0, NULL, 0);
}

int media_session_pause(void* handle)
{
    return media_transact_once(handle, NULL, "pause", NULL, 0, NULL, 0);
}

int media_session_seek(void* handle, unsigned int msec)
{
    char tmp[32];

    snprintf(tmp, sizeof(tmp), "%u", msec);
    return media_transact_once(handle, NULL, "seek", tmp, 0, NULL, 0);
}

int media_session_stop(void* handle)
{
    return media_transact_once(handle, NULL, "stop", NULL, 0, NULL, 0);
}

int media_session_get_state(void* handle, int* state)
{
    return -ENOSYS;
}

int media_session_get_position(void* handle, unsigned int* msec)
{
    return -ENOSYS;
}

int media_session_get_duration(void* handle, unsigned int* msec)
{
    return -ENOSYS;
}

int media_session_set_volume(void* handle, int volume)
{
    char tmp[32];
    int ret;

    ret = media_get_proper_stream(handle, tmp, sizeof(tmp));
    if (ret < 0)
        return ret;

    return media_policy_set_stream_volume(tmp, volume);
}

int media_session_get_volume(void* handle, int* volume)
{
    char tmp[32];
    int ret;

    ret = media_get_proper_stream(handle, tmp, sizeof(tmp));
    if (ret < 0)
        return ret;

    return media_policy_get_stream_volume(tmp, volume);
}

int media_session_increase_volume(void* handle)
{
    char tmp[32];
    int ret;

    ret = media_get_proper_stream(handle, tmp, sizeof(tmp));
    if (ret < 0)
        return ret;

    return media_policy_increase_stream_volume(tmp);
}

int media_session_decrease_volume(void* handle)
{
    char tmp[32];
    int ret;

    ret = media_get_proper_stream(handle, tmp, sizeof(tmp));
    if (ret < 0)
        return ret;

    return media_policy_decrease_stream_volume(tmp);
}

int media_session_next_song(void* handle)
{
    return media_transact_once(handle, NULL, "next", NULL, 0, NULL, 0);
}

int media_session_prev_song(void* handle)
{
    return media_transact_once(handle, NULL, "prev", NULL, 0, NULL, 0);
}

void* media_session_register(void* cookie, media_event_callback event_cb)
{
    media_session_priv_t* priv;
    char tmp[64];

    priv = zalloc(sizeof(media_session_priv_t));
    if (!priv)
        return NULL;

    if (media_transact(MEDIA_ID_SESSION, priv, NULL, "register", NULL, 0, tmp, sizeof(tmp), 0) < 0)
        goto err;

    sscanf(tmp, "%" PRIu64 "", &priv->handle);
    if (!priv->handle)
        goto err;

    if (media_client_set_event_cb(priv->proxy, priv->cpu, media_event_cb, priv) < 0)
        goto err;

    priv->event = event_cb;
    priv->cookie = cookie;

    return priv;

err:
    media_transact_finalize(priv, NULL);
    return NULL;
}

int media_session_notify(void* handle, int event, int result, const char* extra)
{
    char tmp[64];

    if (!MEDIA_IS_STATUS_CHANGE(event))
        return -EINVAL;

    snprintf(tmp, sizeof(tmp), "%d:%d", event, result);
    return media_transact_once(handle, extra, "event", tmp, 0, NULL, 0);
}

int media_session_unregister(void* handle)
{
    media_session_priv_t* priv = handle;
    int ret;

    if (!handle)
        return -EINVAL;

    ret = media_transact_once(priv, NULL, "unregister", NULL, 0, NULL, 0);
    if (ret < 0)
        return ret;

    media_transact_finalize(priv, NULL);
    return ret;
}
