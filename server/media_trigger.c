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
#include <fcntl.h>
#include <malloc.h>
#include <media_recorder.h>
#include <media_trigger_model.h>
#include <netinet/in.h>
#include <netpacket/rpmsg.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
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

#ifdef CONFIG_MEDIA_TRIGGER_DUMP
#define MEDIA_TRIGGER_DUMP_DIR CONFIG_MEDIA_TRIGGER_DUMP_PATH
#endif

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

typedef struct MediaTriggerPluginPriv MediaTriggerPluginPriv;

typedef struct MediaTriggerContext {
    void* context;
    void* handle;
    int state;
    bool exit;
    int tran_fd;
    int notify_fd;
    int recorder_fd;
    uint32_t offset;
    char* buffer;
    size_t buffer_size;
    media_parcel parcel;
    MediaTriggerPluginPriv* priv;
#ifdef CONFIG_MEDIA_TRIGGER_DUMP
    int sample_rate;
    int channels;
    int bits_per_sample;
#endif
} MediaTriggerContext;

struct MediaTriggerPluginPriv {
    MediaTriggerContext* instance;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_MEDIA_TRIGGER_DUMP
static void media_trigger_parse_audio_format(MediaTriggerContext* ctx, const char* options)
{
    const char* ptr;

    /* Parse sample rate */
    ptr = strstr(options, "sample_rate=");
    if (ptr) {
        ctx->sample_rate = atoi(ptr + 12);
    }

    /* Parse channels from ch_layout */
    ptr = strstr(options, "ch_layout=");
    if (ptr) {
        ptr += 10;
        if (strncmp(ptr, "mono", 4) == 0) {
            ctx->channels = 1;
        } else if (strncmp(ptr, "stereo", 6) == 0) {
            ctx->channels = 2;
        } else {
            /* Try to parse as number */
            ctx->channels = atoi(ptr);
        }
    }

    /* Parse bits per sample from format */
    ptr = strstr(options, "format=");
    if (ptr) {
        ptr += 7;
        if (strncmp(ptr, "s16", 3) == 0 || strncmp(ptr, "S16", 3) == 0) {
            ctx->bits_per_sample = 16;
        } else if (strncmp(ptr, "s32", 3) == 0 || strncmp(ptr, "S32", 3) == 0) {
            ctx->bits_per_sample = 32;
        } else if (strncmp(ptr, "s24", 3) == 0 || strncmp(ptr, "S24", 3) == 0) {
            ctx->bits_per_sample = 24;
        } else if (strncmp(ptr, "s8", 2) == 0 || strncmp(ptr, "S8", 2) == 0) {
            ctx->bits_per_sample = 8;
        } else if (strncmp(ptr, "u16", 3) == 0 || strncmp(ptr, "U16", 3) == 0) {
            ctx->bits_per_sample = 16;
        } else if (strncmp(ptr, "u8", 2) == 0 || strncmp(ptr, "U8", 2) == 0) {
            ctx->bits_per_sample = 8;
        }
    }

    MEDIA_INFO("parsed audio format: %dHz, %dch, %dbit\n",
        ctx->sample_rate, ctx->channels, ctx->bits_per_sample);
}

static void media_trigger_dump_audio(MediaTriggerContext* ctx, const char* buffer, size_t size)
{
    int rate = ctx->sample_rate > 0 ? ctx->sample_rate : 16000;
    char filename[256];
    int fd;

    /* Format sample rate as 8K, 16K, 48K, etc. */
    if (rate % 1000 == 0) {
        snprintf(filename, sizeof(filename), "%s/media_trigger_%dK_%dch_%dbit.pcm",
            MEDIA_TRIGGER_DUMP_DIR,
            rate / 1000,
            ctx->channels > 0 ? ctx->channels : 1,
            ctx->bits_per_sample > 0 ? ctx->bits_per_sample : 16);
    } else {
        snprintf(filename, sizeof(filename), "%s/media_trigger_%dHz_%dch_%dbit.pcm",
            MEDIA_TRIGGER_DUMP_DIR,
            rate,
            ctx->channels > 0 ? ctx->channels : 1,
            ctx->bits_per_sample > 0 ? ctx->bits_per_sample : 16);
    }

    fd = open(filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) {
        ssize_t written = write(fd, buffer, size);
        if (written != (ssize_t)size) {
            MEDIA_WARN("failed to write complete buffer: written=%zd, expected=%zu\n",
                written, size);
        }
        close(fd);
    } else {
        MEDIA_ERR("failed to open dump file: %s, errno=%d\n", filename, errno);
    }
}
#endif

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

static void hotword_detection_result_callback(void* user_data, int event,
    int result, const char* extra)
{
    MediaTriggerContext* ctx = (MediaTriggerContext*)user_data;

    media_trigger_notify_event(ctx, event, result, extra);
}

static int media_trigger_stop_recorder(MediaTriggerContext* ctx)
{
    int ret;

    ret = media_recorder_stop(ctx->handle);
    if (ret < 0) {
        MEDIA_ERR("stop recorder failed: %d\n", ret);
        return ret;
    }

    ret = media_recorder_close(ctx->handle);
    ctx->handle = NULL;
    free(ctx->buffer);
    ctx->buffer = NULL;
    ctx->recorder_fd = -1;

    return ret;
}

static int media_trigger_start_recorder(MediaTriggerContext* ctx, const char* options)
{
    int ret;

    ctx->handle = media_recorder_open(MEDIA_SOURCE_HOTWORD);
    if (!ctx->handle) {
        MEDIA_ERR("recorder open failed\n");
        return -EINVAL;
    }

    ret = media_recorder_prepare(ctx->handle, NULL, options);
    if (ret < 0) {
        MEDIA_ERR("recorder prepare failed\n");
        goto out;
    }

    ret = media_recorder_start(ctx->handle);
    if (ret < 0) {
        MEDIA_ERR("recorder start failed\n");
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

    // Get recorder socket and set non-blocking
    ctx->recorder_fd = media_recorder_get_socket(ctx->handle);
    if (ctx->recorder_fd >= 0) {
        if (fcntl(ctx->recorder_fd, F_SETFL, fcntl(ctx->recorder_fd, F_GETFL, 0) | O_NONBLOCK) < 0) {
            MEDIA_ERR("Failed to set recorder fd non-blocking\n");
            ctx->recorder_fd = -1;
        }
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
    ctx->recorder_fd = -1;
    ctx->offset = 0;
    ctx->exit = false;
    ctx->priv = NULL;
    media_parcel_init(&ctx->parcel);
#ifdef CONFIG_MEDIA_TRIGGER_DUMP
    ctx->sample_rate = 0;
    ctx->channels = 0;
    ctx->bits_per_sample = 0;
#endif

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
        ctx->context = media_trigger_model_load(data, size, hotword_detection_result_callback, ctx);
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
        MEDIA_INFO("recorder options: %s\n", options);

#ifdef CONFIG_MEDIA_TRIGGER_DUMP
        media_trigger_parse_audio_format(ctx, options);
#endif

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
                MEDIA_ERR("unload: auto stop recorder failed: %d\n", ret);
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
                MEDIA_ERR("close: stop recorder failed: %d\n", ret);
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

        /* Clear plugin instance pointer when closing */
        if (ctx->priv) {
            ctx->priv->instance = NULL;
        }
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

static int media_trigger_handle_tran_fd(MediaTriggerContext* ctx)
{
    media_parcel ack;
    uint32_t code;
    int ret;

    while (1) {
        ret = media_parcel_recv(&ctx->parcel, ctx->tran_fd, &ctx->offset, MSG_DONTWAIT);
        if (ret < 0) {
            if (ret == -EPIPE) {
                MEDIA_INFO("fd %d connection broken\n", ctx->tran_fd);
                media_trigger_conn_close(ctx);
                return -EPIPE;
            }
            break;
        }

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
                MEDIA_ERR("create notify failed: %d\n", ret);
            break;
        default:
            break;
        }

        media_parcel_reinit(&ctx->parcel);
        ctx->offset = 0;
    }

    return 0;
}

static int media_trigger_handle_recorder_fd(MediaTriggerContext* ctx)
{
    bool detected;
    int ret;

    ret = recv(ctx->recorder_fd, ctx->buffer, ctx->buffer_size, MSG_DONTWAIT);

    if (ret > 0) {
        MEDIA_DEBUG("received %d bytes\n", ret);

#ifdef CONFIG_MEDIA_TRIGGER_DUMP
        media_trigger_dump_audio(ctx, ctx->buffer, ret);
#endif
        detected = media_trigger_model_detect_hotword(ctx->context, ctx->buffer, ret);
        if (detected) {
            MEDIA_INFO("hotword detected\n");
            media_trigger_notify_event(ctx, 0, 0, NULL);
        }
    } else if (ret == 0) {
        MEDIA_INFO("recorder socket closed\n");
        media_recorder_close_socket(ctx->handle);
        ctx->recorder_fd = -1;
    } else if (ret < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        MEDIA_ERR("recv from recorder failed: %d\n", errno);
    }

    return 0;
}

static int media_trigger_setup_poll_fds(MediaTriggerContext* ctx, struct pollfd* fds,
    int* tran_idx, int* recorder_idx, int* model_idx)
{
    int model_poll_fd = -1;
    int nfds = 0;

    *tran_idx = *recorder_idx = *model_idx = -1;

    // Setup transport fd (always present)
    *tran_idx = nfds;
    fds[nfds].fd = ctx->tran_fd;
    fds[nfds].events = POLLIN;
    fds[nfds].revents = 0;
    nfds++;

    // Setup recorder fd (present when started)
    if (ctx->state == SOUND_TRIGGER_STATE_STARTED && ctx->recorder_fd >= 0) {
        *recorder_idx = nfds;
        fds[nfds].fd = ctx->recorder_fd;
        fds[nfds].events = POLLIN;
        fds[nfds].revents = 0;
        nfds++;
    }

    // Setup model poll fd (optional, only if available)
    if (ctx->context) {
        model_poll_fd = media_trigger_model_get_poll_fd(ctx->context);
        if (model_poll_fd >= 0) {
            *model_idx = nfds;
            fds[nfds].fd = model_poll_fd;
            fds[nfds].events = POLLIN;
            fds[nfds].revents = 0;
            nfds++;
        }
    }

    return nfds;
}

static int media_trigger_poll(MediaTriggerContext* ctx)
{
    struct pollfd fds[3];
    int nfds;
    int ret;
    int tran_idx, recorder_idx, model_idx;

    nfds = media_trigger_setup_poll_fds(ctx, fds, &tran_idx, &recorder_idx, &model_idx);

    ret = poll(fds, nfds, -1);
    if (ret < 0) {
        if (errno == EINTR) {
            MEDIA_DEBUG("poll interrupted by signal\n");
            return 0;
        }
        MEDIA_ERR("poll failed: %d\n", errno);
        return -errno;
    } else if (ret == 0) {
        MEDIA_DEBUG("poll timeout\n");
        return -EAGAIN;
    }

    // Check for transport fd errors or hangup first (highest priority)
    if (fds[tran_idx].revents & (POLLERR | POLLHUP)) {
        MEDIA_INFO("tran_fd %d revent: %d\n", ctx->tran_fd, (int)fds[tran_idx].revents);
        media_trigger_conn_close(ctx);
        return -EPIPE;
    }

    // Handle transport fd data (high priority - commands)
    if (fds[tran_idx].revents & POLLIN) {
        ret = media_trigger_handle_tran_fd(ctx);
        if (ret == -EPIPE) {
            return -EPIPE;
        }
    }

    // Handle recorder fd data (medium priority - audio data)
    if (recorder_idx >= 0 && (fds[recorder_idx].revents & POLLIN)) {
        media_trigger_handle_recorder_fd(ctx);
    }

    // Handle model poll fd events (low priority - optional)
    if (model_idx >= 0 && (fds[model_idx].revents & POLLIN)) {
        MEDIA_DEBUG("model poll fd has data available\n");
        ret = media_trigger_model_poll_available(ctx->context);
        if (ret < 0) {
            MEDIA_ERR("model poll available failed: %d\n", ret);
        }
    }

    return 0;
}

static void* media_trigger_thread(void* arg)
{
    MediaTriggerContext* ctx = (MediaTriggerContext*)arg;
    int ret;

    MEDIA_INFO("create trigger thread\n");

    while (1) {
        if (media_trigger_is_exit(ctx))
            break;

        ret = media_trigger_poll(ctx);
        if (ret < 0 && ret != -EAGAIN) {
            MEDIA_ERR("poll available error: %d\n", ret);
        }
    }

    media_trigger_ctx_release(ctx);
    MEDIA_INFO("exit trigger thread\n");

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

static int media_trigger_plugin_init(struct MediadPlugin* pctx)
{
    MediaTriggerPluginPriv* priv = (MediaTriggerPluginPriv*)pctx->priv;

    priv->instance = NULL;
    MEDIA_INFO("media trigger plugin initialized\n");

    return 0;
}

static int media_trigger_plugin_uninit(struct MediadPlugin* pctx)
{
    MediaTriggerPluginPriv* priv = (MediaTriggerPluginPriv*)pctx->priv;

    if (priv->instance) {
        MEDIA_WARN("media trigger plugin uninit: instance still exists, forcing cleanup\n");
        priv->instance = NULL;
    }

    MEDIA_INFO("media trigger plugin uninitialized\n");

    return 0;
}

static int media_trigger_handler(struct MediadPlugin* pctx, struct media_server_conn* conn,
    const char* target, const char* cmd, const char* arg, int flags, char* res, int res_len)
{
    MediaTriggerPluginPriv* priv = (MediaTriggerPluginPriv*)pctx->priv;
    MediaTriggerContext* ctx = NULL;
    int ret = 0;

    MEDIA_INFO("media trigger cmd: %s arg: %s flags: %d res: %s res_len: %d\n",
        cmd, arg ? arg : "_", flags, res ? res : "_", res_len);

    if (!strcmp(cmd, "open")) {
        /* Check if an instance already exists (single instance mode) */
        if (priv->instance != NULL) {
            MEDIA_ERR("media trigger already opened, only one instance allowed\n");
            return -EBUSY;
        }

        ctx = media_trigger_ctx_init();
        if (!ctx)
            return -ENOMEM;

        ctx->tran_fd = media_server_get_tran_fd(conn);
        if (ctx->tran_fd < 0) {
            MEDIA_ERR("trigger get tran fd failed\n");
            media_trigger_ctx_release(ctx);
            return -EINVAL;
        }

        media_server_clean_conn(conn);
        ret = media_trigger_open(ctx);
        if (ret < 0) {
            media_trigger_ctx_release(ctx);
            return ret;
        }

        /* Store instance in plugin priv and set back pointer */
        ctx->priv = priv;
        priv->instance = ctx;

        MEDIA_INFO("media trigger open success\n");
    }

    return 0;
}

MediadPlugin media_trigger_plugin = {
    .name = "media_trigger",
    .priv_size = sizeof(MediaTriggerPluginPriv),
    .priv = NULL,
    .init = media_trigger_plugin_init,
    .get = NULL,
    .available = NULL,
    .run_once = NULL,
    .uninit = media_trigger_plugin_uninit,
    .process_command = media_trigger_handler,
};
