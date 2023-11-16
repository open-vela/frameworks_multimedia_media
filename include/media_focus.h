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

// media focus request play result suggestion,
#define MEDIA_FOCUS_PLAY 0 // media play
#define MEDIA_FOCUS_STOP 1 // media stop
#define MEDIA_FOCUS_PAUSE 2 // media pause
#define MEDIA_FOCUS_PLAY_BUT_SILENT 3 // media play but silent in background
#define MEDIA_FOCUS_PLAY_WITH_DUCK 4 // media play in background, duck volumn down
#define MEDIA_FOCUS_PLAY_WITH_KEEP 5 // media play keep current status in background

/****************************************************************************
 * Public Types
 ****************************************************************************/

typedef void (*media_focus_callback)(int return_type, void* callback_argv);

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/**
 * @brief Allow application to request audio focus.
 *
 * @param[out] return_type   Initial suggestion given by focus policy, details
 *                           @see MEIDA_FOCUS_* in media_focus.h
 * @param[in]  stream_type   Represent the type of the incoming stream to get
 *                           the focus. Stream type macro defines detail,
 *                           @see MEDIA_STREAM_* in media_wrapper.h
 * @param[in]  callback_method  Callback method of request app
 * @param[in]  callback_argv    Argument needed by the callback_method
 * @return     NULL when request failed, void* handle(mostly) for request.
 * @note       Value of return_type are announced above.
 *
 * Here is a example of how to use it:
 * @code
 *  @note Do this callback after get focus suggestion.
 * void* example_focu_callback(int return_type, void* cookie)
 * {
 *      struct chain_s* chain = cookie;
 *      switch(return_type) {
 *          case MEDIA_FOCUS_PLAY:
 *              chain->name = "MEDIA_FOCUS_PLAY";
 *              break;
 *          case MEDIA_FOCUS_STOP:
 *              chain->name = "MEDIA_FOCUS_STOP";
 *              break;
 *          case MEDIA_FOCUS_PAUSE:
 *              chain->name = "MEDIA_FOCUS_PAUSE";
 *              break;
 *          case MEDIA_FOCUS_PLAY_BUT_SILENT:
 *              chain->name = "MEDIA_FOCUS_PLAY_BUT_SILENT";
 *              break;
 *          case MEDIA_FOCUS_PLAY_WITH_DUCK:
 *              chain->name = "MEDIA_FOCUS_PLAY_WITH_DUCK";
 *              break;
 *          case MEDIA_FOCUS_PLAY_WITH_KEEP:
 *              chain->name = "MEDIA_FOCUS_PLAY_WITH_KEEP";
 *              break;
 *          default:
 *              chain->name = "UNKOWN_SUGGESTION";
 *              break;
 *      }
 *      printf("[%s], suggestion value: %d, suggestion name: %s\n",
 *              __func__, suggestion, chain->name, suggestion);
 * }
 *
 *  @note Get focus and store suggestion in address "&suggestion".
 * handle = media_focus_request(&suggestion, MEDIA_STREAM_ALARM,
                                    example_focu_callback, &context);
 * @endcode
 *
 */
void* media_focus_request(int* return_type,
    const char* stream_type,
    media_focus_callback callback_method,
    void* callback_argv);

/**
 * @brief Allow application to abandon its audio focus.
 *
 * @param[in]  handle    The focus handle to lose focus, @see media_focus_request()
 * @return     Zero when request succeed, negative on failure.
 *
 */
int media_focus_abandon(void* handle);

/**
 * @brief Dump focus stack
 *
 * @param[in] options   Dump options(unused so far).
 */
void media_focus_dump(const char* options);

#ifdef CONFIG_LIBUV
/**
 * @brief Request audio focus.
 *
 * @param loop          Handle uv_loop_t* of current thread.
 * @param stream_type   @see MEDIA_STREAM_*.
 * @param on_suggest    Callback to get focus suggestion.
 * @param cookie        Callback argument.
 * @return void*        Handle of focus.
 */
void* media_uv_focus_request(void* loop, const char* stream_type,
    media_focus_callback on_suggest, void* cookie);

/**
 * @brief Abandon audio focus.
 *
 * @param handle        Handle of focus.
 * @param on_abandon    Callback to release cookie.
 * @return int          Zero on success, negative errno on failure.
 */
int media_uv_focus_abandon(void* handle, media_uv_callback on_abandon);
#endif

#endif /* FRAMEWORKS_MEDIA_INCLUDE_MEDIA_FOCUS_H */
