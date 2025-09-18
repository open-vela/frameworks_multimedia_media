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

#define AUDIO_TIMEUPDATE_TIMEOUT 1000

#define APP_PATH_PREFIX "internal://"

#define EPSILON 1e-6

typedef enum {
    MEDIA_STATE_NONE,
    MEDIA_STATE_OPENING,
    MEDIA_STATE_OPENED,
    MEDIA_STATE_STOPPED,
    MEDIA_STATE_PREPARED,
    MEDIA_STATE_STARTED,
    MEDIA_STATE_PAUSED,
} PlayerState;

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
    CallbackInfo onctrlplayprev;
    CallbackInfo onctrlplaynext;
} Event;

typedef struct {
    char title[MAX_TITLE_LEN];
    char album[MAX_ALBUM_LEN];
    char artist[MAX_ARTIST_LEN];
} MetaInfo;

typedef struct {
    void* player;
    void* session;
    FeatureProtoHandle proto;
    Event event;
    uv_timer_t timer;

    char src[MAX_URL_LEN];
    MetaInfo meta;
    char streamType[MAX_STREAMTYPE_LEN];
    PlayerState state;
    float currentTime;
    float duration;
    float percent;
    double volume;
    double mutedvolume; /* Store volume before mute. */
    bool autoplay;
    bool loop;
} AudioObject;

/* uv interface cb function */
static void audio_get_duration_cb(void* cookie, int ret, unsigned duration);
static void audio_media_player_query_cb(void* cookie, int ret, void* object);
static void audio_session_close_cb(void* cookie, int ret);
static void audio_player_close_cb(void* cookie, int ret);
static void audio_timer_close_cb(uv_handle_t* handle);
static void audio_start_cb(void* cookie, int ret);
static void audio_open_cb(void* cookie, int ret);

/*event callback*/
static void audio_player_event_callback(void* cookie, int event, int ret, const char* data);
static void audio_session_event_callback(void* cookie, int event, int ret, const char* extra);

/* inner interface */
static void audio_try_free(AudioObject* obj);
static void audio_reset_obj(AudioObject* obj);
static void audio_close(AudioObject* obj);

/* common interface */
void system_audio_onRegister(const char* feature_name)
{
    FEATURE_LOG_DEBUG("%s::%s(), feature_name: %s\n", file_tag, __FUNCTION__, feature_name);
}

void system_audio_onCreate(FeatureRuntimeContext ctx, FeatureProtoHandle handle)
{
    FEATURE_LOG_INFO("%s::%s(), FeatureProtoHandle: %p\n", file_tag, __FUNCTION__, handle);

    FeatureManagerHandle manager;
    AudioObject* obj;
    uv_loop_t* loop;
    int volume;
    int ret;

    obj = (AudioObject*)calloc(1, sizeof(AudioObject));
    if (!obj) {
        FEATURE_LOG_ERROR("%s::%s(), calloc AudioObject failed\n", file_tag, __FUNCTION__);
        return;
    }

    audio_reset_obj(obj);
    obj->proto = handle;
    obj->state = MEDIA_STATE_OPENING;

    manager = FeatureGetManagerHandleFromProto(handle);
    loop = FeatureGetUVLoop(manager);
    obj->session = media_uv_session_register(loop, NULL, audio_session_event_callback, obj);

    ret = media_policy_get_stream_volume(obj->streamType, &volume);
    if (ret >= 0)
        obj->volume = (double)(volume / 10.0);

    if (!obj->session) {
        FEATURE_LOG_ERROR("%s::%s(), session register failed\n", file_tag, __FUNCTION__);
        goto cleanup;
    }

    obj->player = media_uv_player_open(loop, obj->streamType, audio_open_cb, obj);
    if (!obj->player) {
        FEATURE_LOG_ERROR("%s::%s(), player open failed\n", file_tag, __FUNCTION__);
        goto cleanup;
    }

    obj->state = MEDIA_STATE_OPENED;
    if (media_uv_player_listen(obj->player, audio_player_event_callback) < 0) {
        FEATURE_LOG_ERROR("%s::%s(), player listen failed\n", file_tag, __FUNCTION__);
        goto cleanup;
    }

    uv_timer_init(loop, &obj->timer);
    obj->timer.data = obj;

    FeatureSetProtoData(handle, obj);
    return;

cleanup:
    if (obj->player)
        media_uv_player_close(obj->player, 0, NULL);
    if (obj->session)
        media_uv_session_unregister(obj->session, NULL);
    free(obj);

    return;
}

