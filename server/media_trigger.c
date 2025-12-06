/****************************************************************************
 * frameworks/media/server/media_trigger.c
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
#include <media_recorder.h>
#include <media_trigger_model.h>
#include <netinet/in.h>
#include <netpacket/rpmsg.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/sysinfo.h>
#include <sys/un.h>
#include <unistd.h>

#include "media_common.h"
#include "media_plugin.h"
#include "media_server.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MAX_RECORDER_OPTIONS_LEN 128

enum {
    SOUND_TRIGGER_STATE_NOP,
    SOUND_TRIGGER_STATE_OPENED,
    SOUND_TRIGGER_STATE_SET_EVENTED,
    SOUND_TRIGGER_STATE_LOADED,
    SOUND_TRIGGER_STATE_STARTED,
    SOUND_TRIGGER_STATE_STOPPED,
    SOUND_TRIGGER_STATE_UNLOADED,
};

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct MediaTriggerContext {
    void* context;
    void* handle;
    int state;
    bool exit;
    int tran_fd;
    int notify_fd;
    uint32_t offset;
    char* buffer;
    size_t buffer_size;
    media_parcel parcel;
} MediaTriggerContext;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static inline bool media_trigger_is_exit(MediaTriggerContext* ctx)
{
    return ctx->exit;
}

static void media_trigger_notify_finalize(MediaTriggerContext* ctx)
{
    if (ctx->notify_fd >= 0) {
        close(ctx->notify_fd);
        ctx->notify_fd = -1;
        ctx->offset = 0;
    }
}

static int media_trigger_notify_event(MediaTriggerContext* ctx, int event,
    int result, const char* extra)
{
    media_parcel notify;
    int ret = -EINVAL;

    media_parcel_init(&notify);
    media_parcel_append_printf(&notify, "%i%i%s", event, result, extra);
    if (ctx->notify_fd >= 0)
        ret = media_parcel_send(&notify, ctx->notify_fd, MEDIA_PARCEL_SEND, 0);

    media_parcel_deinit(&notify);
    return ret;
}

static int media_trigger_stop_recorder(MediaTriggerContext* ctx)
{
    int ret;

    ret = media_recorder_stop(ctx->handle);
    if (ret < 0) {
        MEDIA_ERR("stop recorder failed:%d\n", ret);
        return ret;
    }

    ret = media_recorder_close(ctx->handle);
    ctx->handle = NULL;
    free(ctx->buffer);
    ctx->buffer = NULL;

    return ret;
}

static int media_trigger_start_recorder(MediaTriggerContext* ctx, const char* options)
{
    int ret;

    ctx->handle = media_recorder_open("Capture");
    if (!ctx->handle) {
        MEDIA_ERR("Recorder: open failed. \n");
        return -EINVAL;
    }

    ret = media_recorder_prepare(ctx->handle, NULL, options);
    if (ret < 0) {
        MEDIA_ERR("Recorder: prepare failed. \n");
        goto out;
    }

    ret = media_recorder_start(ctx->handle);
    if (ret < 0) {
        MEDIA_ERR("Recorder: start failed. \n");
        goto out;
    }

    media_trigger_model_get_buffer_size(ctx->context, &ctx->buffer_size);
    if (ctx->buffer_size <= 0) {
        ret = -EINVAL;
        goto out;
    }

    ctx->buffer = malloc(ctx->buffer_size);
    if (!ctx->buffer) {
        ret = -ENOMEM;
        goto out;
    }

    return ret;

out:
    media_recorder_close(ctx->handle);
    ctx->handle = NULL;
    return ret;
}

static void media_trigger_conn_close(MediaTriggerContext* ctx)
{
    close(ctx->tran_fd);
    ctx->tran_fd = -1;
    ctx->offset = 0;
    media_parcel_deinit(&ctx->parcel);
}

static MediaTriggerContext* media_trigger_ctx_init(void)
{
    MediaTriggerContext* ctx = NULL;

    ctx = malloc(sizeof(MediaTriggerContext));
    if (!ctx)
        return NULL;

    ctx->state = SOUND_TRIGGER_STATE_NOP;
    ctx->context = NULL;
    ctx->handle = NULL;
    ctx->notify_fd = -1;
    ctx->tran_fd = -1;
    ctx->offset = 0;
    ctx->exit = false;
    media_parcel_init(&ctx->parcel);

    return ctx;
}

static void media_trigger_ctx_release(MediaTriggerContext* ctx)
{
    media_parcel_deinit(&ctx->parcel);
    free(ctx);
}

static int media_trigger_create_notify(MediaTriggerContext* ctx, media_parcel* parcel)
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

    if (strcmp(cpu, CONFIG_RPMSG_LOCAL_CPUNAME)) {
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

static void media_trigger_onreceive(MediaTriggerContext* ctx, media_parcel* in, media_parcel* out)
{
    char *cmd = NULL, *arg = NULL, *response = NULL;
    char options[MAX_RECORDER_OPTIONS_LEN];
    size_t size = 0, resp = 0;
    const void* data = NULL;
    int id = 0, ret = 0;

    media_parcel_read_int32(in, &id);
    if (id != MEDIA_ID_TRIGGER) {
        ret = -EINVAL;
        goto outside;
    }

    media_parcel_read_scanf(in, "%s%s%i%i", &cmd, &arg, &size, &resp);
    if (!strcmp(cmd, "set_event")) {
        ctx->state = SOUND_TRIGGER_STATE_SET_EVENTED;
    } else if (!strcmp(cmd, "load")) {
        if (ctx->state < SOUND_TRIGGER_STATE_SET_EVENTED || size <= 0) {
            ret = -EINVAL;
            goto outside;
        }

        data = media_parcel_read(in, size);
        ctx->context = media_trigger_model_load(data, size);
        if (!ctx->context) {
            MEDIA_ERR("load model failed\n");
            goto outside;
        }

        ctx->state = SOUND_TRIGGER_STATE_LOADED;
    } else if (!strcmp(cmd, "start")) {
        if (ctx->state != SOUND_TRIGGER_STATE_LOADED && ctx->state != SOUND_TRIGGER_STATE_STOPPED) {
            if (ctx->state == SOUND_TRIGGER_STATE_STARTED) {
                goto outside;
            }
            ret = -EINVAL;
            goto outside;
        }

        media_trigger_model_get_options(ctx->context, options, MAX_RECORDER_OPTIONS_LEN);
        MEDIA_INFO("recorder options:%s\n", options);

        ret = media_trigger_start_recorder(ctx, options);
        if (ret < 0)
            goto outside;

        ctx->state = SOUND_TRIGGER_STATE_STARTED;
    } else if (!strcmp(cmd, "stop")) {
        if (ctx->state != SOUND_TRIGGER_STATE_STARTED) {
            ret = -EINVAL;
            goto outside;
        }

        ret = media_trigger_stop_recorder(ctx);
        if (ret < 0)
            goto outside;

        ctx->state = SOUND_TRIGGER_STATE_STOPPED;
    } else if (!strcmp(cmd, "unload")) {
        if (ctx->state < SOUND_TRIGGER_STATE_LOADED) {
            ret = -EINVAL;
            goto outside;
        }

        /* Stop recorder first if still running */
        if (ctx->state == SOUND_TRIGGER_STATE_STARTED) {
            ret = media_trigger_stop_recorder(ctx);
            if (ret < 0) {
                MEDIA_ERR("unload: auto stop recorder failed:%d\n", ret);
                goto outside;
            }
            ctx->state = SOUND_TRIGGER_STATE_STOPPED;
        }

        media_trigger_model_unload(ctx->context);
        ctx->context = NULL;
        ctx->state = SOUND_TRIGGER_STATE_UNLOADED;
    } else if (!strcmp(cmd, "close")) {
        /* Stop recorder first if still running */
        if (ctx->state == SOUND_TRIGGER_STATE_STARTED) {
            ret = media_trigger_stop_recorder(ctx);
            if (ret < 0) {
                MEDIA_ERR("close: stop recorder failed:%d\n", ret);
            }
        }

        /* Unload model if still loaded */
        if (ctx->state >= SOUND_TRIGGER_STATE_LOADED && ctx->state < SOUND_TRIGGER_STATE_UNLOADED) {
            media_trigger_model_unload(ctx->context);
            ctx->context = NULL;
        }

        ctx->state = SOUND_TRIGGER_STATE_NOP;
        media_trigger_notify_finalize(ctx);
        ctx->exit = true;
    } else if (!strcmp(cmd, "get_property")) {
        if (resp > 0)
            response = zalloc(resp);

        if (!response) {
            ret = -ENOMEM;
            goto outside;
        }

        media_trigger_model_get_properties(response, &resp);
        ret = resp;
    } else
        ret = -ENOSYS;

