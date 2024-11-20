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

#define APP_FOCUS_STATE_STACK_TOP 1 /* app focus id at stack top */
#define APP_FOCUS_STATE_STACK_QUIT -1 /* app focus id out of stack */
#define APP_FOCUS_STATE_STACK_UNDER -2 /* app focus id under stack */

/****************************************************************************
 * Public Types
 ****************************************************************************/

typedef void (*media_focus_callback)(int return_type, void* callback_argv);

typedef void (*app_focus_callback)(int focus_return_type, void* callback_argv);

// struct for single focus id
typedef struct app_focus_id {
    int client_id;
    int focus_level;
    unsigned int thread_id; // int extendable for different platform
    int focus_state;
    app_focus_callback focus_callback;
    void* callback_argv;
} app_focus_id;

typedef void (*app_focus_change_callback)(
    app_focus_id* cur_app_focus_id,
    app_focus_id* request_app_focus_id,
    int callback_flag);

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

/**
 * @brief Display all focus request in audio focus stack.
 */
void media_focus_debug_stack_display(void);

/**
 * @brief Assign current media focus stack to focus list,
 *        and give out unused space in num of focus_list.
 *
 * @param[out]  focus_list    pointer of media focus identity list
 * @param[in]   num           length of the focus_id_list
 * @return              number of unused focus_id in focus_id_list.
 */

int media_focus_debug_stack_return(media_focus_id* focus_list, int num);

/****************************************************************************
 * Name: app_focus_stack_top
 *
 * Description:
 *   This function get top app focus id in app focus stack.
 *
 * Input Parameters:
 *   x       - pointer of app focus stack
 *   value   - pointer of focus id, which will catch value of top focus id
 *
 * Returned Value:
 *   Return 0 if value catched, negative error when app focus stack is empty.
 *
 ****************************************************************************/

int app_focus_stack_top(void* x, app_focus_id* value);

/****************************************************************************
 * Name: app_focus_stack_pop
 *
 * Description:
 *   This function pop up app focus id in app focus stack.
 *
 * Input Parameters:
 *   x              - pointer of app focus stack
 *   value          - pointer of focus id, which will catch value of top focus id
 *   callback_flag  - flag for identify call back method status
 *
 * Returned Value:
 *   Return 0 if value poped, negative error when app focus stack is empty.
 *
 ****************************************************************************/

int app_focus_stack_pop(void* x, app_focus_id* value, int callback_flag);

/****************************************************************************
 * Name: app_focus_stack_push
 *
 * Description:
 *   This function push such app focus id into app focus stack.
 *
 * Input Parameters:
 *   x              - pointer of app focus stack
 *   value          - pointer of focus id, value will be pushed into stack
 *   callback_flag  - flag for identify call back method status
 *
 * Returned Value:
 *   Return 0 if push succeed, negative error number when push failed.
 *
 ****************************************************************************/

int app_focus_stack_push(void* x, app_focus_id* value, int callback_flag);

/****************************************************************************
 * Name: app_focus_stack_insert
 *
 * Description:
 *   This function insert such app focus id into app focus stack with index.
 *
 * Input Parameters:
 *   x              - pointer of app focus stack
 *   value          - pointer of focus id, value will be insert into stack
 *   index          - index location of new app focus id
 *
 * Returned Value:
 *   Return 0 if push succeed, negative error number when push failed.
 *
 ****************************************************************************/

int app_focus_stack_insert(void* x, app_focus_id* value, int index);

/****************************************************************************
 * Name: app_focus_stack_delete
 *
 * Description:
 *   This function delete app focus id in app focus stack with client id.
 *
 * Input Parameters:
 *   x              - pointer of app focus stack
 *   value          - pointer of focus id, same client id will be deleted
 *   callback_flag  - flag for identify call back method status
 *
 * Returned Value:
 *   Return 0 if delete succeed, negative error number when failed.
 *
 ****************************************************************************/

int app_focus_stack_delete(void* x, app_focus_id* value, int callback_flag);

/****************************************************************************
 * Name: app_focus_stack_useless_clear
 *
 * Description:
 *   This function clear useless app focus id in app focus stack.
 *
 * Input Parameters:
 *   x              - pointer of app focus stack
 *   callback_flag  - flag for identify call back method status
 *
 * Returned Value:
 *   No return.
 *
 ****************************************************************************/

void app_focus_stack_useless_clear(void* x, int callback_flag);