void system_audio_onRequired(FeatureRuntimeContext ctx, FeatureInstanceHandle handle)
{
    FEATURE_LOG_INFO("%s::%s(), FeatureInstanceHandle: %p\n", file_tag, __FUNCTION__, handle);
}

void system_audio_onDetached(FeatureRuntimeContext ctx, FeatureInstanceHandle handle)
{
    AudioObject* obj;

    obj = (AudioObject*)FeatureGetProtoData(FeatureGetProtoHandle(handle));
    if (!obj)
        return;

    FEATURE_LOG_INFO("%s::%s(), FeatureInstanceHandle: %p\n", file_tag, __FUNCTION__, handle);
    if (obj->event.onplay.feature == handle) {
        obj->event.onplay.feature = NULL;
    }
    if (obj->event.onpause.feature == handle) {
        obj->event.onpause.feature = NULL;
    }

    if (obj->event.onstop.feature == handle) {
        obj->event.onstop.feature = NULL;
    }

    if (obj->event.onloadeddata.feature == handle) {
        obj->event.onloadeddata.feature = NULL;
    }

    if (obj->event.onended.feature == handle) {
        obj->event.onended.feature = NULL;
    }

    if (obj->event.ondurationchange.feature == handle) {
        obj->event.ondurationchange.feature = NULL;
    }

    if (obj->event.ontimeupdate.feature == handle) {
        obj->event.ontimeupdate.feature = NULL;
    }

    if (obj->event.onerror.feature == handle) {
        obj->event.onerror.feature = NULL;
    }
}

void system_audio_onDestroy(FeatureRuntimeContext ctx, FeatureProtoHandle handle)
{
    FEATURE_LOG_INFO("%s::%s(), FeatureProtoHandle: %p\n", file_tag, __FUNCTION__, handle);

    AudioObject* obj;

    obj = (AudioObject*)FeatureGetProtoData(handle);
    if (!obj)
        return;

    uv_close((uv_handle_t*)&obj->timer, audio_timer_close_cb);
}

void system_audio_onUnregister(const char* feature_name)
{
    FEATURE_LOG_DEBUG("%s::%s(), feature_name: %s\n", file_tag, __FUNCTION__, feature_name);
}

/* inner interface */
static void audio_reset_obj(AudioObject* obj)
{
    if (!obj)
        return;

    memset(obj->src, 0, MAX_URL_LEN);
    memset(obj->meta.title, 0, MAX_TITLE_LEN);
    memset(obj->meta.album, 0, MAX_ALBUM_LEN);
    memset(obj->meta.artist, 0, MAX_ARTIST_LEN);
    strncpy(obj->streamType, MEDIA_STREAM_MUSIC, MAX_STREAMTYPE_LEN);

    obj->percent = 0;
    obj->currentTime = 0;
    obj->duration = -1;
    obj->autoplay = false;
    obj->loop = false;
}

static void audio_close(AudioObject* obj)
{
    int ret;

    if (!obj)
        return;

    if (!obj->player && !obj->session) {
        free(obj);
        return;
    }

    ret = media_uv_player_close(obj->player, 0, audio_player_close_cb);
    if (ret < 0) {
        FEATURE_LOG_ERROR("player:%p close, ret:%d\n", obj->player, ret);
        goto error;
    }

    ret = media_uv_session_unregister(obj->session, audio_session_close_cb);
    if (ret < 0) {
        FEATURE_LOG_ERROR("session:%p unregister, ret:%d\n", obj->session, ret);
        goto error;
    }

    return;
error:
    if (FeatureCheckCallbackId(obj->event.onerror.feature, obj->event.onerror.callbackId))
        FeatureInvokeCallback(obj->event.onerror.feature, obj->event.onerror.callbackId);
}

static const char* get_state_string(PlayerState state)
{
    if (state == MEDIA_STATE_STARTED)
        return "play";
    else if (state == MEDIA_STATE_PAUSED)
        return "pause";
    else
        return "stop";
}

static void audio_try_free(AudioObject* obj)
{
    if (!obj->player && !obj->session)
        free(obj);
}

