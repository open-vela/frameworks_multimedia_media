/****************************************************************************
 * frameworks/media/media_focus.c
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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <ctype.h>
#include <debug.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app_focus.h"
#include "media_focus.h"
#include "media_internal.h"
#include "media_wrapper.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_MEDIA_FOCUS_STACK_DEPTH
#define CONFIG_MEDIA_FOCUS_STACK_DEPTH 8
#endif

#define BLOCK_CALLBACK_FLAG     0
#define NONBLOCK_CALLBACK_FLAG (-1)
#define MAX_LEN                 512
#define STREAM_TYPE_LEN         32
#define ID_SHIFT                16

#define MEDIA_FOCUS_FILE_READ_JUMP          0
#define MEDIA_FOCUS_FILE_READ_STREAM_TYPE   1
#define MEDIA_FOCUS_FILE_READ_STREAM_NUM    2

#define ID_TO_HANDLE(x) (((x) << ID_SHIFT) | 0x0000000F)
#define HANDLE_TO_ID(x) ((x) >> ID_SHIFT)

/****************************************************************************
 * Private Data
 ****************************************************************************/

/** media focus is kind if combination of app_focus.h and media stream type.
 * app_focus provide app focus stack for focus changing. And media stream
 * interaction is the core arbitrate of the media focus play result.
 */

typedef struct media_focus_cell {
    int pro_inter;
    int pas_inter;
} media_focus_cell;

typedef struct media_focus {
    int num;
    void* stack;
    char* streams;
    media_focus_cell* matrix;
} media_focus;

static media_focus* g_media_focus = NULL;
static pthread_mutex_t g_media_focus_lock = PTHREAD_MUTEX_INITIALIZER;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

// reformat string with removing space and change line symbol
static char* media_focus_reformat(char* str)
{
    char *out = str, *put = str;
    for (; *str != '\0'; ++str) {
        if (*str != ' ' && *str != '\n') {
            *put++ = *str;
        }
    }
    *put = '\0';
    return out;
}

// method for checking valid input line
static int media_focus_valid_line_check(char* line)
{
    char pre = '\0';
    char cur;
    for (int i = 0; i < strlen(line); i++) {
        cur = *(line + i);
        if (isalnum(cur) == 0 && cur != ',' && cur != ':') {
            // only letter, num and comma in string line
            goto close;
        }
        if (cur == ',' && pre == ',') {
            // no continuous commas
            goto close;
        }
        if (cur == ':' && pre == ':') {
            // no continuous colons
            goto close;
        }
        if (i == strlen(line) - 1) {
            // last should not have comma and colon
            if (cur == ',' || cur == ':') {
                goto close;
            }
        }
        pre = cur;
    }
    return 0;

close:
    return -EINVAL;
}

static int media_focus_str_to_num(char* str)
{
    if (strlen(str) > 0 && (strspn(str, "0123456789") == strlen(str))) {
        long int num = strtol(str, NULL, 10);
        return num;
    }
    return -EINVAL;
}

// interate from line by comma
static char* media_focus_interate_through_comma(char* sl)
{
    char needle = ',';
    return strchr(sl, needle);
}

// give out stream type mumbers based on string token
static int media_focus_stream_type_counts(char* sl)
{
    int count = 0;
    char* front_str = sl;

    if (sl == NULL) {
        return count;
    }
    while (1) {
        (*front_str == ',') ? count++ : count;
        (*front_str == '\0') ? count++ : count;
        if (*front_str == '\0') {
            break;
        }
        sl = front_str + 1;
        front_str++;
    }
    return count;
}

static char* media_focus_streams_init(int col, char* line)
{
    int s_count = 0;
    char* streams = NULL;

    if (media_focus_stream_type_counts(line) == 0) {
        auderr("invalid input in matrix stream\n");
        return NULL;
    }
    g_media_focus->num = media_focus_stream_type_counts(line);
    streams = zalloc((g_media_focus->num * col) * sizeof(char));
    if (streams == NULL) {
        auderr("no mem for creating streams\n");
        return NULL;
    }
    char* ptr = strtok(line, ",");
    // filler streams content with line read
    while (ptr != NULL) {
        int ptr_len = strlen(ptr);
        if (s_count < g_media_focus->num && ptr_len < col) {
            memcpy((streams + s_count * col), ptr, ptr_len);
            s_count += 1;
        } else {
            return NULL;
        }
        ptr = strtok(NULL, ",");
    }
    if (s_count != g_media_focus->num) {
        return NULL;
    }
    return streams;
}

