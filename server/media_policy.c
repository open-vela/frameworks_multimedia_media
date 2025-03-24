/****************************************************************************
 * frameworks/media/server/media_policy.c
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

#include <nuttx/audio/audio.h>
#include <nuttx/wqueue.h>

#include <errno.h>
#include <fcntl.h>
#include <kvdb.h>
#include <pfw.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>

#include "media_common.h"
#include "media_plugin.h"
#include "media_server.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MEDIA_PERSIST "persist.media."
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

/****************************************************************************
 * Private Functions Prototype
 ****************************************************************************/

static void pfw_ffmpeg_command_callback(void* cookie, const char* params);
static void pfw_set_parameter_callback(void* cookie, const char* params);

/****************************************************************************
 * Private Data
 ****************************************************************************/
typedef struct pfw_system_s pfw_system_t;

typedef struct MediaPolicyPriv {
    struct work_s work; /* Used for save kvdb */
    char key[64];
    int value;
    pfw_system_t* policy;
} MediaPolicyPriv;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void pfw_ffmpeg_command_callback(void* cookie, const char* params)
{
    char *target, *cmd, *arg, *outptr, *inptr, *str;

    /* fmt:
     *  {target,cmd,arg;}*
     * example:
     *  sco,sample_rate,16000;sco,play,;
     */

    str = strdup(params);
    if (!str)
        return;

    target = strtok_r(str, ";", &outptr);
    while (target) {
        target = strtok_r(target, ",", &inptr);
        if (!target)
            goto out;

        cmd = strtok_r(NULL, ",", &inptr);
        if (!cmd)
            goto out;

        arg = strtok_r(NULL, ",", &inptr);
        media_stub_process_command(target, cmd, arg);

        target = strtok_r(NULL, ";", &outptr);
    }

out:
    free(str);
}

static void pfw_set_parameter_callback(void* cookie, const char* params)
{
    char *target, *arg, *outptr, *saveptr, *str;
    int fd = 0;

    /* fmt:
     *  target_1,args_1;target_2,args_2;...;target_n,args_n
     * example:
     *  dev/audio/mixer1,mode=normal,outdev0=speaker;dev/audio/mixer2,indev=mic;
     */

    str = strdup(params);
    if (!str)
        return;

    outptr = strtok_r(str, ";", &saveptr);

    while (outptr) {
        target = strtok_r(outptr, ",", &arg);
        if (target == NULL)
            goto out;

        if (arg != NULL && target != NULL) {
            fd = open(target, O_RDWR | O_CLOEXEC);
            if (fd < 0)
                goto out;

            if (ioctl(fd, AUDIOIOC_SETPARAMTER, arg) < 0) {
                close(fd);
                goto out;
            }

            close(fd);
        }

        outptr = strtok_r(NULL, ";", &saveptr);
    }

out:
    free(str);
}

static void pfw_load_criterion_cb(void* cookie, const char* name, int32_t* state)
{
    if (!strncmp(name, MEDIA_PERSIST, strlen(MEDIA_PERSIST)))
        *state = property_get_int32(name, *state);
}

static void pfw_save_criterion_cb(void* cookie, const char* name, int32_t state)
{
    MediaPolicyPriv* priv = cookie;

    if (!strncmp(name, MEDIA_PERSIST, strlen(MEDIA_PERSIST))) {
        strlcpy(priv->key, name, sizeof(priv->key));
        priv->value = state;
        property_set_int32_oneway(priv->key, priv->value);
    }
}

