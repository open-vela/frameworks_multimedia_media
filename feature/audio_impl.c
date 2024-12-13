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

#include "audio.h"
#include "feature_exports.h"
#include "media_api.h"

static const char* file_tag = "[jidl_feature] audio_impl";

#define MAX_URL_LEN 1024
#define MAX_TITLE_LEN 64
#define MAX_ALBUM_LEN 64
#define MAX_ARTIST_LEN 64
#define MAX_STREAMTYPE_LEN 10

#define MEDIA_STATE_STARTED 0
#define MEDIA_STATE_PAUSED 1
#define MEDIA_STATE_STOPPED 2

#define APP_PATH_PREFIX "internal://"

typedef struct {
    FeatureInstanceHandle feature;
    FtCallbackId callbackId;
} CallbackInfo;

typedef struct {
    CallbackInfo onplay;
    CallbackInfo onpause;
    CallbackInfo onstop;
    CallbackInfo onloadeddata;
    CallbackInfo onended;
    CallbackInfo ondurationchange;
    CallbackInfo ontimeupdate;
    CallbackInfo onerror;
} Event;

typedef struct {
    char title[MAX_TITLE_LEN];
    char album[MAX_ALBUM_LEN];
    char artist[MAX_ARTIST_LEN];
} MetaInfo;

typedef struct {
    void* handle;
    FeatureProtoHandle proto;
    Event event;
    uv_timer_t timer;

    char src[MAX_URL_LEN];
    MetaInfo meta;
    char streamType[MAX_STREAMTYPE_LEN];
    int state;
    float currentTime;
    float duration;
    float volume;
    bool autoplay;
    bool loop;
} AudioObject;

static void audio_uv_get_duration_cb(void* cookie, int ret, unsigned duration);
static void audio_uv_get_position_cb(void* cookie, int ret, unsigned position);
static void audio_uv_close_cb(void* cookie, int ret);
static void init_audio_obj(AudioObject* obj);
void system_audio_wrap_stop(FeatureInstanceHandle feature, union AppendData append_data);

/* common interface */
void system_audio_onRegister(const char* feature_name)
{
    FEATURE_LOG_DEBUG("%s::%s(), feature_name: %s\n", file_tag, __FUNCTION__, feature_name);
}

void system_audio_onCreate(FeatureRuntimeContext ctx, FeatureProtoHandle handle)
{
    FEATURE_LOG_INFO("%s::%s(), FeatureProtoHandle: %p\n", file_tag, __FUNCTION__, handle);
    AudioObject* obj;

    obj = (AudioObject*)malloc(sizeof(AudioObject));
    if (!obj) {
        FEATURE_LOG_ERROR("%s::%s(), malloc AudioObject fail\n", file_tag, __FUNCTION__);
        FeatureSetProtoData(handle, NULL);
        return;
    }

    init_audio_obj(obj);
    obj->proto = handle;
    memset(&obj->event, 0, sizeof(obj->event));

    FeatureSetProtoData(handle, obj);
}

void system_audio_onRequired(FeatureRuntimeContext ctx, FeatureInstanceHandle handle)
{
    FEATURE_LOG_DEBUG("%s::%s(), FeatureInstanceHandle: %p\n", file_tag, __FUNCTION__, handle);
}

void system_audio_onDetached(FeatureRuntimeContext ctx, FeatureInstanceHandle handle)
{
    FEATURE_LOG_DEBUG("%s::%s(), FeatureInstanceHandle: %p\n", file_tag, __FUNCTION__, handle);
}

void system_audio_onDestroy(FeatureRuntimeContext ctx, FeatureProtoHandle handle)
{
    FEATURE_LOG_INFO("%s::%s(), FeatureProtoHandle: %p\n", file_tag, __FUNCTION__, handle);

    AudioObject* obj;

    obj = (AudioObject*)FeatureGetProtoData(handle);

    if (!obj || !obj->handle)
        return;

    media_uv_player_close(obj->handle, 0, audio_uv_close_cb);
}

