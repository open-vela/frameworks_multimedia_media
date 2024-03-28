/*
 * This file is auto-generated by jsongensource.py, Do not modify it directly!
 */

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

/* clang-format off */

#include "audio.h"
#include "ajs_features_init.h"
#include "feature_description.h"

#define countof(x) (sizeof(x) / sizeof(x[0]))

/****** for JIDL callback 'on_void' ******/
static const FeatureType system_audio_on_void_parameters[] = {
    FT_PARAM_END
};

static const CallbackType system_audio_on_void_callback_type = {
    .header = { .type = COMPLEX_CALLBACK, .size = sizeof(FtCallbackId) },
    .parameters = system_audio_on_void_parameters,
    .return_type = FT_VOID
};


/****** for JIDL callback 'on_fail' ******/
static const FeatureType system_audio_on_fail_parameters[] = {
    FT_STRING,
    FT_INT,
    FT_PARAM_END
};

static const CallbackType system_audio_on_fail_callback_type = {
    .header = { .type = COMPLEX_CALLBACK, .size = sizeof(FtCallbackId) },
    .parameters = system_audio_on_fail_parameters,
    .return_type = FT_VOID
};


/****** for JIDL struct 'AudioState' ******/
static ObjectMember system_audio_AudioState_struct_members[] = {
    { "state", FT_STRING, offsetof(system_audio_AudioState, state), sizeof(FtString) },
    { "src", FT_STRING, offsetof(system_audio_AudioState, src), sizeof(FtString) },
    { "currentTime", FT_FLOAT, offsetof(system_audio_AudioState, currentTime), sizeof(FtFloat) },
    { "autoplay", FT_BOOLEAN, offsetof(system_audio_AudioState, autoplay), sizeof(FtBool) },
    { "loop", FT_BOOLEAN, offsetof(system_audio_AudioState, loop), sizeof(FtBool) },
    { "volume", FT_FLOAT, offsetof(system_audio_AudioState, volume), sizeof(FtFloat) },
    { "mute", FT_BOOLEAN, offsetof(system_audio_AudioState, mute), sizeof(FtBool) },
    { "duration", FT_FLOAT, offsetof(system_audio_AudioState, duration), sizeof(FtFloat) },
    { NULL },
};

// complex defination
static const ObjectMapType system_audio_AudioState_struct_type = {
    .header = { .type = COMPLEX_STRUCT_MAP, .size = sizeof(system_audio_AudioState) },
    .members = system_audio_AudioState_struct_members
};

system_audio_AudioState* system_audioMallocAudioState () {
    return (system_audio_AudioState*)FeatureMalloc(
        sizeof(system_audio_AudioState), FT_MK_COMPLEX(&system_audio_AudioState_struct_type));
}


/****** for JIDL callback 'on_get_state' ******/
static const FeatureType system_audio_on_get_state_parameters[] = {
    FT_MK_COMPLEX(&system_audio_AudioState_struct_type),
    FT_PARAM_END
};

static const CallbackType system_audio_on_get_state_callback_type = {
    .header = { .type = COMPLEX_CALLBACK, .size = sizeof(FtCallbackId) },
    .parameters = system_audio_on_get_state_parameters,
    .return_type = FT_VOID
};


/****** for JIDL struct 'MetaInfo' ******/
static ObjectMember system_audio_MetaInfo_struct_members[] = {
    { "title", FT_STRING, offsetof(system_audio_MetaInfo, title), sizeof(FtString) },
    { "album", FT_STRING, offsetof(system_audio_MetaInfo, album), sizeof(FtString) },
    { "artist", FT_STRING, offsetof(system_audio_MetaInfo, artist), sizeof(FtString) },
    { NULL },
};

// complex defination
static const ObjectMapType system_audio_MetaInfo_struct_type = {
    .header = { .type = COMPLEX_STRUCT_MAP, .size = sizeof(system_audio_MetaInfo) },
    .members = system_audio_MetaInfo_struct_members
};