static int media_focus_divided_by_colon(char* ptr_line, int* pro, int* pas)
{
    char needle = ':';
    char* index_ptr = NULL;

    index_ptr = strchr(ptr_line, needle);
    if (index_ptr == NULL) {
        auderr("invalid conf file\n");
        return -EINVAL;
    }
    *index_ptr = '\0';
    *pro = media_focus_str_to_num(ptr_line);
    if (*pro < 0) {
        return -EINVAL;
    }
    *pas = media_focus_str_to_num(index_ptr + 1);
    if (*pas < 0) {
        return -EINVAL;
    }
    *index_ptr = ':';
    return 0;
}

static media_focus_cell* media_focus_matrix_init(int len, char* line, int* index, media_focus_cell* matrix)
{
    int count = len;
    char* ptr = strtok(line, ",");
    if (len == 0)
        return NULL;
    if (matrix == NULL) {
        matrix = malloc((len * len) * sizeof(media_focus_cell));
        if (matrix == NULL) {
            auderr("no mem for media focus matrix\n");
            return NULL;
        }
    }
    while (ptr != NULL) {
        if (count >= 0) {
            int pro_num, pas_num;
            if (media_focus_divided_by_colon(ptr, &pro_num, &pas_num) < 0) {
                free(matrix);
                return NULL;
            }
            (matrix + *index)->pro_inter = pro_num;
            (matrix + *index)->pas_inter = pas_num;

            // counts minus and index plus after one media_focus_cell added in matrix
            *index += 1;
            count -= 1;
        } else {
            break;
        }
        ptr = strtok(NULL, ",");
    }
    if (count != 0 && ptr != NULL) {
        return NULL;
    }
    return matrix;
}

// identify date type based on starting of the line, then shift
static int media_focus_line_identity(char* line, int* shift_index)
{
    char* comma_index;
    if (*line == '#') {
        *shift_index = 0;
        return MEDIA_FOCUS_FILE_READ_JUMP;
    } else if (*line != '\0') {
        if (media_focus_valid_line_check(line) < 0) {
            return -EINVAL;
        }

        // get first comma str from input line
        comma_index = media_focus_interate_through_comma(line);
        if (comma_index == NULL) {
            return -EINVAL;
        }

        //shift index position is position after first comma
        *shift_index = comma_index - line + 1;
        if (strncmp(line, "Stream", (*shift_index - 1)) == 0) {
            return MEDIA_FOCUS_FILE_READ_STREAM_TYPE;
        } else {
            return MEDIA_FOCUS_FILE_READ_STREAM_NUM;
        }
    } else {
        return -EINVAL;
    }
}

static int media_focus_play_arbitrate(app_focus_id* top_id, app_focus_id* cur_id)
{
    int inter_location;
    int play_ret = MEDIA_FOCUS_STOP;

    if (!g_media_focus) {
        auderr("media focus intera matrix does not exist\n");
        return play_ret;
    }
    switch (cur_id->focus_state) {
    case APP_FOCUS_STATE_STACK_TOP:
        play_ret = MEDIA_FOCUS_PLAY;
        break;
    case APP_FOCUS_STATE_STACK_QUIT:
        play_ret = MEDIA_FOCUS_STOP;
        break;
    case APP_FOCUS_STATE_STACK_UNDER:
        inter_location = top_id->focus_level * g_media_focus->num + cur_id->focus_level;
        if (inter_location < (g_media_focus->num * g_media_focus->num)) {
            play_ret = (g_media_focus->matrix + inter_location)->pas_inter;
        } else {
            auderr("no matched data\n");
            play_ret = MEDIA_FOCUS_STOP;
        }
        break;
    default:
        play_ret = MEDIA_FOCUS_STOP;
        break;
    }
    return play_ret;
}

// callback method blocker for oper which does not needs instance callback
static void media_focus_stack_callback(
    app_focus_id* cur_id,
    app_focus_id* req_id,
    int callback_flag)
{
    // step 1: use callback_flag block unnecessary calling
    if (callback_flag >= 0) {
        return;
    }

    // step 2: get focus return type of focus change listener
    if (cur_id != NULL && req_id != NULL) {
        req_id->focus_callback(media_focus_play_arbitrate(cur_id, req_id), req_id->callback_argv);
    }
}