void system_audio_onUnregister(const char* feature_name)
{
    FEATURE_LOG_DEBUG("%s::%s(), feature_name: %s\n", file_tag, __FUNCTION__, feature_name);
}

/* inner interface */
static void init_audio_obj(AudioObject* obj)
{
    if (!obj)
        return;

    memset(obj->src, 0, MAX_URL_LEN);
    memset(obj->meta.title, 0, MAX_TITLE_LEN);
    memset(obj->meta.album, 0, MAX_ALBUM_LEN);
    memset(obj->meta.artist, 0, MAX_ARTIST_LEN);
    strncpy(obj->streamType, MEDIA_STREAM_MUSIC, MAX_STREAMTYPE_LEN);

    obj->handle = NULL;
    obj->state = MEDIA_STATE_STOPPED;
    obj->currentTime = 0;
    obj->duration = -1;
    obj->volume = 1;
    obj->autoplay = false;
    obj->loop = false;
}

static const char* get_state_string(int state)
{
    if (state == MEDIA_STATE_STARTED)
        return "play";
    else if (state == MEDIA_STATE_PAUSED)
        return "pause";
    else
        return "stop";
}

static void timeupdate_timer_cb(uv_timer_t* handle)
{
    AudioObject* obj;
    if (!handle)
        return;

    obj = (AudioObject*)handle->data;
    if (!obj)
        return;

    if (obj->state != MEDIA_STATE_STARTED) {
        uv_timer_stop(&obj->timer);
        return;
    }

    media_uv_player_get_position(obj->handle,
        audio_uv_get_position_cb, obj);
}

static void timeupdate_loop_timer(AudioObject* obj)
{
    FeatureManagerHandle manager;
    uv_loop_t* loop;
    if (!obj)
        return;

    manager = FeatureGetManagerHandleFromProto(obj->proto);
    loop = FeatureGetUVLoop(manager);
    if (!loop)
        return;

    uv_timer_init(loop, &obj->timer);
    obj->timer.data = obj;
    uv_timer_start(&obj->timer, timeupdate_timer_cb, 0, 250);
}

static void update_duration(AudioObject* obj)
{
    FEATURE_LOG_INFO("%s::%s(),\n", file_tag, __FUNCTION__);
    if (!obj || !obj->handle)
        return;

    media_uv_player_get_duration(obj->handle, audio_uv_get_duration_cb, obj);
}

static void system_audio_event_callback(void* cookie, int event, int ret, const char* data)
{
    FEATURE_LOG_INFO("%s::%s(), event:%d, ret: %d\n", file_tag, __FUNCTION__, event, ret);
    AudioObject* obj = (AudioObject*)cookie;

    if (!obj) {
        FEATURE_LOG_ERROR("%s::%s() fail, obj is null.\n", file_tag, __FUNCTION__);
        return;
    }

    if (ret < 0) {
        FEATURE_LOG_ERROR("%s::%s() fail, ret < 0.\n", file_tag, __FUNCTION__);
        media_uv_player_close(obj->handle, 0, NULL);
        init_audio_obj(obj);
        return;
    }

    switch (event) {
    case MEDIA_EVENT_PREPARED:
        if (FeatureCheckCallbackId(obj->event.onloadeddata.feature, obj->event.onloadeddata.callbackId))
            FeatureInvokeCallback(obj->event.onloadeddata.feature, obj->event.onloadeddata.callbackId);
        break;
    case MEDIA_EVENT_STARTED:
        obj->state = MEDIA_STATE_STARTED;
        if (FeatureCheckCallbackId(obj->event.onplay.feature, obj->event.onplay.callbackId))
            FeatureInvokeCallback(obj->event.onplay.feature, obj->event.onplay.callbackId);
        update_duration(obj);
        timeupdate_loop_timer(obj);
        break;
    case MEDIA_EVENT_PAUSED:
        obj->state = MEDIA_STATE_PAUSED;
        if (FeatureCheckCallbackId(obj->event.onpause.feature, obj->event.onpause.callbackId))
            FeatureInvokeCallback(obj->event.onpause.feature, obj->event.onpause.callbackId);
        break;
    case MEDIA_EVENT_STOPPED:
        obj->state = MEDIA_STATE_STOPPED;
        if (FeatureCheckCallbackId(obj->event.onstop.feature, obj->event.onstop.callbackId))
            FeatureInvokeCallback(obj->event.onstop.feature, obj->event.onstop.callbackId);
        break;
    case MEDIA_EVENT_COMPLETED:
        obj->state = MEDIA_STATE_STOPPED;
        if (FeatureCheckCallbackId(obj->event.onended.feature, obj->event.onended.callbackId))
            FeatureInvokeCallback(obj->event.onended.feature, obj->event.onended.callbackId);
        break;

    default:
        break;
    }
}