system_audio_MetaInfo* system_audioMallocMetaInfo () {
    return (system_audio_MetaInfo*)FeatureMalloc(
        sizeof(system_audio_MetaInfo), FT_MK_COMPLEX(&system_audio_MetaInfo_struct_type));
}


/****** for JIDL struct 'GetPalyStateParam' ******/
static ObjectMember system_audio_GetPalyStateParam_struct_members[] = {
    { "success", FT_MK_COMPLEX(&system_audio_on_get_state_callback_type), offsetof(system_audio_GetPalyStateParam, success), sizeof(FtCallbackId) },
    { "fail", FT_MK_COMPLEX(&system_audio_on_fail_callback_type), offsetof(system_audio_GetPalyStateParam, fail), sizeof(FtCallbackId) },
    { "complete", FT_MK_COMPLEX(&system_audio_on_void_callback_type), offsetof(system_audio_GetPalyStateParam, complete), sizeof(FtCallbackId) },
    { NULL },
};

// complex defination
static const ObjectMapType system_audio_GetPalyStateParam_struct_type = {
    .header = { .type = COMPLEX_STRUCT_MAP, .size = sizeof(system_audio_GetPalyStateParam) },
    .members = system_audio_GetPalyStateParam_struct_members
};

system_audio_GetPalyStateParam* system_audioMallocGetPalyStateParam () {
    return (system_audio_GetPalyStateParam*)FeatureMalloc(
        sizeof(system_audio_GetPalyStateParam), FT_MK_COMPLEX(&system_audio_GetPalyStateParam_struct_type));
}


/****** for JIDL callback 'on_event' ******/
static const FeatureType system_audio_on_event_parameters[] = {
    FT_PARAM_END
};

static const CallbackType system_audio_on_event_callback_type = {
    .header = { .type = COMPLEX_CALLBACK, .size = sizeof(FtCallbackId) },
    .parameters = system_audio_on_event_parameters,
    .return_type = FT_VOID
};


/****** for JIDL property 'src' ******/
static const MemberAccessor system_audio_src_member_accessor = {
    .getter = { .callback = FFI_FN(system_audio_get_src) },
    .setter = { .callback = FFI_FN(system_audio_set_src) },
    .type = FT_STRING,
};


/****** for JIDL property 'meta' ******/
static const MemberAccessor system_audio_meta_member_accessor = {
    .setter = { .callback = FFI_FN(system_audio_set_meta) },
    .type = FT_MK_COMPLEX(&system_audio_MetaInfo_struct_type),
};


/****** for JIDL property 'currentTime' ******/
static const MemberAccessor system_audio_currentTime_member_accessor = {
    .getter = { .callback = FFI_FN(system_audio_get_currentTime) },
    .setter = { .callback = FFI_FN(system_audio_set_currentTime) },
    .type = FT_FLOAT,
};


/****** for JIDL property 'duration' ******/
static const MemberAccessor system_audio_duration_member_accessor = {
    .getter = { .callback = FFI_FN(system_audio_get_duration) },
    .type = FT_FLOAT,
};


/****** for JIDL property 'autoplay' ******/
static const MemberAccessor system_audio_autoplay_member_accessor = {
    .getter = { .callback = FFI_FN(system_audio_get_autoplay) },
    .setter = { .callback = FFI_FN(system_audio_set_autoplay) },
    .type = FT_BOOLEAN,
};


/****** for JIDL property 'loop' ******/
static const MemberAccessor system_audio_loop_member_accessor = {
    .getter = { .callback = FFI_FN(system_audio_get_loop) },
    .setter = { .callback = FFI_FN(system_audio_set_loop) },
    .type = FT_BOOLEAN,
};


/****** for JIDL property 'volume' ******/
static const MemberAccessor system_audio_volume_member_accessor = {
    .getter = { .callback = FFI_FN(system_audio_get_volume) },
    .setter = { .callback = FFI_FN(system_audio_set_volume) },
    .type = FT_FLOAT,
};