outside:
    if (out)
        media_parcel_append_printf(out, "%i%s", ret, response);

    if (ret < 0) {
        MEDIA_INFO("%s: %s %s %" PRId32 " %s\n",
            media_id_get_name(id), cmd, arg ? arg : "_",
            ret, response ? response : "_");
    }

    if (response)
        free(response);
}

static int media_trigger_poll_available(MediaTriggerContext* ctx)
{
    struct pollfd fds[1];
    struct pollfd* fd = &fds[0];
    fds[0].fd = ctx->tran_fd;
    fds[0].events = POLLIN;
    fds[0].revents = 0;
    media_parcel ack;
    uint32_t code;
    int ret;

    ret = poll(fds, 1, 5);
    if (ret == -1) {
        MEDIA_ERR("poll failed err=%d\n", -errno);
    } else if (ret == 0) {
        MEDIA_DEBUG("poll timeout\n");
    }

    if (ret < 0 && ret != -EAGAIN && ret != -EPIPE) {
        MEDIA_ERR("poll_available failed %d\n", ret);
        return ret;
    }

    if (fd->revents & POLLERR)
        goto out;

    while (1) {
        ret = media_parcel_recv(&ctx->parcel, ctx->tran_fd, &ctx->offset, MSG_DONTWAIT);
        if (ret < 0)
            break;

        code = media_parcel_get_code(&ctx->parcel);
        switch (code) {
        case MEDIA_PARCEL_SEND:
            media_trigger_onreceive(ctx, &ctx->parcel, NULL);
            break;

        case MEDIA_PARCEL_SEND_ACK:
            media_parcel_init(&ack);
            media_trigger_onreceive(ctx, &ctx->parcel, &ack);
            ret = media_parcel_send(&ack, ctx->tran_fd, MEDIA_PARCEL_REPLY, 0);
            media_parcel_deinit(&ack);
            break;

        case MEDIA_PARCEL_CREATE_NOTIFY:
            ret = media_trigger_create_notify(ctx, &ctx->parcel);
            if (ret > 0)
                ctx->notify_fd = ret;
            else
                MEDIA_ERR("create notify failed %d\n", ret);
            break;
        default:
            break;
        }

        media_parcel_reinit(&ctx->parcel);
        ctx->offset = 0;
    }

    if (((fd->revents & POLLIN) && ret == -EPIPE) || (fd->revents & POLLHUP))
        goto out;

    return ret;

out:
    MEDIA_INFO("fd:%d revent:%d\n", fd->fd, (int)fd->revents);
    media_trigger_conn_close(ctx);
    return 0;
}