/* uv interface cb function */
static void audio_uv_prepare_cb(void* cookie, int ret)
{
    FEATURE_LOG_INFO("%s::%s(), ret: %d\n", file_tag, __FUNCTION__, ret);
    AudioObject* obj;

    obj = (AudioObject*)cookie;
    if (!obj || !obj->handle)
        return;

    if (ret < 0)
        goto fail;

    ret = media_uv_player_start(obj->handle, NULL, NULL);
    if (ret < 0)
        goto fail;

    return;

fail:
    FEATURE_LOG_ERROR("%s::%s() error, ret:%d\n", file_tag, __FUNCTION__, ret);
    media_uv_player_close(obj->handle, 0, NULL);
    init_audio_obj(obj);
}

static void audio_uv_open_cb(void* cookie, int ret)
{
    FEATURE_LOG_INFO("%s::%s(), ret: %d\n", file_tag, __FUNCTION__, ret);
    AudioObject* obj;
    obj = (AudioObject*)cookie;
    if (!obj || !obj->handle)
        return;

    if (ret < 0)
        goto fail;

    ret = media_uv_player_listen(obj->handle, system_audio_event_callback);
    if (ret < 0)
        goto fail;

    ret = media_uv_player_prepare(obj->handle, obj->src, NULL,
        NULL, audio_uv_prepare_cb, obj);
    if (ret < 0)
        goto fail;

    return;

fail:
    FEATURE_LOG_ERROR("%s::%s() error, ret:%d\n", file_tag, __FUNCTION__, ret);
    media_uv_player_close(obj->handle, 0, NULL);
    init_audio_obj(obj);
}

static void audio_uv_get_position_cb(void* cookie, int ret, unsigned position)
{
    FEATURE_LOG_INFO("%s::%s(),ret:%d, position:%d\n", file_tag, __FUNCTION__, ret, position);
    AudioObject* obj;

    obj = (AudioObject*)cookie;
    if (!obj)
        return;

    if (ret >= 0)
        obj->currentTime = position / 1000;

    if (FeatureCheckCallbackId(obj->event.ontimeupdate.feature, obj->event.ontimeupdate.callbackId))
        FeatureInvokeCallback(obj->event.ontimeupdate.feature, obj->event.ontimeupdate.callbackId);
}

static void audio_uv_get_duration_cb(void* cookie, int ret, unsigned duration)
{
    FEATURE_LOG_INFO("%s::%s(),ret:%d, duration:%d\n", file_tag, __FUNCTION__, ret, duration);
    AudioObject* obj;
    obj = (AudioObject*)cookie;
    if (!obj)
        return;

    if (ret >= 0)
        obj->duration = duration / 1000;

    if (FeatureCheckCallbackId(obj->event.ondurationchange.feature, obj->event.ondurationchange.callbackId))
        FeatureInvokeCallback(obj->event.ondurationchange.feature, obj->event.ondurationchange.callbackId);
}

static void audio_uv_close_cb(void* cookie, int ret)
{
    FEATURE_LOG_INFO("%s::%s()\n", file_tag, __FUNCTION__);
    AudioObject* obj;

    obj = (AudioObject*)cookie;
    if (obj)
        free(obj);
}