static void timeupdate_timer_cb(uv_timer_t* handle)
{
    AudioObject* obj;

    if (!handle)
        return;

    obj = (AudioObject*)handle->data;
    if (!obj)
        return;

    if (obj->state == MEDIA_STATE_STARTED)
        media_uv_player_query(obj->player, audio_media_player_query_cb, obj);
}

static void update_duration(AudioObject* obj)
{
    FEATURE_LOG_INFO("%s::%s(),\n", file_tag, __FUNCTION__);
    if (!obj || !obj->player)
        return;

    media_uv_player_get_duration(obj->player, audio_get_duration_cb, obj);
}

static void audio_session_event_callback(void* cookie, int event, int ret, const char* extra)
{
    FEATURE_LOG_INFO("session_event_cb cookie:%p event:%s(%d) ret:%d extra:%s\n",
        cookie, media_event_get_name(event), event, ret, extra);

    AudioObject* obj;

    obj = (AudioObject*)cookie;
    if (!obj) {
        FEATURE_LOG_ERROR("%s::%s() fail, obj is null.\n", file_tag, __FUNCTION__);
        return;
    }

    /* Received control event from session, call user's operation callback. */
    switch (event) {
    case MEDIA_EVENT_START:
        media_uv_player_start_auto(obj->player, MEDIA_SCENARIO_MUSIC, audio_start_cb, obj);
        break;

    case MEDIA_EVENT_PAUSE:
        media_uv_player_pause(obj->player, NULL, NULL);
        break;

    case MEDIA_EVENT_STOP:
        media_uv_player_stop(obj->player, NULL, NULL);
        break;

    case MEDIA_EVENT_PREV_SONG:
        if (FeatureCheckCallbackId(obj->event.onctrlplayprev.feature, obj->event.onctrlplayprev.callbackId))
            FeatureInvokeCallback(obj->event.onctrlplayprev.feature, obj->event.onctrlplayprev.callbackId);
        break;

    case MEDIA_EVENT_NEXT_SONG:
        if (FeatureCheckCallbackId(obj->event.onctrlplaynext.feature, obj->event.onctrlplaynext.callbackId))
            FeatureInvokeCallback(obj->event.onctrlplaynext.feature, obj->event.onctrlplaynext.callbackId);
        break;

    default:
        break;
    }
}

static void audio_player_event_callback(void* cookie, int event, int ret, const char* extra)
{
    FEATURE_LOG_INFO("player_event_cb cookie:%p event:%s(%d) ret:%d extra:%s\n",
        cookie, media_event_get_name(event), event, ret, extra);

    media_metadata_t data = { 0 };
    AudioObject* obj;

    obj = (AudioObject*)cookie;
    if (!obj) {
        FEATURE_LOG_ERROR("%s::%s() fail, obj is null.\n", file_tag, __FUNCTION__);
        return;
    }

    if (ret < 0) {
        FEATURE_LOG_ERROR("%s::%s() fail, ret < 0.\n", file_tag, __FUNCTION__);
        media_uv_player_stop(obj->player, 0, NULL);
        goto error;
    }

    switch (event) {
    case MEDIA_EVENT_PREPARED:
        if (FeatureCheckCallbackId(obj->event.onloadeddata.feature, obj->event.onloadeddata.callbackId))
            FeatureInvokeCallback(obj->event.onloadeddata.feature, obj->event.onloadeddata.callbackId);

        data.flags = MEDIA_METAFLAG_TITLE | MEDIA_METAFLAG_ARTIST | MEDIA_METAFLAG_ALBUM;
        data.title = obj->meta.title;
        data.artist = obj->meta.artist;
        data.album = obj->meta.album;
        break;

    case MEDIA_EVENT_STARTED:
        obj->state = MEDIA_STATE_STARTED;
        if (FeatureCheckCallbackId(obj->event.onplay.feature, obj->event.onplay.callbackId))
            FeatureInvokeCallback(obj->event.onplay.feature, obj->event.onplay.callbackId);
        update_duration(obj);
        uv_timer_start(&obj->timer, timeupdate_timer_cb, 0, AUDIO_TIMEUPDATE_TIMEOUT);
        data.flags = MEDIA_METAFLAG_STATE;
        data.state = 1;
        break;

    case MEDIA_EVENT_PAUSED:
        obj->state = MEDIA_STATE_PAUSED;
        uv_timer_stop(&obj->timer);
        if (FeatureCheckCallbackId(obj->event.onpause.feature, obj->event.onpause.callbackId))
            FeatureInvokeCallback(obj->event.onpause.feature, obj->event.onpause.callbackId);
        data.flags = MEDIA_METAFLAG_STATE;
        data.state = 0;
        break;

    case MEDIA_EVENT_STOPPED:
        obj->state = MEDIA_STATE_STOPPED;
        uv_timer_stop(&obj->timer);
        if (FeatureCheckCallbackId(obj->event.onstop.feature, obj->event.onstop.callbackId))
            FeatureInvokeCallback(obj->event.onstop.feature, obj->event.onstop.callbackId);
        data.flags = MEDIA_METAFLAG_STATE;
        data.state = 0;
        break;

    case MEDIA_EVENT_COMPLETED:
        if (FeatureCheckCallbackId(obj->event.onended.feature, obj->event.onended.callbackId))
            FeatureInvokeCallback(obj->event.onended.feature, obj->event.onended.callbackId);
        break;

    default:
        break;
    }

    ret = media_uv_session_update(obj->session, &data, NULL, NULL);
    if (ret < 0) {
        FEATURE_LOG_ERROR("%s::%s()media session update fail, ret:%d.\n", file_tag, __FUNCTION__, ret);
        goto error;
    }

    return;
error:
    if (FeatureCheckCallbackId(obj->event.onerror.feature, obj->event.onerror.callbackId))
        FeatureInvokeCallback(obj->event.onerror.feature, obj->event.onerror.callbackId);
}

