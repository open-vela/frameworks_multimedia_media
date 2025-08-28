/*
 * Copyright (C) 2023 Xiaomi Corporation. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "feature_exports.h"
#include "media_api.h"
#include "record.h"

static const char* TAG = "rec";

#define APP_PATH_PREFIX "internal://"

#define ARRAY_SIZE(array) (sizeof(array) / sizeof(array[0]))

#define RECORD_FILE_PATH "cache"

#ifndef CONFIG_HAP_APP_PATH
#define CONFIG_HAP_APP_PATH ""
#endif

#define MAX_LEN 128
#define OPTIONS_LEN 16
#define DEFAULT_SAMPLE_RATE 8000

#define RECORD_BUSY 205
#define INVALID_PARAM 202

typedef struct {
    char ch_layout_desc[OPTIONS_LEN];
    char filepath[CONFIG_PATH_MAX];
    char format[OPTIONS_LEN];
    char basename[MAX_LEN];
    char options[MAX_LEN];
    int encodeBitRate;
    uv_timer_t timer;
    bool recordbusy;
    int sampleRate;
    void* handle;
    int duration;

    FeatureInstanceHandle feature;
    FtCallbackId complete;
    FtCallbackId success;
    FtCallbackId fail;
} RecordObject;

/* common interface */
void system_record_onRegister(const char* feature_name)
{
    FEATURE_LOG_INFO("%s::%s(), feature_name: %s\n", TAG, __FUNCTION__, feature_name);
}

void system_record_onCreate(FeatureRuntimeContext ctx, FeatureProtoHandle handle)
{
    FEATURE_LOG_INFO("%s::%s(), FPH: %p\n", TAG, __FUNCTION__, handle);
}

void system_record_onRequired(FeatureRuntimeContext ctx, FeatureInstanceHandle handle)
{
    FEATURE_LOG_INFO("%s::%s(), FIH: %p\n", TAG, __FUNCTION__, handle);

    RecordObject* obj;

    obj = (RecordObject*)calloc(1, sizeof(RecordObject));
    if (!obj) {
        FEATURE_LOG_ERROR("%s::%s(), calloc failed!\n", TAG, __FUNCTION__);
        return;
    }

    obj->feature = handle;
    FeatureSetObjectData(handle, obj);
}

void system_record_onDetached(FeatureRuntimeContext ctx, FeatureInstanceHandle handle)
{
    FEATURE_LOG_INFO("%s::%s(), FIH: %p\n", TAG, __FUNCTION__, handle);

    RecordObject* obj;

    obj = (RecordObject*)FeatureGetObjectData(handle);
    if (obj)
        free(obj);

    FeatureSetObjectData(handle, NULL);
}

void system_record_onDestroy(FeatureRuntimeContext ctx, FeatureProtoHandle handle)
{
    FEATURE_LOG_INFO("%s::%s(), FPH: %p\n", TAG, __FUNCTION__, handle);
}

void system_record_onUnregister(const char* feature_name)
{
    FEATURE_LOG_INFO("%s::%s(), feature_name: %s\n", TAG, __FUNCTION__, feature_name);
}

static void finish_callback(int status, const char* msg, RecordObject* obj)
{
    char filepath[CONFIG_PATH_MAX] = { 0 };
    ft_context_ref ft_ctx;
    ft_value_t ret_obj;

    if ((status == 0) && (FeatureCheckCallbackId(obj->feature, obj->success))) {
        ft_ctx = FeatureGetContext(obj->feature);
        ret_obj = ft_new_object(FeatureGetContext(obj->feature));

        snprintf(filepath, sizeof(filepath), "%s%s/%s",
            APP_PATH_PREFIX, RECORD_FILE_PATH, obj->basename);
        ft_obj_set_property(ft_ctx, ret_obj, "uri",
            ft_from_string(ft_ctx, filepath));

        FeatureInvokeCallback(obj->feature, obj->success, &ret_obj);
        FeatureRemoveCallback(obj->feature, obj->success);
    } else if (FeatureCheckCallbackId(obj->feature, obj->fail)) {
        FeatureInvokeCallback(obj->feature, obj->fail, msg, status);
        FeatureRemoveCallback(obj->feature, obj->fail);
    }

    if (FeatureCheckCallbackId(obj->feature, obj->complete)) {
        FeatureInvokeCallback(obj->feature, obj->complete,
            (status == 0) ? "success" : "fail");
        FeatureRemoveCallback(obj->feature, obj->complete);
    }
}