/* warp function */
void system_audio_wrap_play(FeatureInstanceHandle feature, union AppendData append_data)
{
    FEATURE_LOG_INFO("%s::%s(),\n", file_tag, __FUNCTION__);
    AudioObject* obj;
    FeatureManagerHandle manager;

    obj = (AudioObject*)FeatureGetProtoData(FeatureGetProtoHandle(feature));
    if (!obj)
        return;

    if (obj->state == MEDIA_STATE_STARTED)
        return;

    // resume
    if (obj->state == MEDIA_STATE_PAUSED && obj->handle) {
        media_uv_player_start(obj->handle, NULL, NULL);
        return;
    }

    manager = FeatureGetManagerHandleFromInstance(feature);
    obj->handle = media_uv_player_open(FeatureGetUVLoop(manager), MEDIA_STREAM_MUSIC,
        audio_uv_open_cb, obj);
}

void system_audio_wrap_pause(FeatureInstanceHandle feature, union AppendData append_data)
{
    FEATURE_LOG_INFO("%s::%s(),\n", file_tag, __FUNCTION__);
    AudioObject* obj;

    obj = (AudioObject*)FeatureGetProtoData(FeatureGetProtoHandle(feature));
    if (!obj || !obj->handle)
        return;

    if (obj->state != MEDIA_STATE_STARTED)
        return;

    media_uv_player_pause(obj->handle, NULL, NULL);
}

void system_audio_wrap_stop(FeatureInstanceHandle feature, union AppendData append_data)
{
    FEATURE_LOG_INFO("%s::%s(),\n", file_tag, __FUNCTION__);
    AudioObject* obj;

    obj = (AudioObject*)FeatureGetProtoData(FeatureGetProtoHandle(feature));
    if (!obj || !obj->handle)
        return;

    if (obj->state == MEDIA_STATE_STOPPED)
        return;

    media_uv_player_close(obj->handle, 0, NULL);
    init_audio_obj(obj);
}

void system_audio_wrap_getPlayState(FeatureInstanceHandle feature, union AppendData append_data, system_audio_GetPalyStateParam* p)
{
    FEATURE_LOG_INFO("%s::%s(),\n", file_tag, __FUNCTION__);
    char* info;
    AudioObject* obj;
    system_audio_AudioState* audiostate;

    obj = (AudioObject*)FeatureGetProtoData(FeatureGetProtoHandle(feature));
    if (!obj)
        return;

    audiostate = system_audioMallocAudioState();

    info = (char*)FeatureMalloc(strlen(get_state_string(obj->state)) + 1, FT_CHAR);
    strcpy(info, get_state_string(obj->state));
    audiostate->state = info;
    info = (char*)FeatureMalloc(strlen(obj->src) + 1, FT_CHAR);
    strcpy(info, obj->src);
    audiostate->src = info;
    audiostate->currentTime = obj->currentTime;
    audiostate->autoplay = obj->autoplay;
    audiostate->loop = obj->loop;
    audiostate->volume = obj->volume;
    audiostate->mute = obj->volume == 0;
    audiostate->duration = obj->duration;

    if (FeatureCheckCallbackId(feature, p->success)) {
        FeatureInvokeCallback(feature, p->success, audiostate);
        FeatureRemoveCallback(feature, p->success);
    }
    if (FeatureCheckCallbackId(feature, p->complete)) {
        FeatureInvokeCallback(feature, p->complete);
        FeatureRemoveCallback(feature, p->complete);
    }
    if (FeatureCheckCallbackId(feature, p->fail))
        FeatureRemoveCallback(feature, p->fail);

    FeatureFreeValue(audiostate);
}