static bool audio_check_playerstate(AudioObject* obj, FeatureInstanceHandle feature)
{
    if (!obj || !obj->player) {
        FEATURE_LOG_ERROR("Invalid AudioObject or player pointer.");
        return false;
    }

    if (obj->state == MEDIA_STATE_NONE) {
        FEATURE_LOG_ERROR("player:%p state is none.", obj->player);
        return false;
    }

    FEATURE_LOG_INFO("player:%p state is %d.", obj->player, obj->state);
    return true;
}

/* uv interface cb function */
static void audio_open_cb(void* cookie, int ret)
{
    FEATURE_LOG_INFO("%s::%s(), ret: %d\n", file_tag, __FUNCTION__, ret);

    AudioObject* obj = (AudioObject*)cookie;
    if (!obj) {
        FEATURE_LOG_ERROR("AudioObject is null.");
        return;
    }

    if (ret < 0) {
        obj->state = MEDIA_STATE_NONE;
        FEATURE_LOG_ERROR("player:%p open failed, ret:%d\n", obj->player, ret);
        if (FeatureCheckCallbackId(obj->event.onerror.feature, obj->event.onerror.callbackId))
            FeatureInvokeCallback(obj->event.onerror.feature, obj->event.onerror.callbackId);
        if (obj->player) {
            media_uv_player_close(obj->player, 0, NULL);
            obj->player = NULL;
        }
        if (obj->session) {
            media_uv_session_unregister(obj->session, NULL);
            obj->session = NULL;
        }

        return;
    }

    return;
}

static void audio_timer_close_cb(uv_handle_t* handle)
{
    audio_close((AudioObject*)handle->data);
}

static void audio_session_close_cb(void* cookie, int ret)
{
    FEATURE_LOG_INFO("session:%p closed", cookie);
    AudioObject* obj;

    obj = (AudioObject*)cookie;
    obj->session = NULL;
    audio_try_free(obj);
}

static void audio_player_close_cb(void* cookie, int ret)
{
    FEATURE_LOG_INFO("player:%p closed", cookie);
    AudioObject* obj;

    obj = (AudioObject*)cookie;
    obj->player = NULL;
    audio_try_free(obj);
}