static void record_uv_close_cb(void* cookie, int ret)
{
    FEATURE_LOG_INFO("%s::%s(), ret: %d\n", TAG, __FUNCTION__, ret);

    RecordObject* obj;

    obj = (RecordObject*)cookie;
    if (!obj)
        return;

    memset(obj, 0, sizeof(RecordObject));
}

static void record_uv_stop_cb(void* cookie, int ret)
{
    FEATURE_LOG_INFO("%s::%s()\n", TAG, __FUNCTION__);

    RecordObject* obj;

    obj = (RecordObject*)cookie;
    if (!obj || !obj->handle)
        return;

    media_uv_recorder_close(obj->handle, record_uv_close_cb);
}

static void timeupdate_timer_cb(uv_timer_t* timer)
{
    FEATURE_LOG_INFO("%s::%s()\n", TAG, __FUNCTION__);

    RecordObject* obj;

    obj = (RecordObject*)timer->data;
    if (!obj || !obj->handle)
        return;

    uv_timer_stop(timer);
    media_uv_recorder_stop(obj->handle, record_uv_stop_cb, obj);
}

static void timeupdate_loop_timer(RecordObject* obj)
{
    FEATURE_LOG_INFO("%s::%s()\n", TAG, __FUNCTION__);

    FeatureManagerHandle manager;
    uv_loop_t* loop;

    if (!obj)
        return;

    manager = FeatureGetManagerHandleFromInstance(obj->feature);
    if (!manager)
        return;

    loop = FeatureGetUVLoop(manager);
    if (!loop)
        return;

    uv_timer_init(loop, &obj->timer);
    obj->timer.data = obj;

    uv_timer_start(&obj->timer, timeupdate_timer_cb, obj->duration, 0);
}

static void record_uv_start_cb(void* cookie, int ret)
{
    FEATURE_LOG_INFO("%s::%s(), ret: %d\n", TAG, __FUNCTION__, ret);

    RecordObject* obj;

    obj = (RecordObject*)cookie;
    if (!obj || !obj->handle)
        return;

    if (!ret) {
        finish_callback(ret, NULL, cookie);
        if (obj->duration > 0)
            timeupdate_loop_timer(obj);
    } else {
        FEATURE_LOG_ERROR("%s::%s() error, ret:%d\n", TAG, __FUNCTION__, ret);
        media_uv_recorder_close(obj->handle, record_uv_close_cb);
    }

    obj->recordbusy = true;
}

static void record_uv_prepare_cb(void* cookie, int ret)
{
    FEATURE_LOG_INFO("%s::%s(), ret: %d\n", TAG, __FUNCTION__, ret);

    RecordObject* obj;

    obj = (RecordObject*)cookie;
    if (!obj || !obj->handle)
        return;

    if (ret < 0)
        goto fail;

#ifdef CONFIG_MEDIA_FOCUS
    ret = media_uv_recorder_start_auto(obj->handle, MEDIA_SCENARIO_RECORD,
        record_uv_start_cb, obj);
    if (ret < 0)
        goto fail;
#else
    ret = media_uv_recorder_start(obj->handle, record_uv_start_cb, obj);
    if (ret < 0)
        goto fail;
#endif
    return;

fail:
    FEATURE_LOG_ERROR("%s::%s() error, ret:%d\n", TAG, __FUNCTION__, ret);
    media_uv_recorder_close(obj->handle, record_uv_close_cb);
}

static void record_event_callback(void* cookie, int event,
    int ret, const char* extra)
{
    FEATURE_LOG_INFO("%s::%s() event:%s(%d) ret:%d extra:%s\n",
        TAG, __FUNCTION__, media_event_get_name(event),
        event, ret, extra);
}

static void record_uv_open_cb(void* cookie, int ret)
{
    FEATURE_LOG_INFO("%s::%s(), ret: %d\n", TAG, __FUNCTION__, ret);

    RecordObject* obj;

    obj = (RecordObject*)cookie;
    if (!obj || !obj->handle)
        return;

    if (media_uv_recorder_listen(obj->handle, record_event_callback) < 0)
        goto fail;

    if (media_uv_recorder_prepare(obj->handle, obj->filepath, obj->options,
            NULL, record_uv_prepare_cb, obj)
        < 0)
        goto fail;

    return;

fail:
    FEATURE_LOG_ERROR("%s::%s() error, ret:%d\n", TAG, __FUNCTION__, ret);
    media_uv_recorder_close(obj->handle, record_uv_close_cb);
}

