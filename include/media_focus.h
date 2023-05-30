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
 * @param[out] return_type      pointer of play return suggestion for app
 * @param[in]  stream_type      one of stream types defined in media_wrapper.h
 * @param[in]  callback_method  callback method of request app
 * @param[in]  callback_argv    argument of callback
 * @return     NULL when request failed, void* handle for request.
 * @note       Value of return_type are announced above.
 */
void* media_focus_request(int* return_type,
    const char* stream_type,
    media_focus_callback callback_method,
    void* callback_argv);

/**
 * @brief Allow application to abandon its audio focus.
 *
 * @param[in] handle    app identify id in audio focus stack
 * @return 0 if specific abandon success, negative number when abandon failed.
 */
int media_focus_abandon(void* handle);

/**
 * @brief Dump focus stack
 * @param[in] options   dump options
 */
void media_focus_dump(const char* options);

#endif /* FRAMEWORKS_MEDIA_INCLUDE_MEDIA_FOCUS_H */