static void audio_start_cb(void* cookie, int ret)
{
    FEATURE_LOG_INFO("%s::%s(), ret: %d\n", file_tag, __FUNCTION__, ret);
    AudioObject* obj;

    obj = (AudioObject*)cookie;
    if (ret < 0) {
        if (obj->state == MEDIA_STATE_PREPARED)
            FEATURE_LOG_ERROR("player:%p prepare ready, but start failed, ret:%d\n", obj->player, ret);
        else
            FEATURE_LOG_ERROR("player:%p resume start failed, ret:%d\n", obj->player, ret);

        if (FeatureCheckCallbackId(obj->event.onerror.feature, obj->event.onerror.callbackId))
            FeatureInvokeCallback(obj->event.onerror.feature, obj->event.onerror.callbackId);
    }
}

static void audio_media_player_query_cb(void* cookie, int ret, void* object)
{
    const media_metadata_t* cdata = (const media_metadata_t*)object;
    media_metadata_t data = { 0 };
    AudioObject* obj;

    obj = (AudioObject*)cookie;

    if (!obj)
        return;

    FEATURE_LOG_INFO("%s::%s(),volume:%d position:%u duration:%d\n", file_tag, __FUNCTION__, cdata->volume, cdata->position, cdata->duration);

    if (ret >= 0 && obj->state == MEDIA_STATE_STARTED) {
        obj->currentTime = cdata->position / 1000;
        obj->duration = cdata->duration / 1000;
        obj->percent = (obj->currentTime * 100.0) / obj->duration;
        obj->volume = (double)cdata->volume / 10.0;
    }

    if (FeatureCheckCallbackId(obj->event.ontimeupdate.feature, obj->event.ontimeupdate.callbackId))
        FeatureInvokeCallback(obj->event.ontimeupdate.feature, obj->event.ontimeupdate.callbackId);

    data.flags = MEDIA_METAFLAG_VOLUME | MEDIA_METAFLAG_POSITION | MEDIA_METAFLAG_DURATION;
    data.volume = cdata->volume;
    data.position = cdata->position;
    data.duration = cdata->duration;
    if (media_uv_session_update(obj->session, &data, NULL, NULL) < 0) {
        FEATURE_LOG_ERROR("%s::%s()media session update fail, ret:%d.\n", file_tag, __FUNCTION__, ret);
        if (FeatureCheckCallbackId(obj->event.onerror.feature, obj->event.onerror.callbackId))
            FeatureInvokeCallback(obj->event.onerror.feature, obj->event.onerror.callbackId);
    }
}

static void audio_get_duration_cb(void* cookie, int ret, unsigned duration)
{
    FEATURE_LOG_INFO("%s::%s(),ret:%d, duration:%d\n", file_tag, __FUNCTION__, ret, duration);

    media_metadata_t data = { 0 };
    AudioObject* obj;

    obj = (AudioObject*)cookie;
    if (!obj)
        return;

    if (ret >= 0)
        obj->duration = duration / 1000;

    if (FeatureCheckCallbackId(obj->event.ondurationchange.feature, obj->event.ondurationchange.callbackId))
        FeatureInvokeCallback(obj->event.ondurationchange.feature, obj->event.ondurationchange.callbackId);

    data.flags = MEDIA_METAFLAG_DURATION;
    data.duration = duration;
    if (media_uv_session_update(obj->session, &data, NULL, NULL) < 0) {
        FEATURE_LOG_ERROR("%s::%s()media session update fail, ret:%d.\n", file_tag, __FUNCTION__, ret);
        if (FeatureCheckCallbackId(obj->event.onerror.feature, obj->event.onerror.callbackId))
            FeatureInvokeCallback(obj->event.onerror.feature, obj->event.onerror.callbackId);
    }
}

/* warp function */
void system_audio_wrap_play(FeatureInstanceHandle feature, union AppendData append_data)
{
    FEATURE_LOG_INFO("%s::%s(),\n", file_tag, __FUNCTION__);

    AudioObject* obj;
    int ret;

    obj = (AudioObject*)FeatureGetProtoData(FeatureGetProtoHandle(feature));
    if (!obj || !obj->player)
        return;

    if (!audio_check_playerstate(obj, feature))
        return;

    if (obj->state < MEDIA_STATE_PREPARED) {
        if (!obj->src[0]) {
            FEATURE_LOG_ERROR("player:%p audio info src is NULL.\n", obj->player);
            goto error;
        }

        ret = media_uv_player_prepare(obj->player, obj->src, NULL, NULL, NULL, NULL);
        FEATURE_LOG_INFO("player:%p prepare, ret:%d", obj->player, ret);
        if (ret < 0)
            goto error;
        obj->state = MEDIA_STATE_PREPARED;
        /* for the scenario where the user seek before playback */
        if (obj->currentTime)
            media_uv_player_seek(obj->player, obj->currentTime * 1000, NULL, NULL);
    }

    ret = media_uv_player_start_auto(obj->player, MEDIA_SCENARIO_MUSIC, audio_start_cb, obj);
    FEATURE_LOG_INFO("player:%p start, ret:%d", obj->player, ret);
    if (ret < 0)
        goto error;
    obj->state = MEDIA_STATE_STARTED;
    return;
error:
    if (FeatureCheckCallbackId(obj->event.onerror.feature, obj->event.onerror.callbackId))
        FeatureInvokeCallback(obj->event.onerror.feature, obj->event.onerror.callbackId);
    return;
}