/****** for JIDL property 'muted' ******/
static const MemberAccessor system_audio_muted_member_accessor = {
    .getter = { .callback = FFI_FN(system_audio_get_muted) },
    .setter = { .callback = FFI_FN(system_audio_set_muted) },
    .type = FT_BOOLEAN,
};


/****** for JIDL property 'streamType' ******/
static const MemberAccessor system_audio_streamType_member_accessor = {
    .getter = { .callback = FFI_FN(system_audio_get_streamType) },
    .type = FT_STRING,
};


/****** for JIDL function 'play' ******/
static const FeatureType system_audio_play_parameters[] = {
    FT_PARAM_END
};

static const MemberMethod system_audio_play_member_method = {
    .func = { .callback = FFI_FN(system_audio_wrap_play) },
    .parameters = system_audio_play_parameters,
    .return_type = FT_VOID,
};


/****** for JIDL function 'pause' ******/
static const FeatureType system_audio_pause_parameters[] = {
    FT_PARAM_END
};

static const MemberMethod system_audio_pause_member_method = {
    .func = { .callback = FFI_FN(system_audio_wrap_pause) },
    .parameters = system_audio_pause_parameters,
    .return_type = FT_VOID,
};


/****** for JIDL function 'stop' ******/
static const FeatureType system_audio_stop_parameters[] = {
    FT_PARAM_END
};

static const MemberMethod system_audio_stop_member_method = {
    .func = { .callback = FFI_FN(system_audio_wrap_stop) },
    .parameters = system_audio_stop_parameters,
    .return_type = FT_VOID,
};


/****** for JIDL function 'getPlayState' ******/
static const FeatureType system_audio_getPlayState_parameters[] = {
    FT_MK_COMPLEX(&system_audio_GetPalyStateParam_struct_type),
    FT_PARAM_END
};

static const MemberMethod system_audio_getPlayState_member_method = {
    .func = { .callback = FFI_FN(system_audio_wrap_getPlayState) },
    .parameters = system_audio_getPlayState_parameters,
    .return_type = FT_VOID,
};


/****** for JIDL property 'onplay' ******/
static const MemberAccessor system_audio_onplay_member_accessor = {
    .setter = { .callback = FFI_FN(system_audio_set_onplay) },
    .type = FT_MK_COMPLEX(&system_audio_on_event_callback_type),
};


/****** for JIDL property 'onpause' ******/
static const MemberAccessor system_audio_onpause_member_accessor = {
    .setter = { .callback = FFI_FN(system_audio_set_onpause) },
    .type = FT_MK_COMPLEX(&system_audio_on_event_callback_type),
};


/****** for JIDL property 'onstop' ******/
static const MemberAccessor system_audio_onstop_member_accessor = {
    .setter = { .callback = FFI_FN(system_audio_set_onstop) },
    .type = FT_MK_COMPLEX(&system_audio_on_event_callback_type),
};


/****** for JIDL property 'onloadeddata' ******/
static const MemberAccessor system_audio_onloadeddata_member_accessor = {
    .setter = { .callback = FFI_FN(system_audio_set_onloadeddata) },
    .type = FT_MK_COMPLEX(&system_audio_on_event_callback_type),
};


/****** for JIDL property 'onended' ******/
static const MemberAccessor system_audio_onended_member_accessor = {
    .setter = { .callback = FFI_FN(system_audio_set_onended) },
    .type = FT_MK_COMPLEX(&system_audio_on_event_callback_type),
};


/****** for JIDL property 'ondurationchange' ******/
static const MemberAccessor system_audio_ondurationchange_member_accessor = {
    .setter = { .callback = FFI_FN(system_audio_set_ondurationchange) },
    .type = FT_MK_COMPLEX(&system_audio_on_event_callback_type),
};


/****** for JIDL property 'ontimeupdate' ******/
static const MemberAccessor system_audio_ontimeupdate_member_accessor = {
    .setter = { .callback = FFI_FN(system_audio_set_ontimeupdate) },
    .type = FT_MK_COMPLEX(&system_audio_on_event_callback_type),
};