/* property function */
FtString system_audio_get_src(void* feature, union AppendData append_data)
{
    FEATURE_LOG_INFO("%s::%s(),\n", file_tag, __FUNCTION__);

    AudioObject* obj;

    obj = (AudioObject*)FeatureGetProtoData(FeatureGetProtoHandle(feature));
    if (!obj)
        return NULL;

    FtString src = (FtString)FeatureMalloc(MAX_URL_LEN + 1, FT_CHAR);
    strncpy((char*)src, obj->src, MAX_URL_LEN);

    return src;
}

void system_audio_set_src(void* feature, union AppendData append_data, FtString src)
{
    FEATURE_LOG_INFO("%s::%s(),\n", file_tag, __FUNCTION__);

    AudioObject* obj;
    const char* pkg;

    obj = (AudioObject*)FeatureGetProtoData(FeatureGetProtoHandle(feature));
    if (!obj || !src)
        return;

    pkg = FeatureGetPackageName(FeatureGetProtoHandle(feature));
    if (!pkg)
        return;

    if (!strncmp(src, APP_PATH_PREFIX, strlen(APP_PATH_PREFIX))) {
        const char* offset = src + strlen(APP_PATH_PREFIX);
        const char* type_end = strchr(offset, '/');
        if (!type_end) {
            FEATURE_LOG_ERROR("Invalid path format: %s", src);
            return;
        }

        int type_len = type_end - offset;
        if (type_len >= sizeof(obj->src) || type_len == 0) {
            FEATURE_LOG_ERROR("Type part is too long or empty");
            return;
        }
        const char* vaild_types[] = { "cache", "file", "mass", "tmp" };
        const size_t vaild_types_count = sizeof(vaild_types) / sizeof(vaild_types[0]);

        int type_valid = 0;
        for (int i = 0; i < vaild_types_count; ++i) {
            if (!strncmp(vaild_types[i], offset, type_len)) {
                type_valid = 1;
                break;
            }
        }

        if (!type_valid) {
            FEATURE_LOG_ERROR("Invalid type in src: %s", src);
            return;
        }

        if (!strcmp(offset, "tmp"))
            snprintf(obj->src, sizeof(obj->src), "%s/%.*s%s",
                CONFIG_HAP_APP_PATH, (int)type_len, offset,
                type_end + 1);
        else
            snprintf(obj->src, sizeof(obj->src), "%s/%.*s/%s/%s",
                CONFIG_HAP_APP_PATH, (int)type_len, offset,
                pkg, type_end + 1);
    } else if (*src == '/')
        snprintf(obj->src, sizeof(obj->src), "%s/app/%s%s",
            CONFIG_HAP_APP_PATH, pkg, src);
    else
        strncpy(obj->src, src, sizeof(obj->src));

    FEATURE_LOG_INFO("audio set src:%s", obj->src);
}

void system_audio_set_meta(void* feature, union AppendData append_data, system_audio_MetaInfo* meta)
{
    FEATURE_LOG_INFO("%s::%s(),\n", file_tag, __FUNCTION__);
    AudioObject* obj;
    obj = (AudioObject*)FeatureGetProtoData(FeatureGetProtoHandle(feature));
    if (!obj)
        return;
    if (meta->title != NULL)
        strncpy(obj->meta.title, meta->title, MAX_TITLE_LEN);
    if (meta->album != NULL)
        strncpy(obj->meta.album, meta->album, MAX_ALBUM_LEN);
    if (meta->artist != NULL)
        strncpy(obj->meta.artist, meta->artist, MAX_ARTIST_LEN);
}

FtFloat system_audio_get_currentTime(void* feature, union AppendData append_data)
{
    FEATURE_LOG_INFO("%s::%s(),\n", file_tag, __FUNCTION__);
    AudioObject* obj;

    obj = (AudioObject*)FeatureGetProtoData(FeatureGetProtoHandle(feature));
    if (!obj)
        return 0;

    return obj->currentTime;
}