static void media_trigger_poll(MediaTriggerContext* ctx)
{
    bool detected;
    int ret;

    ret = media_trigger_poll_available(ctx);
    if (ret < 0 && ret != -EAGAIN)
        return;

    if (ctx->state == SOUND_TRIGGER_STATE_STARTED) {
        ret = media_recorder_read_data(ctx->handle, ctx->buffer, ctx->buffer_size);
        if (ret == ctx->buffer_size) {
            detected = media_trigger_model_detect_hotword(ctx->context, ctx->buffer, ctx->buffer_size);
            if (detected) {
                MEDIA_INFO("Hotword detected\n");
                media_trigger_notify_event(ctx, 0, 0, NULL);
            }
        }
    }
}

static void* media_trigger_thread(void* arg)
{
    MediaTriggerContext* ctx = (MediaTriggerContext*)arg;
    MEDIA_INFO("create trigger thread.\n");

    while (1) {
        if (media_trigger_is_exit(ctx))
            break;

        media_trigger_poll(ctx);
    }

    media_trigger_ctx_release(ctx);
    MEDIA_INFO("exit trigger thread.\n");

    return NULL;
}

static int media_trigger_open(MediaTriggerContext* ctx)
{
    struct sched_param param;
    pthread_attr_t attr;
    pthread_t thread;
    int ret;

    pthread_attr_init(&attr);
    pthread_attr_getschedparam(&attr, &param);
    pthread_attr_setstacksize(&attr, CONFIG_MEDIA_TRIGGER_STACKSIZE);
    param.sched_priority = CONFIG_MEDIA_TRIGGER_PRIORITY;
    pthread_attr_setschedparam(&attr, &param);
    ret = pthread_create(&thread, &attr, media_trigger_thread, ctx);
    if (ret != 0)
        return ret;

    pthread_setname_np(thread, "media_trigger");
    pthread_detach(thread);

    return 0;
}

static int media_trigger_handler(struct MediadPlugin* pctx, struct media_server_conn* conn,
    const char* target, const char* cmd, const char* arg, int flags, char* res, int res_len)
{
    MediaTriggerContext* ctx = NULL;
    int ret = 0;

    MEDIA_INFO("media trigger cmd:%s arg:%s flags:%d res:%s res_len:%d\n",
        cmd, arg ? arg : "_", flags, res ? res : "_", res_len);

    if (!strcmp(cmd, "open")) {
        ctx = media_trigger_ctx_init();
        if (!ctx)
            return -ENOMEM;

        ctx->tran_fd = media_server_get_tran_fd(conn);
        if (ctx->tran_fd < 0) {
            MEDIA_ERR("trigger get tran fd failed...\n");
            media_trigger_ctx_release(ctx);
            return -EINVAL;
        }

        media_server_clean_conn(conn);
        ret = media_trigger_open(ctx);
        if (ret < 0) {
            media_trigger_ctx_release(ctx);
            return ret;
        }

        MEDIA_INFO("media trigger open success\n");
    }

    return 0;
}

MediadPlugin media_trigger_plugin = {
    .name = "media_trigger",
    .priv_size = sizeof(MediaTriggerContext),
    .priv = NULL,
    .init = NULL,
    .get = NULL,
    .available = NULL,
    .run_once = NULL,
    .uninit = NULL,
    .process_command = media_trigger_handler,
};
