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

#ifndef JSON_AST_GEN_MODULE_SYSTEM_VOLUME_H_
#define JSON_AST_GEN_MODULE_SYSTEM_VOLUME_H_

#include "feature_exports.h"
#include "feature_log.h"

#include <ffi.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// FeatureCallbacks to be implemented
void system_volume_onRegister(const char* feature_name);
void system_volume_onCreate(FeatureRuntimeContext ctx, FeatureProtoHandle handle);
void system_volume_onRequired(FeatureRuntimeContext ctx, FeatureInstanceHandle handle);
void system_volume_onDetached(FeatureRuntimeContext ctx, FeatureInstanceHandle handle);
void system_volume_onDestroy(FeatureRuntimeContext ctx, FeatureProtoHandle handle);
void system_volume_onUnregister(const char* feature_name);

// Struct defines
typedef struct _GetRet {
  FtDouble value;
} system_volume_GetRet;

system_volume_GetRet* system_volumeMallocGetRet(void);

typedef struct _GetInfo {
  FtCallbackId success;
  FtCallbackId fail;
  FtCallbackId complete;
} system_volume_GetInfo;

system_volume_GetInfo* system_volumeMallocGetInfo(void);

typedef struct _SetInfo {
  FtDouble value;
  FtCallbackId success;
  FtCallbackId fail;
  FtCallbackId complete;
} system_volume_SetInfo;

system_volume_SetInfo* system_volumeMallocSetInfo(void);


// Function wrappers to be implemented
void system_volume_wrap_setMediaValue(FeatureInstanceHandle feature, union AppendData append_data, system_volume_SetInfo * info);
void system_volume_wrap_getMediaValue(FeatureInstanceHandle feature, union AppendData append_data, system_volume_GetInfo * info);

// Interface constructors

// interface vtable functions to be implemented

// Property getters and setters to be implemented

// Array malloc functions

#endif // JSON_AST_GEN_MODULE_SYSTEM_VOLUME_H_
/* clang-format on */
