/*
 * Copyright (C) 2023 Xiaomi Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * 	 http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "feature_log.h"
#include "media_defs.h"
#include "media_policy.h"
#include "volume.h"

static const char* file_tag = "[jidl_feature] volume_impl";
#define ERROR_CODE 202
#define ERROR_BCL 209

enum OP {
    VOLUME_OP_GET,
    VOLUME_OP_SET,
    VOLUME_OP_NONE,
};

typedef struct {
    FeatureInstanceHandle feature;
    enum OP op; /* The operation */
    FtCallbackId success;
    FtCallbackId fail;
    FtCallbackId complete;
    double value;
} VolumeHandle;

typedef struct {
    void* policy_handle;
    FeatureInstanceHandle feature;
} SubscriberHandle;

static void volume_change_callback(void* cookie, int number, const char* literal)
{
    SubscriberHandle* sub_handle = cookie;
    FEATURE_LOG_INFO("%s::%s() number:%d\n", file_tag, __FUNCTION__, number);

    if (!sub_handle) {
        FEATURE_LOG_ERROR("%s::%s(), sub_handle is null!", file_tag, __FUNCTION__);
        return;
    }

    system_volume_GetRet* volumeRet = system_volumeMallocGetRet();
    volumeRet->value = (double)(number / 10.0);
    int cb_count = FeatureGetEventCallbackCountByName(sub_handle->feature, "onMediaValueChanged");
    if (cb_count > 0) {
        FeatureEmitEventByName(sub_handle->feature, "onMediaValueChanged", volumeRet);
    }
}

static VolumeHandle* volume_malloc(FeatureInstanceHandle feature)
{
    VolumeHandle* handle = (VolumeHandle*)malloc(sizeof(VolumeHandle));
    handle->feature = feature;
    handle->op = VOLUME_OP_NONE;
    handle->value = 0;
    handle->complete = -1;
    handle->success = -1;
    handle->fail = -1;
    return handle;
}

static void volume_free(VolumeHandle* handle)
{
    if (handle == NULL)
        return;

    free(handle);
    handle = NULL;
}

static void finish_callback(int status, FeatureInstanceHandle feature, const char* msg,
    system_volume_GetRet* volumeRet, VolumeHandle* handle)
{
    if (handle == NULL)
        return;

    if ((status == 0) && (FeatureCheckCallbackId(feature, handle->success)))
        FeatureInvokeCallback(feature, handle->success, volumeRet);
    else if (FeatureCheckCallbackId(feature, handle->fail))
        FeatureInvokeCallback(feature, handle->fail, msg, status);

    if (FeatureCheckCallbackId(feature, handle->complete))
        FeatureInvokeCallback(feature, handle->complete, (status == 0) ? "success" : "fail");

    FeatureRemoveCallback(feature, handle->success);
    FeatureRemoveCallback(feature, handle->fail);
    FeatureRemoveCallback(feature, handle->complete);
    volume_free(handle);
}

static void volume_set_cb(void* arg, int ret)
{
    if (arg == NULL)
        return;

    VolumeHandle* handle = (VolumeHandle*)(arg);
    FEATURE_LOG_INFO("%s::%s() op:%d ret:%d\n", file_tag, __FUNCTION__, handle->op, ret);
    if (ret >= 0) {
        system_volume_GetRet* volumeRet = system_volumeMallocGetRet();
        volumeRet->value = handle->value;
        finish_callback(0, handle->feature, "success", volumeRet, handle);
    } else
        finish_callback(ret, handle->feature, "volume_set_cb failed", NULL, handle);
}

static void volume_get_cb(void* arg, int ret, int value)
{
    if (arg == NULL)
        return;

    VolumeHandle* handle = (VolumeHandle*)(arg);
    FEATURE_LOG_INFO("%s::%s() op:%d ret:%d\n", file_tag, __FUNCTION__, handle->op, ret);
    if (ret >= 0) {
        if ((value >= 0) && (value <= 10)) {
            system_volume_GetRet* volumeRet = system_volumeMallocGetRet();
            volumeRet->value = (double)(value / 10.0);
            finish_callback(0, handle->feature, "success", volumeRet, handle);
        } else
            finish_callback(-1, handle->feature, "volume_get_cb volume invalid", NULL, handle);
    } else
        finish_callback(-1, handle->feature, "volume_get_cb volume invalid", NULL, handle);
}

void system_volume_onRegister(const char* feature_name)
{
    FEATURE_LOG_INFO("%s::%s()\n", file_tag, __FUNCTION__);
}

void system_volume_onCreate(FeatureRuntimeContext ctx, FeatureProtoHandle handle)
{
    FEATURE_LOG_INFO("%s::%s()\n", file_tag, __FUNCTION__);
}