void system_audio_set_currentTime(void* feature, union AppendData append_data, FtFloat currentTime)
{
    FEATURE_LOG_INFO("%s::%s(),\n", file_tag, __FUNCTION__);
    AudioObject* obj;

    obj = (AudioObject*)FeatureGetProtoData(FeatureGetProtoHandle(feature));
    if (!obj || !obj->handle)
        return;

    media_uv_player_seek(obj->handle, currentTime, NULL, NULL);
    obj->currentTime = currentTime;
}

FtFloat system_audio_get_duration(void* feature, union AppendData append_data)
{
    FEATURE_LOG_INFO("%s::%s(),\n", file_tag, __FUNCTION__);
    AudioObject* obj;

    obj = (AudioObject*)FeatureGetProtoData(FeatureGetProtoHandle(feature));
    if (!obj)
        return -1;

    return obj->duration;
}

FtBool system_audio_get_autoplay(void* feature, union AppendData append_data)
{
    FEATURE_LOG_INFO("%s::%s(),\n", file_tag, __FUNCTION__);
    AudioObject* obj;

    obj = (AudioObject*)FeatureGetProtoData(FeatureGetProtoHandle(feature));
    if (!obj)
        return false;

    return obj->autoplay;
}

void system_audio_set_autoplay(void* feature, union AppendData append_data, FtBool autoplay)
{
    FEATURE_LOG_INFO("%s::%s(),\n", file_tag, __FUNCTION__);
    AudioObject* obj;

    obj = (AudioObject*)FeatureGetProtoData(FeatureGetProtoHandle(feature));
    if (!obj)
        return;

    obj->autoplay = autoplay;
}

FtBool system_audio_get_loop(void* feature, union AppendData append_data)
{
    FEATURE_LOG_INFO("%s::%s(),\n", file_tag, __FUNCTION__);
    AudioObject* obj;

    obj = (AudioObject*)FeatureGetProtoData(FeatureGetProtoHandle(feature));
    if (!obj)
        return false;

    return obj->loop;
}

void system_audio_set_loop(void* feature, union AppendData append_data, FtBool loop)
{
    FEATURE_LOG_INFO("%s::%s(),\n", file_tag, __FUNCTION__);
    AudioObject* obj;

    obj = (AudioObject*)FeatureGetProtoData(FeatureGetProtoHandle(feature));
    if (!obj || !obj->handle)
        return;

    media_uv_player_set_looping(obj->handle, loop ? -1 : 0, NULL, NULL);
    obj->loop = loop;
}

FtFloat system_audio_get_volume(void* feature, union AppendData append_data)
{
    FEATURE_LOG_INFO("%s::%s(),\n", file_tag, __FUNCTION__);
    AudioObject* obj;

    obj = (AudioObject*)FeatureGetProtoData(FeatureGetProtoHandle(feature));
    if (!obj)
        return 0;

    return obj->volume;
}

void system_audio_set_volume(void* feature, union AppendData append_data, FtFloat volume)
{
    FEATURE_LOG_INFO("%s::%s(), volume:%f\n", file_tag, __FUNCTION__, volume);
    AudioObject* obj;

    obj = (AudioObject*)FeatureGetProtoData(FeatureGetProtoHandle(feature));
    if (!obj || !obj->handle)
        return;

    media_uv_player_set_volume(obj->handle, volume, NULL, NULL);
    obj->volume = volume;
}

FtBool system_audio_get_muted(void* feature, union AppendData append_data)
{
    FEATURE_LOG_INFO("%s::%s(),\n", file_tag, __FUNCTION__);
    return system_audio_get_volume(feature, append_data) == 0;
}

void system_audio_set_muted(void* feature, union AppendData append_data, FtBool muted)
{
    FEATURE_LOG_INFO("%s::%s(),\n", file_tag, __FUNCTION__);
    return system_audio_set_volume(feature, append_data, 0);
}

FtString system_audio_get_streamType(void* feature, union AppendData append_data)
{
    FEATURE_LOG_INFO("%s::%s(),\n", file_tag, __FUNCTION__);
    AudioObject* obj;

    obj = (AudioObject*)FeatureGetProtoData(FeatureGetProtoHandle(feature));
    if (!obj)
        return MEDIA_STREAM_MUSIC;

    return obj->streamType;
}