static void media_focus_destory(void)
{
    if (g_media_focus) {
        free(g_media_focus->stack);
        free(g_media_focus->streams);
        free(g_media_focus->matrix);
        free(g_media_focus);
        g_media_focus = NULL;
    }
}

static int media_focus_init(void)
{
    FILE* fp;
    char* buf = NULL;
    int ret = 0;
    int index = 0;
    int shift_index = 0;

    fp = fopen(CONFIG_MEDIA_FOCUS_CONFIG_PATH"/media_focus.conf", "r");
    if (fp == NULL) {
        auderr("no such interaction matrix file\n");
        return -ENOENT;
    }
    buf = malloc(MAX_LEN * sizeof(char));
    if (buf == NULL) {
        ret = -ENOMEM;
        goto out;
    }
    buf[MAX_LEN - 1] = '\0';

    g_media_focus = zalloc(sizeof(media_focus));
    if (g_media_focus == NULL) {
        ret = -ENOMEM;
        goto out;
    }

    while (fgets(buf, MAX_LEN - 1, fp) != NULL) {

        // step 1: remove line and change line symbol from the buf reader result
        char* line = media_focus_reformat(buf);

        // step 2: analysis reformated line by its head
        ret = media_focus_line_identity(line, &shift_index);

        // get first valid str in line with index shift
        line += shift_index;
        switch (ret) {
        case MEDIA_FOCUS_FILE_READ_STREAM_TYPE:

            // step 3.1: malloc space for stream types array based on number of stream type and fill with line read
            g_media_focus->streams = media_focus_streams_init(STREAM_TYPE_LEN, line);
            if (!g_media_focus->streams) {
                auderr("no mem for media focus streams\n");
                ret = -ENOMEM;
                goto out;
            }

            // step 3.2 malloc space and init media stack
            g_media_focus->stack = app_focus_stack_init(CONFIG_MEDIA_FOCUS_STACK_DEPTH, &media_focus_stack_callback);
            if (!g_media_focus->stack) {
                auderr("no mem for media focus stack\n");
                ret = -ENOMEM;
                goto out;
            }
            break;

        case MEDIA_FOCUS_FILE_READ_STREAM_NUM:
            // step 4: filler interaction play result from each line
            g_media_focus->matrix = media_focus_matrix_init(g_media_focus->num, line, &index, g_media_focus->matrix);
            if (!g_media_focus->matrix) {
                auderr("no mem for media focus stack\n");
                ret = -ENOMEM;
                goto out;
            }
            break;
        case MEDIA_FOCUS_FILE_READ_JUMP:
            break;
        default:
            ret = -EINVAL;
            goto out;
        }
    }

out:
    fclose(fp);
    free(buf);
    if (ret < 0)
        media_focus_destory();
    return ret;
}

