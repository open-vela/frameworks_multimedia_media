/****************************************************************************
 * frameworks/media/media_stub.c
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
#include <syslog.h>

#include "media_server.h"
#include "media_internal.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void media_stub_event_cb(void *cookie, int event,
                                int result, const char *extra)
{
    media_parcel notify;

    media_parcel_init(&notify);
    media_parcel_append_printf(&notify, "%i%i%s", event, result, extra);
    media_server_notify(media_get_server(), cookie, &notify);
    media_parcel_deinit(&notify);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void media_stub_onreceive(void *cookie, media_parcel *in, media_parcel *out)
{
    const char *param_s1 = NULL, *param_s2 = NULL, *param_s3 = NULL;
    int32_t cmd = 0, param_i32 = 0, param_i32_1 = 0, ret;
    uint32_t param_u32 = 0;
    char *response = NULL;
    float param_flt = 0.0;
    uint64_t handle = 0;

    media_parcel_read_int32(in, &cmd);

    switch (cmd) {
        case MEDIA_PROCESS_COMMAND:
            media_parcel_read_scanf(in, "%s%s%s%i", &param_s1, &param_s2,
                                    &param_s3, &param_i32);
            if (param_i32 > 0) {
                response = malloc(param_i32);
                if (!response) {
                    media_parcel_append_int32(out, -ENOMEM);
                    break;
                }
            }

            ret = media_graph_process_command(media_get_graph(), param_s1,
                                              param_s2, param_s3, response, param_i32);
            media_parcel_append_printf(out, "%i%s", ret, response);
            free(response);
            break;

        case MEDIA_GRAPH_DUMP:
            media_parcel_read_scanf(in, "%s", &param_s1);
            response = media_graph_dump_(media_get_graph(), param_s1);
            media_parcel_append_printf(out, "%s", response);
            free(response);
            break;

        case MEDIA_PLAYER_OPEN:
            media_parcel_read_scanf(in, "%s", &param_s1);
            handle = (uint64_t)(uintptr_t)media_player_open_(media_get_graph(), param_s1);
            media_parcel_append_printf(out, "%l", handle);
            break;

        case MEDIA_PLAYER_CLOSE:
            media_parcel_read_scanf(in, "%l%i", &handle, &param_i32);
            ret = media_player_close_((void *)(uintptr_t)handle, param_i32);
            media_parcel_append_printf(out, "%i", ret);
            break;

        case MEDIA_PLAYER_SET_CALLBACK:
            media_parcel_read_scanf(in, "%l", &handle);
            ret = media_player_set_event_callback_((void *)(uintptr_t)handle, cookie, media_stub_event_cb);
            media_parcel_append_printf(out, "%i", ret);
            break;

        case MEDIA_PLAYER_PREPARE:
            media_parcel_read_scanf(in, "%l%s%s", &handle, &param_s1, &param_s2);
            ret = media_player_prepare_((void *)(uintptr_t)handle, param_s1, param_s2);
            media_parcel_append_printf(out, "%i", ret);
            break;

        case MEDIA_PLAYER_RESET:
            media_parcel_read_scanf(in, "%l", &handle);
            ret = media_player_reset_((void *)(uintptr_t)handle);
            media_parcel_append_printf(out, "%i", ret);
            break;

        case MEDIA_PLAYER_START:
            media_parcel_read_scanf(in, "%l", &handle);
            ret = media_player_start_((void *)(uintptr_t)handle);
            media_parcel_append_printf(out, "%i", ret);
            break;

        case MEDIA_PLAYER_STOP:
            media_parcel_read_scanf(in, "%l", &handle);
            ret = media_player_stop_((void *)(uintptr_t)handle);
            media_parcel_append_printf(out, "%i", ret);
            break;

        case MEDIA_PLAYER_PAUSE:
            media_parcel_read_scanf(in, "%l", &handle);
            ret = media_player_pause_((void *)(uintptr_t)handle);
            media_parcel_append_printf(out, "%i", ret);
            break;

        case MEDIA_PLAYER_SEEK:
            media_parcel_read_scanf(in, "%l%i", &handle, &param_u32);
            ret = media_player_seek_((void *)(uintptr_t)handle, param_u32);
            media_parcel_append_printf(out, "%i", ret);
            break;

        case MEDIA_PLAYER_LOOP:
            media_parcel_read_scanf(in, "%l%i", &handle, &param_i32);
            ret = media_player_set_looping_((void *)(uintptr_t)handle, param_i32);
            media_parcel_append_printf(out, "%i", ret);
            break;

        case MEDIA_PLAYER_ISPLAYING:
            media_parcel_read_scanf(in, "%l", &handle);
            param_u32 = media_player_is_playing_((void *)(uintptr_t)handle);
            media_parcel_append_printf(out, "%i", param_u32);
            break;

        case MEDIA_PLAYER_POSITION:
            media_parcel_read_scanf(in, "%l", &handle);
            ret = media_player_get_position_((void *)(uintptr_t)handle, &param_u32);
            media_parcel_append_printf(out, "%i%i", ret, param_u32);
            break;

        case MEDIA_PLAYER_DURATION:
            media_parcel_read_scanf(in, "%l", &handle);
            ret = media_player_get_duration_((void *)(uintptr_t)handle, &param_u32);
            media_parcel_append_printf(out, "%i%i", ret, param_u32);
            break;

        case MEDIA_PLAYER_SET_VOLUME:
            media_parcel_read_scanf(in, "%l%f", &handle, &param_flt);
            ret = media_player_set_volume_((void *)(uintptr_t)handle, param_flt);
            media_parcel_append_printf(out, "%i", ret);
            break;

        case MEDIA_PLAYER_GET_VOLUME:
            media_parcel_read_scanf(in, "%l", &handle);
            ret = media_player_get_volume_((void *)(uintptr_t)handle, &param_flt);
            media_parcel_append_printf(out, "%i%f", ret, param_flt);
            break;

        case MEDIA_PLAYER_SET_PROPERTY:
            media_parcel_read_scanf(in, "%l%s%s%s", &handle, &param_s1, &param_s2, &param_s3);
            ret = media_player_process_command((void *)(uintptr_t)handle, param_s1, param_s2, param_s3, NULL, 0);
            media_parcel_append_printf(out, "%i", ret);
            break;

        case MEDIA_PLAYER_GET_PROPERTY:
            media_parcel_read_scanf(in, "%l%s%s%i", &handle, &param_s1, &param_s2, &param_i32);

            if (param_i32 > 0) {
                response = malloc(param_i32);
                if (!response) {
                    media_parcel_append_int32(out, -ENOMEM);
                    break;
                }
            }

            ret = media_player_process_command((void *)(uintptr_t)handle, param_s1, param_s2, NULL, response, param_i32);
            if (ret < 0)
                media_parcel_append_printf(out, "%i", ret);
            else
                media_parcel_append_printf(out, "%i%s", ret, response);

            free(response);
            break;

        case MEDIA_RECORDER_OPEN:
            media_parcel_read_scanf(in, "%s", &param_s1);
            handle = (uint64_t)(uintptr_t)media_recorder_open_(media_get_graph(), param_s1);
            media_parcel_append_printf(out, "%l", handle);
            break;

        case MEDIA_RECORDER_CLOSE:
            media_parcel_read_scanf(in, "%l", &handle);
            ret = media_recorder_close_((void *)(uintptr_t)handle);
            media_parcel_append_printf(out, "%i", ret);
            break;

        case MEDIA_RECORDER_SET_CALLBACK:
            media_parcel_read_scanf(in, "%l", &handle);
            ret = media_recorder_set_event_callback_((void *)(uintptr_t)handle, cookie, media_stub_event_cb);
            media_parcel_append_printf(out, "%i", ret);
            break;

        case MEDIA_RECORDER_PREPARE:
            media_parcel_read_scanf(in, "%l%s%s", &handle, &param_s1, &param_s2);
            ret = media_recorder_prepare_((void *)(uintptr_t)handle, param_s1, param_s2);
            media_parcel_append_printf(out, "%i", ret);
            break;

        case MEDIA_RECORDER_START:
            media_parcel_read_scanf(in, "%l", &handle);
            ret = media_recorder_start_((void *)(uintptr_t)handle);
            media_parcel_append_printf(out, "%i", ret);
            break;

        case MEDIA_RECORDER_PAUSE:
            media_parcel_read_scanf(in, "%l", &handle);
            ret = media_recorder_pause_((void *)(uintptr_t)handle);
            media_parcel_append_printf(out, "%i", ret);
            break;

        case MEDIA_RECORDER_STOP:
            media_parcel_read_scanf(in, "%l", &handle);
            ret = media_recorder_stop_((void *)(uintptr_t)handle);
            media_parcel_append_printf(out, "%i", ret);
            break;

        case MEDIA_RECORDER_RESET:
            media_parcel_read_scanf(in, "%l", &handle);
            ret = media_recorder_reset_((void *)(uintptr_t)handle);
            media_parcel_append_printf(out, "%i", ret);
            break;

        case MEDIA_RECORDER_SET_PROPERTY:
            media_parcel_read_scanf(in, "%l%s%s%s", &handle, &param_s1, &param_s2, &param_s3);
            ret = media_recorder_process_command((void *)(uintptr_t)handle, param_s1, param_s2, param_s3, NULL, 0);
            media_parcel_append_printf(out, "%i", ret);
            break;

        case MEDIA_RECORDER_GET_PROPERTY:
            media_parcel_read_scanf(in, "%l%s%s%i", &handle, &param_s1, &param_s2, &param_i32);

            if (param_i32 > 0) {
                response = malloc(param_i32);
                if (!response) {
                    media_parcel_append_int32(out, -ENOMEM);
                    break;
                }
            }

            ret = media_recorder_process_command((void *)(uintptr_t)handle, param_s1, param_s2, NULL, response, param_i32);
            if (ret < 0)
                media_parcel_append_printf(out, "%i", ret);
            else
                media_parcel_append_printf(out, "%i%s", ret, response);

            free(response);
            break;

        case MEDIA_POLICY_SET_INT:
            media_parcel_read_scanf(in, "%s%i%i", &param_s1, &param_i32, &param_i32_1);
            ret = media_policy_set_int_(param_s1, param_i32, param_i32_1);
            media_parcel_append_printf(out, "%i", ret);
            break;

        case MEDIA_POLICY_GET_INT:
            media_parcel_read_scanf(in, "%s", &param_s1);
            ret = media_policy_get_int_(param_s1, &param_i32);
            media_parcel_append_printf(out, "%i%i", ret, param_i32);
            break;

        case MEDIA_POLICY_SET_STRING:
            media_parcel_read_scanf(in, "%s%s%i", &param_s1, &param_s2, &param_i32);
            ret = media_policy_set_string_(param_s1, param_s2, param_i32);
            media_parcel_append_printf(out, "%i", ret);
            break;

        case MEDIA_POLICY_GET_STRING:
            media_parcel_read_scanf(in, "%s%i", &param_s1, &param_i32);
            if (param_i32 > 0) {
                response = zalloc(param_i32);
                if (!response) {
                    media_parcel_append_int32(out, -ENOMEM);
                    break;
                }
            }

            ret = media_policy_get_string_(param_s1, response, param_i32);
            media_parcel_append_printf(out, "%i%s", ret, response);
            free(response);
            break;

        case MEDIA_POLICY_INCLUDE:
            media_parcel_read_scanf(in, "%s%s%i", &param_s1, &param_s2, &param_i32);
            ret = media_policy_include_(param_s1, param_s2, param_i32);
            media_parcel_append_printf(out, "%i", ret);
            break;

        case MEDIA_POLICY_EXCLUDE:
            media_parcel_read_scanf(in, "%s%s%i", &param_s1, &param_s2, &param_i32);
            ret = media_policy_exclude_(param_s1, param_s2, param_i32);
            media_parcel_append_printf(out, "%i", ret);
            break;

        case MEDIA_POLICY_INCREASE:
            media_parcel_read_scanf(in, "%s%i", &param_s1, &param_i32);
            ret = media_policy_increase_(param_s1, param_i32);
            media_parcel_append_printf(out, "%i", ret);
            break;

        case MEDIA_POLICY_DECREASE:
            media_parcel_read_scanf(in, "%s%i", &param_s1, &param_i32);
            ret = media_policy_decrease_(param_s1, param_i32);
            media_parcel_append_printf(out, "%i", ret);
            break;

        case MEDIA_POLICY_DUMP:
            media_parcel_read_scanf(in, "%s", &param_s1);
            response = media_policy_dump_(param_s1);
            media_parcel_append_printf(out, "%s", response);
            free(response);
            break;

        default:
            break;
    }
}