/****************************************************************************
 * Name: app_focus_stack_top_change_broadcast
 *
 * Description:
 *   This function broadcast under focus with top app focus id in focus stack.
 *
 * Input Parameters:
 *   x              - pointer of app focus stack
 *   callback_flag  - flag for identify call back method status
 *
 * Returned Value:
 *   Return 0 if notify successd, negative error number when nofiy failed.
 *
 ****************************************************************************/

int app_focus_stack_top_change_broadcast(void* x, int callback_flag);

/****************************************************************************
 * Name: app_focus_stack_clean
 *
 * Description:
 *   This function clean all focus id in app focus stack.
 *
 * Input Parameters:
 *   x              - pointer of app focus stack
 *   value          - pointer of focus id, which launch clean method
 *   callback_flag  - flag for identify call back method status
 *
 * Returned Value:
 *   No return.
 *
 ****************************************************************************/

void app_focus_stack_clean(void* x, app_focus_id* value, int callback_flag);

/****************************************************************************
 * Name: app_focus_stack_search_client_id
 *
 * Description:
 *   This function search one app focus id in stack with same client id.
 *
 * Input Parameters:
 *   x              - pointer of app focus stack
 *   value          - pointer of focus id, which will catch searched result
 *
 * Returned Value:
 *   Return 0 if search succeed, negative error number when failed.
 *
 ****************************************************************************/

int app_focus_stack_search_client_id(void* x, app_focus_id* value);

/****************************************************************************
 * Name: app_focus_stack_search_focus_level
 *
 * Description:
 *   This function search one app focus id in stack with same focus level.
 *
 * Input Parameters:
 *   x              - pointer of app focus stack
 *   value          - pointer of focus id, which will catch searched result
 *
 * Returned Value:
 *   Return 0 if search succeed, negative error number when failed.
 *
 ****************************************************************************/

int app_focus_stack_search_focus_level(void* x, app_focus_id* value);

/****************************************************************************
 * Name: app_focus_stack_get_index
 *
 * Description:
 *   This function get app focus id value with its index value in stack.
 *
 * Input Parameters:
 *   x              - pointer of app focus stack
 *   value          - pointer of focus id, which will catch index app focus id
 *   index          - inde location of app focus in stack
 * Returned Value:
 *   Return 0 if search succeed, negative error number when failed.
 *
 ****************************************************************************/

int app_focus_stack_get_index(void* x, app_focus_id* value, int index);

/****************************************************************************
 * Name: app_focus_stack_display
 *
 * Description:
 *   This function display all app focus id in focus stack with stack order
 *
 * Input Parameters:
 *   x              - pointer of app focus stack

 * Returned Value:
 *   No return.
 *
 ****************************************************************************/

void app_focus_stack_display(void* x);

/****************************************************************************
 * Name: app_focus_stack_init
 *
 * Description:
 *   This function initialize app stack setting when it created.
 *
 * Input Parameters:
 *   focus_stack_size       - focus stack size from xxx focus setting
 *   focus_callback_method  - xxx focus callback function
 *
 * Returned Value:
 *   Pointer to a delicated xxx focus stack setting when success, failed with
 *   NULL.
 *
 ****************************************************************************/

void* app_focus_stack_init(size_t focus_stack_size,
    app_focus_change_callback focus_callback_method);

/****************************************************************************
 * Name: app_focus_stack_destory
 *
 * Description:
 *   This function destory one specific app stack setting.
 *
 * Input Parameters:
 *   x       - pointer of app focus stack setting
 *
 * Returned Value:
 *   void.
 *
 ****************************************************************************/

void app_focus_stack_destory(void* x);

/****************************************************************************
 * Name: app_focus_free_client_id
 *
 * Description:
 *   This function find free client id in node list for new focus request.
 *
 * Input Parameters:
 *   x    - pointer of app focus stack setting
 *
 * Returned Value:
 *   Return negative num when there is no free client for new request, 0 and
 *   other positive means free client id can be used in node list.
 *
 ****************************************************************************/

int app_focus_free_client_id(void* x);

/****************************************************************************
 * Name: app_focus_stack_return
 *
 * Description:
 *   This function give all app focus id in current stack.
 *
 * Input Parameters:
 *   x              - pointer of app focus stack setting
 *   focus_id_list  - pointer of app focus identity list
 *   num            - length of the focus_id_list
 *
 * Returned Value:
 *   Return number of unused focus_id in focus_id_list.
 *
 ****************************************************************************/

int app_focus_stack_return(void* x, app_focus_id* focus_id_list, int num);

#endif /* FRAMEWORKS_MEDIA_INCLUDE_MEDIA_FOCUS_H */
