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

#include <errno.h>
#include <fcntl.h>
#include <kvdb.h>
#include <pfw.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <syslog.h>

#include "media_proxy.h"
#include "media_server.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MEDIA_PERSIST "persist.media."

/****************************************************************************
 * Private Functions Prototype
 ****************************************************************************/

static void pfw_ffmpeg_command_callback(void* cookie, const char* params);
static void pfw_set_parameter_callback(void* cookie, const char* params);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static pfw_plugin_def_t g_media_policy_plugins[] = {
    { "FFmpegCommand", NULL, pfw_ffmpeg_command_callback },
    { "SetParameter", NULL, pfw_set_parameter_callback }
};

static const size_t g_media_policy_nb_plugins = sizeof(g_media_policy_plugins)
    / sizeof(g_media_policy_plugins[0]);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void policy_process_command(const char* target, const char* cmd, const char* arg)
{
#ifdef CONFIG_LIB_FFMPEG
    media_graph_handler(media_get_graph(), target, cmd, arg, NULL, 0);
#endif
    media_proxy(MEDIA_ID_GRAPH, NULL, target, cmd, arg, 0, NULL, 0, true);
}

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
        policy_process_command(target, cmd, arg);

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

static void pfw_load_criterion(const char* name, int32_t* state)
{
    if (!strncmp(name, MEDIA_PERSIST, strlen(MEDIA_PERSIST)))
        *state = property_get_int32(name, *state);
}

static void pfw_save_criterion(const char* name, int32_t state)
{
    if (!strncmp(name, MEDIA_PERSIST, strlen(MEDIA_PERSIST)))
        property_set_int32_oneway(name, state);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int media_policy_handler(void* policy, const char* name, const char* cmd,
    const char* value, int apply, char* res, int res_len)
{
    int ret = -ENOSYS, tmp;
    char* dump;

    if (!strcmp(cmd, "set_int")) {
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
        ret = pfw_contain(policy, name, value, &tmp);
        if (ret >= 0)
            return snprintf(res, res_len, "%d", tmp);
    } else if (!strcmp(cmd, "get_int")) {
        ret = pfw_getint(policy, name, &tmp);
        if (ret >= 0)
            return snprintf(res, res_len, "%d", tmp);
    } else if (!strcmp(cmd, "get_string")) {
        ret = pfw_getstring(policy, name, res, res_len);
        if (ret >= 0)
            return 0;
    } else if (!strcmp(cmd, "get_parameter")) {
        ret = pfw_getparameter(policy, name, res, res_len);
        if (ret >= 0)
            return 0;
    } else if (!strcmp(cmd, "dump")) {
        dump = pfw_dump(policy);
        syslog(LOG_INFO, "\n%s", dump);
        free(dump);
        return 0;
    }

    if (ret < 0)
        return ret;

    if (apply)
        pfw_apply(policy);

    return 0;
}

int media_policy_destroy(void* policy)
{
    pfw_destroy(policy);
    return 0;
}

void* media_policy_create(void* params)
{
    const char** paths = params;
    void* policy;

    if (!params)
        return NULL;

    policy = pfw_create(paths[0], paths[1], g_media_policy_plugins,
        g_media_policy_nb_plugins, pfw_load_criterion, pfw_save_criterion);
    if (!policy)
        return NULL;

    pfw_apply(policy);
    return policy;
}