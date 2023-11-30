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

#include <media_event.h>
#include <media_stream.h>
#include <stddef.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Suggestions for users. */

#define MEDIA_FOCUS_PLAY 0
#define MEDIA_FOCUS_STOP 1
#define MEDIA_FOCUS_PAUSE 2
#define MEDIA_FOCUS_PLAY_BUT_SILENT 3
#define MEDIA_FOCUS_PLAY_WITH_DUCK 4 /* Play with low volume. */
#define MEDIA_FOCUS_PLAY_WITH_KEEP 5 /* Nothing should be done. */

/****************************************************************************
 * Public Types
 ****************************************************************************/

/**
 * @brief Callback to receive suggestions.
 *
 * @param[in] suggestion    @see MEIDA_FOCUS_* .
 * @param[in] cookie        Argument set by `media_focus_request`.
 * @code
 *  void demo_focu_callback(int suggestion, void* cookie)
 *  {
 *      switch(suggestion) {
 *          case MEDIA_FOCUS_PLAY:
 *          case MEDIA_FOCUS_STOP:
 *          case MEDIA_FOCUS_PAUSE:
 *          case MEDIA_FOCUS_PLAY_BUT_SILENT:
 *          case MEDIA_FOCUS_PLAY_WITH_DUCK:
 *          case MEDIA_FOCUS_PLAY_WITH_KEEP:
 *          default:
 *      }
 *  }
 * @endcode
 */
typedef void (*media_focus_callback)(int suggestion, void* cookie);

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/**
 * @brief Allow application to request audio focus.
 *
 * @param[out] initial_suggestion   @see MEIDA_FOCUS_* .
 * @param[in]  scenario             @see MEDIA_SCENARIO_* .
 * @param[in]  on_suggestion        Callback to receive suggestions.
 * @param[in]  cookie               Argument for callback.
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
 *      MEDIA_STREAM_ALARM, demo_focu_callback, context);
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
 * @brief Abandon audio focus.
 *
 * @param[in] handle    Focus handle to abandon.
 * @return Zero when request succeed, negative on failure.
 */
int media_focus_abandon(void* handle);

/**
 * @brief Dump focus stack.
 *
 * @param[in] options   Dump options(unused so far).
 */
void media_focus_dump(const char* options);

#ifdef CONFIG_LIBUV
/**
 * @brief Request audio focus.
 *
 * @param[in] loop          Handle uv_loop_t* of current thread.
 * @param[in] scenario      @see MEDIA_SCENARIO_*.
 * @param[in] on_suggest    Callback to get focus suggestion.
 * @param[in] cookie        Callback argument.
 * @return void*        Handle of focus.
 */
void* media_uv_focus_request(void* loop, const char* scenario,
    media_focus_callback on_suggest, void* cookie);

/**
 * @brief Abandon audio focus.
 *
 * @param[in] handle        Handle of focus.
 * @param[in] on_abandon    Callback to release cookie.
 * @return int          Zero on success, negative errno on failure.
 */
int media_uv_focus_abandon(void* handle, media_uv_callback on_abandon);
#endif

#endif /* FRAMEWORKS_MEDIA_INCLUDE_MEDIA_FOCUS_H */