void system_audio_wrap_pause(FeatureInstanceHandle feature, union AppendData append_data)
{
    FEATURE_LOG_INFO("%s::%s(),\n", file_tag, __FUNCTION__);

    AudioObject* obj;
    int ret;

    obj = (AudioObject*)FeatureGetProtoData(FeatureGetProtoHandle(feature));
    if (!obj || !obj->player)
        return;

    if (!audio_check_playerstate(obj, feature))
        return;

    if (obj->state != MEDIA_STATE_STARTED) {
        FEATURE_LOG_WARN("player:%p not in started state, cannot pause", obj->player);
        return;
    }

    ret = media_uv_player_pause(obj->player, NULL, NULL);
    FEATURE_LOG_INFO("player:%p pause, ret:%d", obj->player, ret);
    if (ret < 0)
        goto error;

    obj->state = MEDIA_STATE_PAUSED;
    return;
error:
    if (FeatureCheckCallbackId(obj->event.onerror.feature, obj->event.onerror.callbackId))
        FeatureInvokeCallback(obj->event.onerror.feature, obj->event.onerror.callbackId);
    return;
}

void system_audio_wrap_stop(FeatureInstanceHandle feature, union AppendData append_data)
{
    FEATURE_LOG_INFO("%s::%s(),\n", file_tag, __FUNCTION__);
    AudioObject* obj;
    int ret;

    obj = (AudioObject*)FeatureGetProtoData(FeatureGetProtoHandle(feature));
    if (!obj || !obj->player)
        return;

    if (!audio_check_playerstate(obj, feature))
        return;

    if (obj->state == MEDIA_STATE_STOPPED) {
        FEATURE_LOG_WARN("player:%p already stopped, no action needed", obj->player);
        audio_reset_obj(obj);
        return;
    }

    ret = media_uv_player_stop(obj->player, 0, NULL);
    FEATURE_LOG_INFO("player:%p stop, ret:%d", obj->player, ret);
    if (ret < 0)
        goto error;

    obj->state = MEDIA_STATE_STOPPED;
    audio_reset_obj(obj);
    return;
error:
    if (FeatureCheckCallbackId(obj->event.onerror.feature, obj->event.onerror.callbackId))
        FeatureInvokeCallback(obj->event.onerror.feature, obj->event.onerror.callbackId);
    return;
}

void system_audio_wrap_getPlayState(FeatureInstanceHandle feature, union AppendData append_data, system_audio_GetPalyStateParam* p)
{
    FEATURE_LOG_DEBUG("%s::%s(),\n", file_tag, __FUNCTION__);
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
    audiostate->muted = obj->volume < EPSILON;
    audiostate->duration = obj->duration;
    audiostate->percent = obj->percent;

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

    if (obj->state > MEDIA_STATE_PREPARED) {
        FEATURE_LOG_WARN("player:%p already prepared, cannot set src.", obj->player);
        return;
    }

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
        const char* vaild_types[] = { "cache", "file", "mass", "tmp", "files" };
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
        strlcpy(obj->src, src, sizeof(obj->src));

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
        strlcpy(obj->meta.title, meta->title, MAX_TITLE_LEN);
    if (meta->album != NULL)
        strlcpy(obj->meta.album, meta->album, MAX_ALBUM_LEN);
    if (meta->artist != NULL)
        strlcpy(obj->meta.artist, meta->artist, MAX_ARTIST_LEN);
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
    if (!obj || !obj->player)
        return;

    media_uv_player_seek(obj->player, currentTime * 1000, NULL, NULL);
    obj->currentTime = currentTime;
}