/****** for JIDL property 'onerror' ******/
static const MemberAccessor system_audio_onerror_member_accessor = {
    .setter = { .callback = FFI_FN(system_audio_set_onerror) },
    .type = FT_MK_COMPLEX(&system_audio_on_event_callback_type),
};


// members
static const Member system_audio_members[] = {
    {
        .type = MEMBER_ACCESSOR,
        .name = "src",
        .accessor = &system_audio_src_member_accessor,
    },
    {
        .type = MEMBER_ACCESSOR,
        .name = "meta",
        .accessor = &system_audio_meta_member_accessor,
    },
    {
        .type = MEMBER_ACCESSOR,
        .name = "currentTime",
        .accessor = &system_audio_currentTime_member_accessor,
    },
    {
        .type = MEMBER_ACCESSOR,
        .name = "duration",
        .accessor = &system_audio_duration_member_accessor,
    },
    {
        .type = MEMBER_ACCESSOR,
        .name = "autoplay",
        .accessor = &system_audio_autoplay_member_accessor,
    },
    {
        .type = MEMBER_ACCESSOR,
        .name = "loop",
        .accessor = &system_audio_loop_member_accessor,
    },
    {
        .type = MEMBER_ACCESSOR,
        .name = "volume",
        .accessor = &system_audio_volume_member_accessor,
    },
    {
        .type = MEMBER_ACCESSOR,
        .name = "muted",
        .accessor = &system_audio_muted_member_accessor,
    },
    {
        .type = MEMBER_ACCESSOR,
        .name = "streamType",
        .accessor = &system_audio_streamType_member_accessor,
    },
    {
        .type = MEMBER_METHOD,
        .name = "play",
        .method = &system_audio_play_member_method,
    },
    {
        .type = MEMBER_METHOD,
        .name = "pause",
        .method = &system_audio_pause_member_method,
    },
    {
        .type = MEMBER_METHOD,
        .name = "stop",
        .method = &system_audio_stop_member_method,
    },
    {
        .type = MEMBER_METHOD,
        .name = "getPlayState",
        .method = &system_audio_getPlayState_member_method,
    },
    {
        .type = MEMBER_ACCESSOR,
        .name = "onplay",
        .accessor = &system_audio_onplay_member_accessor,
    },
    {
        .type = MEMBER_ACCESSOR,
        .name = "onpause",
        .accessor = &system_audio_onpause_member_accessor,
    },
    {
        .type = MEMBER_ACCESSOR,
        .name = "onstop",
        .accessor = &system_audio_onstop_member_accessor,
    },
    {
        .type = MEMBER_ACCESSOR,
        .name = "onloadeddata",
        .accessor = &system_audio_onloadeddata_member_accessor,
    },
    {
        .type = MEMBER_ACCESSOR,
        .name = "onended",
        .accessor = &system_audio_onended_member_accessor,
    },
    {
        .type = MEMBER_ACCESSOR,
        .name = "ondurationchange",
        .accessor = &system_audio_ondurationchange_member_accessor,
    },
    {
        .type = MEMBER_ACCESSOR,
        .name = "ontimeupdate",
        .accessor = &system_audio_ontimeupdate_member_accessor,
    },
    {
        .type = MEMBER_ACCESSOR,
        .name = "onerror",
        .accessor = &system_audio_onerror_member_accessor,
    },
};

// callbacks
static const struct FeatureCallbacks system_audio_callbacks = {
    system_audio_onRegister,
    system_audio_onCreate,
    system_audio_onRequired,
    system_audio_onDetached,
    system_audio_onDestroy,
    system_audio_onUnregister
};

static const FeatureDescription system_audio_desc = {
    .version = 1,
    .name = "system.audio",
    .description = "system.audio",
    { .dynamic = false },
    .native_callbacks = &system_audio_callbacks,
    .member_count = countof(system_audio_members),
    .members = system_audio_members,
};

QAPPFEATURE_INIT(system_audio)
{
    return FeatureRegisterFeature(handle, &system_audio_desc);
}
/* clang-format on */