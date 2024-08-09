/*
 * Copyright (C) 2024 Xiaomi Corporation. All rights reserved.
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
 */

#include "media_session.h"
#include "session.h"

#define INVOKE_SUCCESS_CB(cb, ...)                                 \
    do {                                                           \
        if (!cb)                                                   \
            break;                                                 \
        if (!FeatureInvokeCallback(handle, cb, ##__VA_ARGS__)) {   \
            FEATURE_LOG_ERROR("invoke success callback failed !"); \
        }                                                          \
    } while (0)

#define INVOKE_FAIL_CB(cb, msg, code)                           \
    do {                                                        \
        if (!cb)                                                \
            break;                                              \
        if (!FeatureInvokeCallback(handle, cb, msg, code)) {    \
            FEATURE_LOG_ERROR("invoke fail callback failed !"); \
        }                                                       \
    } while (0)

#define INVOKE_COMPLET_CB(cb)                                       \
    do {                                                            \
        if (!cb)                                                    \
            break;                                                  \
        if (!FeatureInvokeCallback(handle, cb)) {                   \
            FEATURE_LOG_ERROR("invoke complete callback failed !"); \
        }                                                           \
    } while (0)

#define REMOVE_ALL_CALLBACK(__succ__, __fail__, __complet__) \
    do {                                                     \
        FeatureRemoveCallback(handle, __succ__);             \
        FeatureRemoveCallback(handle, __fail__);             \
        FeatureRemoveCallback(handle, __complet__);          \
    } while (0)

static const char* file_tag = "[jidl_feature] session_impl";

#define EVENT_TYPE "{\"event\":\"%d\",\"ret\":\"%d\"}"

/****************************************************************************
 * Private Types
 ****************************************************************************/
typedef struct session_context_s {
    FeatureInstanceHandle instance;
    FeatureInterfaceHandle interface;
    FtCallbackId onstatuschange;
    char event_type[64];
    void* handle;
} session_t;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static session_t* session_obj_get(FeatureInterfaceHandle handle)
{
    return (session_t*)FeatureGetObjectData(handle);
}

static void session_feature_post_cb(int mode, void* data)
{
    session_t* session = (session_t*)data;

    FEATURE_LOG_INFO("%s::%s(), task mode: %d, event_type: %d\n", file_tag, __FUNCTION__, mode, session->event_type);
    if (mode == FEATURE_TASK_MODE_FREE) {
        FeatureRemoveCallback(session->interface, session->onstatuschange);
    } else if (mode == FEATURE_TASK_MODE_NORMAL) {
        if (!FeatureInvokeCallback(session->interface, session->onstatuschange, session->event_type)) {
            FEATURE_LOG_ERROR("invoke onstatuschange callback failed !");
        }
    } else {
        FEATURE_LOG_INFO("%s::%s(), Invalid task mode: %d\n", file_tag, __FUNCTION__, mode);
    }
}

void system_media_session_onRegister(const char* feature_name)
{
    FEATURE_LOG_INFO("%s::%s()\n", file_tag, __FUNCTION__);
}

void system_media_session_onCreate(FeatureRuntimeContext ctx, FeatureProtoHandle handle)
{
    FEATURE_LOG_INFO("%s::%s()\n", file_tag, __FUNCTION__);
}

void system_media_session_onRequired(FeatureRuntimeContext ctx, FeatureInstanceHandle handle)
{
    FEATURE_LOG_INFO("%s::%s()\n", file_tag, __FUNCTION__);
}

void system_media_session_onDetached(FeatureRuntimeContext ctx, FeatureInstanceHandle handle)
{
    FEATURE_LOG_INFO("%s::%s()\n", file_tag, __FUNCTION__);
}

void system_media_session_onDestroy(FeatureRuntimeContext ctx, FeatureProtoHandle handle)
{
    FEATURE_LOG_INFO("%s::%s()\n", file_tag, __FUNCTION__);
}

void system_media_session_onUnregister(const char* feature_name)
{
    FEATURE_LOG_INFO("%s::%s()\n", file_tag, __FUNCTION__);
}

// Function wrappers to be implemented
static void set_event_callback(void* user_data, int event, int ret, const char* data)
{
    session_t* session = (session_t*)user_data;

    FEATURE_LOG_INFO("%s::%s(), session event %d , ret %d\n", file_tag, __FUNCTION__, event, ret);
    if (!user_data) {
        FEATURE_LOG_ERROR("%s::%s(), session is NULL\n", file_tag, __FUNCTION__);
    }

    if (FeatureInstanceIsDetached(session->instance)) {
        FEATURE_LOG_ERROR("%s::%s(), FeatureInstanceIsDetached, FeatureInstanceHandle: %p\n", file_tag, __FUNCTION__, session->instance);
    }

    if (FeatureCheckCallbackId(session->interface, session->onstatuschange)) {
        snprintf(session->event_type, 64, EVENT_TYPE, event, ret);
        FeaturePost(session->interface, session_feature_post_cb, user_data);
    } else {
        FEATURE_LOG_INFO("%s::%s(), FeatureInterfaceHandle: %p, onstatuschange: %#X not registered, skip...\n", file_tag, __FUNCTION__, session->interface, session->onstatuschange);
    }
}

FeatureInterfaceHandle system_media_session_wrap_createSession(FeatureInstanceHandle feature, union AppendData append_data, FtString params)
{
    int ret = 0;
    session_t* session = (session_t*)calloc(1, sizeof(*session));

    FeatureInterfaceHandle handle = system_media_session_createSession_instance(feature);
    FEATURE_LOG_INFO("create session instance successfully\n");

    FeatureSetObjectData(handle, session);

    session->instance = FeatureDupInstanceHandle(feature);
    session->interface = handle;

    session->handle = media_session_open(params);
    if (!session->handle)
        return NULL;

    ret = media_session_set_event_callback(session->handle, session->handle, set_event_callback);

    if (ret < 0)
        FEATURE_LOG_ERROR("%s::%s(), FeatureInterfaceHandle: %p, session_open failed\n", file_tag, __FUNCTION__, handle);

    return handle;
}

void system_media_session_Session_interface_media_session_finalize(FeatureInterfaceHandle handle)
{
    FEATURE_LOG_INFO("%s::%s(), Finalize, FeatureInterfaceHandle: %p\n", file_tag, __FUNCTION__, handle);

    int ret = 0;
    session_t* session = session_obj_get(handle);

    if (!session) {
        FEATURE_LOG_INFO("%s::%s(), FeatureInterfaceHandle: %p, session is NULL\n", file_tag, __FUNCTION__, handle);
        return;
    }

    ret = media_session_close(session->handle);
    if (ret < 0)
        FEATURE_LOG_ERROR("%s::%s(), Close session failed, FeatureInstanceHandle: %p\n", file_tag, __FUNCTION__, session->instance);

    FeatureFreeInstanceHandle(session->instance);
    session->instance = NULL;

    free(session);
    session = NULL;
    FeatureSetObjectData(handle, NULL);
}

void system_media_session_Session_interface_media_session_start(FeatureInterfaceHandle handle, union AppendData append_data, system_media_session_Feedback* fb)
{
    int ret = 0;
    session_t* session = session_obj_get(handle);

    if (session->handle) {
        ret = media_session_start(session->handle);
        if (ret < 0) {
            INVOKE_FAIL_CB(fb->fail, "start session error", ret);
        } else {
            INVOKE_SUCCESS_CB(fb->success, "session start success");
        }
    }

    REMOVE_ALL_CALLBACK(fb->success, fb->fail, fb->complete);
}

void system_media_session_Session_interface_media_session_pause(FeatureInterfaceHandle handle, union AppendData append_data, system_media_session_Feedback* fb)
{
    int ret = 0;
    session_t* session = session_obj_get(handle);

    if (session->handle) {
        ret = media_session_pause(session->handle);
        if (ret < 0) {
            INVOKE_FAIL_CB(fb->fail, "pause session error", ret);
        }
    }

    REMOVE_ALL_CALLBACK(fb->success, fb->fail, fb->complete);
}

void system_media_session_Session_interface_media_session_stop(FeatureInterfaceHandle handle, union AppendData append_data, system_media_session_Feedback* fb)
{
    int ret = 0;
    session_t* session = session_obj_get(handle);

    if (session->handle) {
        ret = media_session_stop(session->handle);
        if (ret < 0) {
            INVOKE_FAIL_CB(fb->fail, "stop session error", ret);
        }
    }

    REMOVE_ALL_CALLBACK(fb->success, fb->fail, fb->complete);
}

void system_media_session_Session_interface_media_session_prev(FeatureInterfaceHandle handle, union AppendData append_data, system_media_session_Feedback* fb)
{
    int ret = 0;
    session_t* session = session_obj_get(handle);

    if (session->handle) {
        ret = media_session_prev_song(session->handle);
        if (ret < 0) {
            INVOKE_FAIL_CB(fb->fail, "prev session error", ret);
        }
    }

    REMOVE_ALL_CALLBACK(fb->success, fb->fail, fb->complete);
}

void system_media_session_Session_interface_media_session_next(FeatureInterfaceHandle handle, union AppendData append_data, system_media_session_Feedback* fb)
{
    int ret = 0;
    session_t* session = session_obj_get(handle);

    if (session->handle) {
        ret = media_session_next_song(session->handle);
        if (ret < 0) {
            INVOKE_FAIL_CB(fb->fail, "next session error", ret);
        }
    }

    REMOVE_ALL_CALLBACK(fb->success, fb->fail, fb->complete);
}

void system_media_session_Session_interface_media_session_increaseVolume(FeatureInterfaceHandle handle, union AppendData append_data, system_media_session_Feedback* fb)
{
    int ret = 0;
    session_t* session = session_obj_get(handle);

    if (session->handle) {
        ret = media_session_increase_volume(session->handle);
        if (ret < 0) {
            INVOKE_FAIL_CB(fb->fail, "increase session error", ret);
        }
    }

    REMOVE_ALL_CALLBACK(fb->success, fb->fail, fb->complete);
}

void system_media_session_Session_interface_media_session_decreaseVolume(FeatureInterfaceHandle handle, union AppendData append_data, system_media_session_Feedback* fb)
{
    int ret = 0;
    session_t* session = session_obj_get(handle);

    if (session->handle) {
        ret = media_session_decrease_volume(session->handle);
        if (ret < 0) {
            INVOKE_FAIL_CB(fb->fail, "decrease session error", ret);
        }
    }

    REMOVE_ALL_CALLBACK(fb->success, fb->fail, fb->complete);
}

void system_media_session_Session_interface_media_session_set_onEvent(FeatureInterfaceHandle handle, union AppendData append_data, FtCallbackId onstatuschange)
{
    session_t* session = session_obj_get(handle);

    if (!session) {
        FEATURE_LOG_ERROR("%s::%s(), FeatureInterfaceHandle: %p, session is NULL\n", file_tag, __FUNCTION__, handle);
        return;
    }

    session->onstatuschange = onstatuschange;
}