FtFloat system_audio_get_percent(void* feature, union AppendData append_data)
{
    FEATURE_LOG_INFO("%s::%s(),\n", file_tag, __FUNCTION__);
    AudioObject* obj;

    obj = (AudioObject*)FeatureGetProtoData(FeatureGetProtoHandle(feature));
    if (!obj)
        return 0;

    return obj->percent;
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
    if (!obj || !obj->player)
        return;

    media_uv_player_set_looping(obj->player, loop ? -1 : 0, NULL, NULL);
    obj->loop = loop;
}

double system_audio_get_volume(void* feature, union AppendData append_data)
{
    AudioObject* obj;

    obj = (AudioObject*)FeatureGetProtoData(FeatureGetProtoHandle(feature));
    if (!obj)
        return 0;

    FEATURE_LOG_INFO("%s::%s(), obj->volume: %f\n", file_tag, __FUNCTION__, obj->volume);
    return obj->volume;
}

void system_audio_set_volume(void* feature, union AppendData append_data, double volume)
{
    FEATURE_LOG_INFO("%s::%s(), volume:%f\n", file_tag, __FUNCTION__, volume);
    FeatureManagerHandle manager;
    AudioObject* obj;
    uv_loop_t* loop;

    obj = (AudioObject*)FeatureGetProtoData(FeatureGetProtoHandle(feature));
    if (!obj || !obj->player)
        return;

    if (volume < 0)
        volume = 0;
    else if (volume > 1)
        volume = 1;

    manager = FeatureGetManagerHandleFromProto(obj->proto);
    loop = FeatureGetUVLoop(manager);
    media_uv_policy_set_stream_volume(loop, obj->streamType, (int)(volume * 10), NULL, NULL);
    obj->volume = volume;
}

FtBool system_audio_get_muted(void* feature, union AppendData append_data)
{
    FEATURE_LOG_INFO("%s::%s(),\n", file_tag, __FUNCTION__);
    return system_audio_get_volume(feature, append_data) == 0;
}

void system_audio_set_muted(void* feature, union AppendData append_data, FtBool muted)
{
    FEATURE_LOG_INFO("%s::%s(), muted: %d\n", file_tag, __FUNCTION__, muted);

    AudioObject* obj;

    obj = (AudioObject*)FeatureGetProtoData(FeatureGetProtoHandle(feature));
    if (!obj || !obj->player)
        return;

    if (muted) {
        if (obj->volume > 0)
            obj->mutedvolume = obj->volume;

        system_audio_set_volume(feature, append_data, 0);
        return;
    }

    if (obj->mutedvolume > 0)
        system_audio_set_volume(feature, append_data, obj->mutedvolume);
}

FtString system_audio_get_streamType(void* feature, union AppendData append_data)
{
    FEATURE_LOG_INFO("%s::%s(),\n", file_tag, __FUNCTION__);
    FtString src;

    src = (FtString)FeatureMalloc(6, FT_CHAR);
    if (src == NULL) {
        FEATURE_LOG_ERROR("FeatureMalloc failed!\n");
        return NULL;
    }

    snprintf((char*)src, 6, "music");

    return src;
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

void system_audio_set_onctrlplayprev(void* feature, AppendData append_data, FtCallbackId onctrlplayprev)
{
    FEATURE_LOG_DEBUG("%s::%s(),\n", file_tag, __FUNCTION__);
    AudioObject* obj;

    obj = (AudioObject*)FeatureGetProtoData(FeatureGetProtoHandle(feature));
    if (!obj)
        return;

    obj->event.onctrlplayprev.callbackId = onctrlplayprev;
    obj->event.onctrlplayprev.feature = feature;
}

void system_audio_set_onctrlplaynext(void* feature, AppendData append_data, FtCallbackId onctrlplaynext)
{
    FEATURE_LOG_DEBUG("%s::%s(),\n", file_tag, __FUNCTION__);
    AudioObject* obj;

    obj = (AudioObject*)FeatureGetProtoData(FeatureGetProtoHandle(feature));
    if (!obj)
        return;

    obj->event.onctrlplaynext.callbackId = onctrlplaynext;
    obj->event.onctrlplaynext.feature = feature;
}