static void media_policy_notify_cb(void* cookie, int number, char* literal)
{
    media_stub_notify_event(cookie, 0, number, literal);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

static int media_policy_handler(MediadPlugin* ctx, struct media_server_conn* conn, const char* name, const char* cmd,
    const char* value, int apply, char* res, int res_len)
{
    MediaPolicyPriv* priv = ctx->priv;
    pfw_system_t* policy = priv->policy;
    int ret = -ENOSYS, tmp[2];
    void* handle;
    char* dump;

    if (!cmd) {
        return -EINVAL;
    } else if (!strcmp(cmd, "ping")) {
        return 0;
    } else if (!strcmp(cmd, "subscribe")) {
        handle = pfw_subscribe(policy, name, media_policy_notify_cb, conn);
        if (!handle)
            return -EINVAL;
        media_server_set_data(conn, handle);
        return 0;
    } else if (!strcmp(cmd, "unsubscribe")) {
        handle = media_server_get_data(conn);
        pfw_unsubscribe(policy, handle);
        media_stub_notify_finalize((void**)&conn);
        return 0;
    } else if (!strcmp(cmd, "set_int")) {
        ret = pfw_setint(policy, name, atoi(value));
    } else if (!strcmp(cmd, "increase")) {
        ret = pfw_increase(policy, name);
    } else if (!strcmp(cmd, "decrease")) {
        ret = pfw_decrease(policy, name);
    } else if (!strcmp(cmd, "set_string")) {
        ret = pfw_setstring(policy, name, value);
    } else if (!strcmp(cmd, "include")) {
        ret = pfw_include(policy, name, value);
    } else if (!strcmp(cmd, "exclude")) {
        ret = pfw_exclude(policy, name, value);
    } else if (!strcmp(cmd, "contain")) {
        ret = pfw_contain(policy, name, value, &tmp[0]);
        if (ret >= 0) {
            MEDIA_DEBUG("%s %s %s %s ret:%d res:%d.",
                name, cmd, value ? value : "_", apply ? "apply" : "_", ret, tmp[0]);
            return snprintf(res, res_len, "%d", tmp[0]);
        }
    } else if (!strcmp(cmd, "get_int")) {
        ret = pfw_getint(policy, name, &tmp[0]);
        if (ret >= 0) {
            MEDIA_DEBUG("%s %s %s %s ret:%d res:%d.",
                name, cmd, value ? value : "_", apply ? "apply" : "_", ret, tmp[0]);
            return snprintf(res, res_len, "%d", tmp[0]);
        }
    } else if (!strcmp(cmd, "get_string")) {
        ret = pfw_getstring(policy, name, res, res_len);
        MEDIA_DEBUG("%s %s %s %s ret:%d res:%s.",
            name, cmd, value ? value : "_", apply ? "apply" : "_", ret, res);
        if (ret >= 0)
            return 0;
    } else if (!strcmp(cmd, "get_range")) {
        ret = pfw_getrange(policy, name, &tmp[0], &tmp[1]);
        if (ret >= 0) {
            MEDIA_DEBUG("%s %s %s %s ret:%d res:%d - %d.",
                name, cmd, value ? value : "_", apply ? "apply" : "_", ret, tmp[0], tmp[1]);
            return snprintf(res, res_len, "%d,%d", tmp[0], tmp[1]);
        }
    } else if (!strcmp(cmd, "dump")) {
        dump = pfw_dump(policy);
        MEDIA_INFO("\n%s", dump);
        free(dump);
        return 0;
    }

    MEDIA_DEBUG("%s %s %s %s ret:%d.",
        name, cmd, value ? value : "_", apply ? "apply" : "_", ret);

    if (ret < 0)
        return ret;

    if (apply)
        pfw_apply(policy);

    return 0;
}

static int media_policy_uninit(MediadPlugin* ctx)
{
    MediaPolicyPriv* priv = ctx->priv;

    if (priv->policy) {
        pfw_destroy(priv->policy, NULL);
        priv->policy = NULL;
    }

    return 0;
}

static int media_policy_init(MediadPlugin* ctx)
{
    const char* paths[] = {
        CONFIG_MEDIA_SERVER_CONFIG_PATH "criteria.txt",
        CONFIG_MEDIA_SERVER_CONFIG_PATH "settings.pfw"
    };
    pfw_plugin_def_t media_policy_plugins[] = {
        { "FFmpegCommand", NULL, pfw_ffmpeg_command_callback },
        { "SetParameter", NULL, pfw_set_parameter_callback }
    };
    MediaPolicyPriv* priv = ctx->priv;
    pfw_system_t* policy;

    policy = pfw_create(paths[0], paths[1], media_policy_plugins,
        ARRAY_SIZE(media_policy_plugins), pfw_load_criterion_cb, pfw_save_criterion_cb, (void*)priv);
    if (!policy)
        return -ENOMEM;

    pfw_apply(policy);
    priv->policy = policy;

    return 0;
}

MediadPlugin media_policy_plugin = {
    .name = "media_policy",
    .priv_size = sizeof(MediaPolicyPriv),
    .priv = NULL,
    .init = media_policy_init,
    .get = NULL,
    .available = NULL,
    .run_once = NULL,
    .uninit = media_policy_uninit,
    .process_command = media_policy_handler,
};