/* warp function */
void system_record_wrap_start(FeatureInstanceHandle feature, union AppendData append_data,
    system_record_SetInfo* info)
{
    FEATURE_LOG_INFO("%s:%s: dur=%d, sr=%d, ch=%d, br=%d, fmt=%s",
        TAG, __FUNCTION__, info->duration, info->sampleRate,
        info->numberOfChannels, info->encodeBitRate, info->format);

    const char* formatcheck[] = { "wav", "opus", "pcm" };
    FeatureManagerHandle manager;
    char absolute_path[MAX_LEN];
    RecordObject* obj;
    struct tm* pdate;
    time_t timep;
    int i;

    obj = (RecordObject*)FeatureGetObjectData(feature);
    if (!obj)
        return;

    obj->feature = feature;
    obj->fail = info->fail;
    obj->success = info->success;
    obj->complete = info->complete;

    if (obj->recordbusy) {
        finish_callback(RECORD_BUSY, "recordbusy", obj);
        return;
    }

    if (info->numberOfChannels != 1 && info->numberOfChannels != 2) {
        finish_callback(INVALID_PARAM, "invalid number of channels", obj);
        return;
    }

    if (!info->format) {
        finish_callback(INVALID_PARAM, "format is NULL", obj);
        return;
    }

    for (i = 0; i < ARRAY_SIZE(formatcheck); i++) {
        if (!strcmp(formatcheck[i], info->format))
            break;
    }

    if (i == ARRAY_SIZE(formatcheck)) {
        finish_callback(INVALID_PARAM, "invalid format", obj);
        return;
    }

    obj->duration = info->duration;
    obj->encodeBitRate = info->encodeBitRate;
    obj->sampleRate = info->sampleRate ? info->sampleRate : DEFAULT_SAMPLE_RATE;

    strlcpy(obj->ch_layout_desc, info->numberOfChannels == 1 ? "mono" : "stereo",
        sizeof(obj->ch_layout_desc));
    strlcpy(obj->format, info->format, sizeof(obj->format));

    if (!strcmp(info->format, "opus"))
        snprintf(obj->options, sizeof(obj->options),
            "format=opus:sample_rate=%d:ch_layout=%s:b=%d:vbr=0:compression_level=1",
            obj->sampleRate, obj->ch_layout_desc, obj->encodeBitRate);
    else if (!strcmp(info->format, "wav"))
        snprintf(obj->options, sizeof(obj->options),
            "format=%s:sample_rate=%d:ch_layout=%s:b=%d",
            obj->format, obj->sampleRate, obj->ch_layout_desc, obj->encodeBitRate);
    else
        snprintf(obj->options, sizeof(obj->options),
            "format=s16le:sample_rate=%d:ch_layout=%s:b=%d",
            obj->sampleRate, obj->ch_layout_desc, obj->encodeBitRate);

    time(&timep);
    pdate = localtime(&timep);
    if (!pdate) {
        finish_callback(INVALID_PARAM, "record failed can't get localtime", obj);
        return;
    }

    snprintf(absolute_path, sizeof(absolute_path), CONFIG_HAP_APP_PATH "/%s/%s", RECORD_FILE_PATH,
        FeatureGetPackageName(FeatureGetProtoHandle(feature)));

    strftime(obj->basename, sizeof(obj->basename), "audio%Y%m%d%H%M%S.", pdate);
    strcat(obj->basename, info->format);
    snprintf(obj->filepath, sizeof(obj->filepath), "%s/%s", absolute_path, obj->basename);

    manager = FeatureGetManagerHandleFromInstance(feature);
    if (!manager)
        return;

    obj->handle = media_uv_recorder_open(FeatureGetUVLoop(manager), MEDIA_SOURCE_MIC,
        record_uv_open_cb, obj);
    if (!obj->handle)
        finish_callback(INVALID_PARAM, "record open failed", obj);

    return;
}

void system_record_wrap_stop(FeatureInstanceHandle feature, union AppendData append_data)
{
    FEATURE_LOG_INFO("%s::%s(),\n", TAG, __FUNCTION__);
    RecordObject* obj;

    obj = (RecordObject*)FeatureGetObjectData(feature);
    if (!obj || !obj->recordbusy || !obj->handle)
        return;

    if (obj->duration > 0)
        uv_timer_stop(&obj->timer);
    media_uv_recorder_stop(obj->handle, record_uv_stop_cb, obj);
}