/* event funciton*/
void system_audio_set_onplay(void* feature, union AppendData append_data, FtCallbackId onplay)
{
    FEATURE_LOG_INFO("%s::%s(),\n", file_tag, __FUNCTION__);
    AudioObject* obj;

    obj = (AudioObject*)FeatureGetProtoData(FeatureGetProtoHandle(feature));
    if (!obj)
        return;

    obj->event.onplay.callbackId = onplay;
    obj->event.onplay.feature = feature;
}

void system_audio_set_onpause(void* feature, union AppendData append_data, FtCallbackId onpause)
{
    FEATURE_LOG_INFO("%s::%s(),\n", file_tag, __FUNCTION__);
    AudioObject* obj;

    obj = (AudioObject*)FeatureGetProtoData(FeatureGetProtoHandle(feature));
    if (!obj)
        return;

    obj->event.onpause.callbackId = onpause;
    obj->event.onpause.feature = feature;
}

void system_audio_set_onstop(void* feature, union AppendData append_data, FtCallbackId onstop)
{
    FEATURE_LOG_INFO("%s::%s(),\n", file_tag, __FUNCTION__);
    AudioObject* obj;

    obj = (AudioObject*)FeatureGetProtoData(FeatureGetProtoHandle(feature));
    if (!obj)
        return;

    obj->event.onstop.callbackId = onstop;
    obj->event.onstop.feature = feature;
}

void system_audio_set_onloadeddata(void* feature, union AppendData append_data, FtCallbackId onloadeddata)
{
    FEATURE_LOG_DEBUG("%s::%s(),\n", file_tag, __FUNCTION__);
    AudioObject* obj;

    obj = (AudioObject*)FeatureGetProtoData(FeatureGetProtoHandle(feature));
    if (!obj)
        return;

    obj->event.onloadeddata.callbackId = onloadeddata;
    obj->event.onloadeddata.feature = feature;
}

void system_audio_set_onended(void* feature, union AppendData append_data, FtCallbackId onended)
{
    FEATURE_LOG_DEBUG("%s::%s(),\n", file_tag, __FUNCTION__);
    AudioObject* obj;

    obj = (AudioObject*)FeatureGetProtoData(FeatureGetProtoHandle(feature));
    if (!obj)
        return;

    obj->event.onended.callbackId = onended;
    obj->event.onended.feature = feature;
}

void system_audio_set_ondurationchange(void* feature, union AppendData append_data, FtCallbackId ondurationchange)
{
    FEATURE_LOG_DEBUG("%s::%s(),\n", file_tag, __FUNCTION__);
    AudioObject* obj;

    obj = (AudioObject*)FeatureGetProtoData(FeatureGetProtoHandle(feature));
    if (!obj)
        return;

    obj->event.ondurationchange.callbackId = ondurationchange;
    obj->event.ondurationchange.feature = feature;
}

void system_audio_set_ontimeupdate(void* feature, union AppendData append_data, FtCallbackId ontimeupdate)
{
    FEATURE_LOG_DEBUG("%s::%s(),\n", file_tag, __FUNCTION__);
    AudioObject* obj;

    obj = (AudioObject*)FeatureGetProtoData(FeatureGetProtoHandle(feature));
    if (!obj)
        return;

    obj->event.ontimeupdate.callbackId = ontimeupdate;
    obj->event.ontimeupdate.feature = feature;
}

void system_audio_set_onerror(void* feature, union AppendData append_data, FtCallbackId onerror)
{
    FEATURE_LOG_DEBUG("%s::%s(),\n", file_tag, __FUNCTION__);
    AudioObject* obj;

    obj = (AudioObject*)FeatureGetProtoData(FeatureGetProtoHandle(feature));
    if (!obj)
        return;

    obj->event.onerror.callbackId = onerror;
    obj->event.onerror.feature = feature;
}