static int media_focus_focus_id_insert(void* x, app_focus_id* new_focus_id)
{
    int index_value = 0;
    app_focus_id focus_id;
    while (!app_focus_stack_get_index(x, &focus_id, index_value)) {
        if (new_focus_id->focus_level <= focus_id.focus_level) {
            break;
        }
        index_value += 1;
    }
    if (index_value < 0) {
        auderr("no appro location for inserting new request\n");
        return -ENODATA;
    }
    if (app_focus_stack_insert(x, new_focus_id, index_value) < 0) {
        auderr("inter media request failed\n");
        return -EINVAL;
    }
    return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void* media_focus_request(int* return_type,
    const char* stream_type,
    media_focus_callback callback_method,
    void* callback_argv)
{
    int i;
    int ret = -EINVAL;
    int valid_id;
    int new_stream_type = -EINVAL;
    app_focus_id tmp_id;
    app_focus_id new_id;

    // step 1: check if callback method is null or not
    if (return_type == NULL || callback_method == NULL) {
        auderr("wrong callback method input\n");
        return NULL;
    }
    pthread_mutex_lock(&g_media_focus_lock);

    // step 2: get media focus
    if (!g_media_focus) {
        if (media_focus_init() < 0) {
            auderr("media focus initial failed\n");
            goto err;
        }
    }

    // step 3: trans stream type to valid num in interaction matrix
    for (i = 0; i < g_media_focus->num; i++) {
        if (strcmp((g_media_focus->streams + i * STREAM_TYPE_LEN), stream_type) == 0) {
            new_stream_type = i;
            break;
        }
    }
    if (new_stream_type < 0) {
        auderr("wrong stream type input\n");
        goto err;
    }

    // step 4: useless focus node clear
    app_focus_stack_useless_clear(g_media_focus->stack, NONBLOCK_CALLBACK_FLAG);

    // step 5: get valid id from focus stack
    valid_id = app_focus_free_client_id(g_media_focus->stack);
    if (valid_id < 0) {
        auderr("audio focus stack is full\n");
        goto err;
    }

    // step 6: specific app_focus_request assemable
    new_id.client_id = valid_id;
    new_id.focus_level = new_stream_type;
    new_id.thread_id = getpid();
    new_id.focus_state = APP_FOCUS_STATE_STACK_QUIT;
    new_id.focus_callback = callback_method;
    new_id.callback_argv = callback_argv;

    // step 7: get exist top focus id
    if (app_focus_stack_top(g_media_focus->stack, &tmp_id) == 0) {

        // step 8.1: compare with stack top
        int inter_location = new_id.focus_level * g_media_focus->num + tmp_id.focus_level;
        if (inter_location < (g_media_focus->num * g_media_focus->num)) {
            ret = (g_media_focus->matrix + inter_location)->pro_inter;
            *return_type = ret;
            switch (ret) {
            case MEDIA_FOCUS_PLAY:
                app_focus_stack_push(g_media_focus->stack, &new_id, BLOCK_CALLBACK_FLAG);
                app_focus_stack_top_change_broadcast(g_media_focus->stack, NONBLOCK_CALLBACK_FLAG);
                break;
            case MEDIA_FOCUS_PLAY_BUT_SILENT:
            case MEDIA_FOCUS_PLAY_WITH_DUCK:
                if (media_focus_focus_id_insert(g_media_focus->stack, &new_id) < 0) {
                    ret = -EINVAL;
                    goto err;
                }
                break;
            default:
                *return_type = MEDIA_FOCUS_STOP;
                break;
            }
        } else {
            auderr("no matched data\n");
            *return_type = MEDIA_FOCUS_STOP;
        }
    } else {
        // step 8.2: stack top not exist, request directly
        *return_type = MEDIA_FOCUS_PLAY;
        ret = MEDIA_FOCUS_PLAY;
        app_focus_stack_push(g_media_focus->stack, &new_id, BLOCK_CALLBACK_FLAG);
    }

err:
    pthread_mutex_unlock(&g_media_focus_lock);
    if (ret < 0) {
        return NULL;
    }
    if (*return_type == MEDIA_FOCUS_STOP) {
        valid_id += CONFIG_MEDIA_FOCUS_STACK_DEPTH;
    }
    return (void*)ID_TO_HANDLE(valid_id);
}

int media_focus_abandon(void* handle)
{
    app_focus_id tmp_id;
    int ret = 0;
    int app_client_id = (int)handle;

    // step 1: invalid app client id check
    if (app_client_id < ID_SHIFT - 1) {
        auderr("invalid app client id input\n");
        return -EINVAL;
    }
    app_client_id = HANDLE_TO_ID(app_client_id);

    pthread_mutex_lock(&g_media_focus_lock);
    // step 3: useless focus id clear
    app_focus_stack_useless_clear(g_media_focus->stack, NONBLOCK_CALLBACK_FLAG);

    // step 4: get exist top focus id
    if (app_focus_stack_top(g_media_focus->stack, &tmp_id) < 0) {
        auderr("media focus stack is empty\n");
        ret = -ENOENT;
        goto err;
    }
    if (tmp_id.client_id == app_client_id) {
        app_focus_stack_delete(g_media_focus->stack, &tmp_id, NONBLOCK_CALLBACK_FLAG);
        app_focus_stack_top_change_broadcast(g_media_focus->stack, NONBLOCK_CALLBACK_FLAG);
    } else {
        // step 5.2: abandon focus id in media focus stack
        tmp_id.client_id = app_client_id;
        app_focus_stack_delete(g_media_focus->stack, &tmp_id, NONBLOCK_CALLBACK_FLAG);
    }

err:
    pthread_mutex_unlock(&g_media_focus_lock);
    return ret;
}

void media_focus_debug_stack_display(void)
{
    pthread_mutex_lock(&g_media_focus_lock);
    if (g_media_focus) {
        app_focus_stack_display(g_media_focus->stack);
    }
    pthread_mutex_unlock(&g_media_focus_lock);
}

int media_focus_debug_stack_return(media_focus_id* p_focus_list, int num)
{
    return app_focus_stack_return(g_media_focus->stack, (app_focus_id*)p_focus_list, num);
}
