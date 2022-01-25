/****************************************************************************
 * frameworks/media/media_focus.h
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
 * Pre-processor Definitions
 ****************************************************************************/

//media focus request play result suggestion,
#define MEDIA_FOCUS_PLAY                0   // media play
#define MEDIA_FOCUS_STOP                1   // media stop
#define MEDIA_FOCUS_PAUSE               2   // media pause
#define MEDIA_FOCUS_PLAY_BUT_SILENT     3   // media play but silent in background
#define MEDIA_FOCUS_PLAY_WITH_DUCK      4   // media play in background, duck volumn down
#define MEDIA_FOCUS_PLAY_WITH_KEEP      5   // media play keep current status in background

/****************************************************************************
 * Public Types
 ****************************************************************************/

typedef void (*media_focus_callback)(int return_type, void* callback_argv);

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: media_focus_request
 *
 * Description:
 *   This function allow application to request audio focus.
 *
 * Input Parameters:
 *   return_type            - pointer of play return suggestion for app
 *   stream_type            - one of stream types defined in media_wrapper.h
 *   callback_method        - callback method of request app
 *   callback_argv          - argument of callback
 *
 * Returned Value:
 *   Return NULL when request failed.
 *   Return void* handle for request.
 *
 * Comments:
 *  Value of return_type are:
 *          MEDIA_FOCUS_PLAY(0)             with media play,
 *          MEDIA_FOCUS_STOP(1)             with media stop,
 *          MEDIA_FOCUS_PLAY_BUT_SILENT(3)  with play but silent in background
 *          MEDIA_FOCUS_PLAY_WITH_DUCK(4)   with play with duck volume down.
 *
 ****************************************************************************/

void* media_focus_request(int* return_type,
    const char* stream_type,
    media_focus_callback callback_method,
    void* callback_argv);

/****************************************************************************
 * Name: media_focus_abandon
 *
 * Description:
 *   This function allow application to abandon its audio focus.
 *
 * Input Parameters:
 *   handle          - app identify id in audio focus stack
 *
 * Returned Value:
 *   Return 0 if specific abandon success.
 *   Return negative number when abandon failed.
 *
 ****************************************************************************/

int media_focus_abandon(void* handle);

/****************************************************************************
 * Debug Type
 ****************************************************************************/

typedef struct media_focus_id {
    int client_id;
    int stream_type;
    unsigned int thread_id;
    int focus_state;
    media_focus_callback callback_method;
    void* callback_argv;
} media_focus_id;

/****************************************************************************
 * Debug Functions
 ****************************************************************************/

/****************************************************************************
 * Name: media_focus_debug_stack_display
 *
 * Description:
 *   This function display all focus request in audio focus stack.
 *
 * Returned Value:
 *   Void.
 *
 ****************************************************************************/

void media_focus_debug_stack_display(void);

/****************************************************************************
 * Name: media_focus_debug_stack_return
 *
 * Description:
 *   This function assign current media focus stack to focus list, and give
 *   out unused space in num of focus_list.
 *
 * Input Parameters:
 *   focus_list          - pointer of media focus identity list
 *   num                 - length of the focus_id_list
 *
 * Returned Value:
 *   Return number of unused focus_id in focus_id_list.
 *
 ****************************************************************************/

int media_focus_debug_stack_return(media_focus_id* focus_list, int num);

#endif /* FRAMEWORKS_MEDIA_INCLUDE_MEDIA_FOCUS_H */