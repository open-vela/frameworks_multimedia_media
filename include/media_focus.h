/****************************************************************************
 * frameworks/media/include/media_focus.h
 *
 * Copyright (C) 2020 Xiaomi Corporation
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

#ifndef FRAMEWORKS_MEDIA_INCLUDE_MEDIA_FOCUS_H
#define FRAMEWORKS_MEDIA_INCLUDE_MEDIA_FOCUS_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <media_defs.h>
#include <stddef.h>

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C" {
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/**
 * @deprecated This function is deprecated and will be removed in future versions.
 * Use media_focus_request2() instead.
 *
 * @brief Request Audio Focus.
 *
 * @param[out] initial_suggestion   MEDIA_FOCUS_* .
 * @param[in] scenario              MEDIA_SCENARIO_* .
 * @param[out] on_suggestion        Callback to receive suggestions.
 * @param[in] cookie                Callback argument.
 * @return Handle, NULL on failure.
 *
 * @note If `initial_suggestion` is `MEDIA_FOCUS_STOP`, `on_suggestion`
 * won't be called, but this api still return a handle, which means caller
 * need to call `media_focus_abandon`, or there will be leak.
 *
 * @code
 *  int initial_suggestion;
 *
 *  context->handle = media_focus_request(&initial_suggestion,
 *      MEDIA_SCENARIO_MUSIC, demo_focu_callback, context);
 *  if (!context->handle)
 *      ; // handle error
 *
 *  if (initial_suggestion == MEDIA_FOCUS_STOP)
 *      media_focus_abandon(context->handle);
 * @endcode
 *
 */
void* media_focus_request(int* initial_suggestion, const char* scenario,
    media_focus_callback on_suggestion, void* cookie);

/**
 * @brief Request Audio Focus.
 *
 * @param[out] initial_suggestion   MEDIA_FOCUS_* .
 * @param[in] scenario              MEDIA_SCENARIO_* .
 * @param[out] on_suggestion        Callback to receive suggestions.
 * @param[in] auto_reply            Auto reply or not.
 * @param[in] cookie                Callback argument.
 * @return Handle, NULL on failure.
 *
 * @note If `initial_suggestion` is `MEDIA_FOCUS_STOP`, `on_suggestion`
 * won't be called, but this api still return a handle, which means caller
 * need to call `media_focus_abandon`, or there will be leak.
 *
 * @code
 *  int initial_suggestion;
 *
 *  context->handle = media_focus_request2(&initial_suggestion,
 *      MEDIA_SCENARIO_MUSIC, demo_focu_callback, 1, context);
 *  if (!context->handle)
 *      ; // handle error
 *
 *  if (initial_suggestion == MEDIA_FOCUS_STOP)
 *      media_focus_abandon(context->handle);
 * @endcode
 *
 */
void* media_focus_request2(int* initial_suggestion, const char* scenario,
    media_focus_callback2 on_suggestion, int auto_reply, void* cookie);

/**
 * @brief Abandon audio focus.
 *
 * @param[in] handle    Focus handle to abandon.
 * @return Zero when request succeed, negative on failure.
 */
int media_focus_abandon(void* handle);

/**
 * @brief Reply audio focus.
 *
 * @param[in] handle        Focus handle to reply.
 * @param[in] req_id        Request id.
 * @return int  Zero on success, negative errno on failure.
 */
int media_focus_reply(void* handle, int req_id);

/**
 * @brief Dump focus stack.
 *
 * @param[in] options   Dump options(unused so far).
 *
 * @deprecated would merge into `media_dump()`.
 */
void media_focus_dump(const char* options);

#ifdef CONFIG_LIBUV
/**
 * @deprecated This function is deprecated and will be removed in future versions.
 * Use media_uv_focus_request2() instead.
 *
 * @brief Request audio focus.
 *
 * @param[in] loop              Handle uv_loop_t* of current thread.
 * @param[in] scenario          MEDIA_SCENARIO_*.
 * @param[out] on_suggestion    Callback to get focus suggestion.
 * @param[in] cookie            Callback argument.
 * @return void*    Async focus handle.
 *
 * @note After `on_suggestion` give `MEDIA_FOCUS_STOP`, you should
 * abandon the handle by `media_uv_focus_abandon`.
 *
 * @code
 *  void user_on_suggestion(int, suggestion, void* cookie) {
 *      UserContext* ctx = cookie;
 *
 *      switch (suggestion) {
 *      case MEDIA_FOCUS_STOP:
 *          media_uv_focus_abandon();
 *      }
 *  }
 *
 *  ctx->handle = media_uv_focus_request(loop, MEDIA_SCENARIO_MUSIC,
 *      user_on_suggestion, ctx);
 * @endcode
 */
void* media_uv_focus_request(void* loop, const char* scenario,
    media_focus_callback on_suggestion, void* cookie);

/**
 * @brief Request audio focus.
 *
 * @param[in] loop              Handle uv_loop_t* of current thread.
 * @param[in] scenario          MEDIA_SCENARIO_*.
 * @param[out] on_suggestion    Callback to get focus suggestion.
 * @param[in] cookie            Callback argument.
 * @param[in] auto_reply        Auto reply or not.
 * @return void*    Async focus handle.
 *
 * @note After `on_suggestion` give `MEDIA_FOCUS_STOP`, you should
 * abandon the handle by `media_uv_focus_abandon`.
 *
 * @code
 *  void user_on_suggestion(int, suggestion, int req_id, void* cookie) {
 *      UserContext* ctx = cookie;
 *
 *      switch (suggestion) {
 *      case MEDIA_FOCUS_STOP:
 *          media_uv_focus_abandon();
 *      }
 *  }
 *
 *  ctx->handle = media_uv_focus_request2(loop, MEDIA_SCENARIO_MUSIC,
 *      user_on_suggestion, 1, ctx);
 * @endcode
 */
void* media_uv_focus_request2(void* loop, const char* scenario,
    media_focus_callback2 on_suggestion, int auto_reply, void* cookie);

/**
 * @brief Abandon audio focus.
 *
 * @param[in] handle        Async focus handle..
 * @param[in] on_abandon    Callback to release cookie.
 * @return int  Zero on success, negative errno on failure.
 */
int media_uv_focus_abandon(void* handle, media_uv_callback on_abandon);

/**
 * @brief Reply audio focus.
 *
 * @param[in] handle        Async focus handle.
 * @param[in] req_id        Request id.
 * @return int  Zero on success, negative errno on failure.
 */
int media_uv_focus_reply(void* handle, int req_id);
#endif

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* FRAMEWORKS_MEDIA_INCLUDE_MEDIA_FOCUS_H */