void system_volume_onRequired(FeatureRuntimeContext ctx, FeatureInstanceHandle handle)
{
    FEATURE_LOG_INFO("%s::%s()\n", file_tag, __FUNCTION__);

    SubscriberHandle* sub_handle = (SubscriberHandle*)malloc(sizeof(SubscriberHandle));
    if (!sub_handle) {
        FEATURE_LOG_ERROR("%s::%s(), calloc SubscriberHandle failed\n", file_tag, __FUNCTION__);
        return;
    }

    sub_handle->feature = handle;
    sub_handle->policy_handle = media_policy_subscribe(MEDIA_SCENARIO_MUSIC MEDIA_POLICY_VOLUME, volume_change_callback, sub_handle);
    if (!sub_handle->policy_handle) {
        FEATURE_LOG_ERROR("%s::%s(), volume subscribe failed\n", file_tag, __FUNCTION__);
        free(sub_handle);
        return;
    }

    FeatureSetObjectData(handle, sub_handle);
}

void system_volume_onDetached(FeatureRuntimeContext ctx, FeatureInstanceHandle handle)
{
    FEATURE_LOG_INFO("%s::%s()\n", file_tag, __FUNCTION__);
    SubscriberHandle* sub_handle;
    sub_handle = (SubscriberHandle*)FeatureGetObjectData(handle);
    if (!sub_handle)
        return;

    if (sub_handle->policy_handle)
        media_policy_unsubscribe(sub_handle->policy_handle);

    free(sub_handle);
}

void system_volume_onDestroy(FeatureRuntimeContext ctx, FeatureProtoHandle handle)
{
    FEATURE_LOG_INFO("%s::%s()\n", file_tag, __FUNCTION__);
}

void system_volume_onUnregister(const char* feature_name)
{
    FEATURE_LOG_INFO("%s::%s()\n", file_tag, __FUNCTION__);
}

void system_volume_wrap_setMediaValue(FeatureInstanceHandle feature, AppendData data, system_volume_SetInfo* info)
{
    FEATURE_LOG_INFO("%s::%s():value=%f.", file_tag, __FUNCTION__, info->value);

    VolumeHandle* handle = volume_malloc(feature);
    if (handle == NULL) {
        FEATURE_LOG_ERROR("%s::%s() VolumeHandle malloc failed\n", file_tag, __FUNCTION__);
        return finish_callback(ERROR_CODE, feature, "volume malloc fail", NULL, handle);
    }

    handle->op = VOLUME_OP_SET;
    handle->value = info->value;
    handle->success = info->success;
    handle->fail = info->fail;
    handle->complete = info->complete;

    if ((handle->value < 0) || (handle->value > 1)) {
        FEATURE_LOG_ERROR("%s::%s() volume input argurments invalid\n", file_tag, __FUNCTION__);
        return finish_callback(ERROR_CODE, feature, "param is invalid", NULL, handle);
    }

    FeatureManagerHandle manager = FeatureGetManagerHandleFromInstance(feature);
    int status = media_uv_policy_set_stream_volume(FeatureGetUVLoop(manager), MEDIA_STREAM_MUSIC,
        (int)(handle->value * 10), volume_set_cb, (void*)handle);
    if (status != 0) {
        FEATURE_LOG_ERROR("%s::%s() media_uv_policy_set_stream_volume failed, ret %d\n", file_tag, __FUNCTION__, status);
        return finish_callback(ERROR_CODE, feature, "media_uv_policy_set_stream_volume fail", NULL, handle);
    }
}

void system_volume_wrap_getMediaValue(FeatureInstanceHandle feature, AppendData data, system_volume_GetInfo* info)
{
    FEATURE_LOG_INFO("%s::%s()\n", file_tag, __FUNCTION__);
    VolumeHandle* handle = volume_malloc(feature);
    if (handle == NULL) {
        FEATURE_LOG_ERROR("%s::%s() VolumeHandle malloc failed\n", file_tag, __FUNCTION__);
        return finish_callback(ERROR_CODE, feature, "volume malloc fail", NULL, handle);
    }

    handle->op = VOLUME_OP_GET;
    handle->success = info->success;
    handle->fail = info->fail;
    handle->complete = info->complete;

    FeatureManagerHandle manager = FeatureGetManagerHandleFromInstance(feature);
    int status = media_uv_policy_get_stream_volume(FeatureGetUVLoop(manager), MEDIA_STREAM_MUSIC,
        volume_get_cb, (void*)handle);
    if (status != 0) {
        FEATURE_LOG_ERROR("%s::%s() media_uv_policy_get_stream_volume failed, ret %d\n", file_tag, __FUNCTION__, status);
        return finish_callback(ERROR_CODE, feature, "media_uv_policy_get_stream_volume failed", NULL, handle);
    }
}
