/****************************************************************************
 * framework/media/media_tool.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <stdlib.h>
#include <system/readline.h>
#include <unistd.h>
#ifdef CONFIG_LIBUV_EXTENSION
#include <uv.h>
#include <uv_async_queue.h>
#endif

#include <media_api.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MEDIATOOL_MAX_CHAIN 16
#define MEDIATOOL_MAX_ARGC 16
#define MEDIATOOL_MAX_SIZE 1024
#define MEDIATOOL_PLAYER 1
#define MEDIATOOL_RECORDER 2
#define MEDIATOOL_CONTROLLER 3
#define MEDIATOOL_CONTROLLEE 4
#define MEDIATOOL_FOCUS 5
#define MEDIATOOL_UVPLAYER 6
#define MEDIATOOL_UVRECORDER 7
#define MEDIATOOL_UVFOCUS 8
#define MEDIATOOL_UVCONTROLLER 9
#define MEDIATOOL_UVCONTROLLEE 10
#define MEDIATOOL_POLICY 11

#define GET_ARG_FUNC(out_type, arg)                  \
    static out_type get_##out_type##_arg(char* arg); \
    static out_type get_##out_type##_arg(char* arg)

#define GET_ARG(out_type, arg) \
    get_##out_type##_arg(arg)

#define CMD0(func)                                                                 \
    static int mediatool_cmd_##func##_exec(mediatool_t* mediatool);                \
    static int mediatool_cmd_##func(mediatool_t* mediatool, int argc, char** argv) \
    {                                                                              \
        return mediatool_cmd_##func##_exec(mediatool);                             \
    }                                                                              \
    static int mediatool_cmd_##func##_exec(mediatool_t* mediatool)

#define CMD1(func, type1, arg1)                                                    \
    static int mediatool_cmd_##func##_exec(mediatool_t* mediatool, type1 arg1);    \
    static int mediatool_cmd_##func(mediatool_t* mediatool, int argc, char** argv) \
    {                                                                              \
        type1 arg1;                                                                \
        arg1 = (argc > 1) ? GET_ARG(type1, argv[1]) : 0;                           \
        return mediatool_cmd_##func##_exec(mediatool, arg1);                       \
    }                                                                              \
    static int mediatool_cmd_##func##_exec(mediatool_t* mediatool, type1 arg1)

#define CMD2(func, type1, arg1, type2, arg2)                                                \
    static int mediatool_cmd_##func##_exec(mediatool_t* mediatool, type1 arg1, type2 arg2); \
    static int mediatool_cmd_##func(mediatool_t* mediatool, int argc, char** argv)          \
    {                                                                                       \
        type1 arg1;                                                                         \
        type2 arg2;                                                                         \
        arg1 = (argc > 1) ? GET_ARG(type1, argv[1]) : 0;                                    \
        arg2 = (argc > 2) ? GET_ARG(type2, argv[2]) : 0;                                    \
        return mediatool_cmd_##func##_exec(mediatool, arg1, arg2);                          \
    }                                                                                       \
    static int mediatool_cmd_##func##_exec(mediatool_t* mediatool, type1 arg1, type2 arg2)

#define CMD3(func, type1, arg1, type2, arg2, type3, arg3)                                               \
    static int mediatool_cmd_##func##_exec(mediatool_t* mediatool, type1 arg1, type2 arg2, type3 arg3); \
    static int mediatool_cmd_##func(mediatool_t* mediatool, int argc, char** argv)                      \
    {                                                                                                   \
        type1 arg1;                                                                                     \
        type2 arg2;                                                                                     \
        type3 arg3;                                                                                     \
        arg1 = (argc > 1) ? GET_ARG(type1, argv[1]) : 0;                                                \
        arg2 = (argc > 2) ? GET_ARG(type2, argv[2]) : 0;                                                \
        arg3 = (argc > 3) ? GET_ARG(type3, argv[3]) : 0;                                                \
        return mediatool_cmd_##func##_exec(mediatool, arg1, arg2, arg3);                                \
    }                                                                                                   \
    static int mediatool_cmd_##func##_exec(mediatool_t* mediatool, type1 arg1, type2 arg2, type3 arg3)

#define CMD4(func, type1, arg1, type2, arg2, type3, arg3, type4, arg4)                                              \
    static int mediatool_cmd_##func##_exec(mediatool_t* mediatool, type1 arg1, type2 arg2, type3 arg3, type4 arg4); \
    static int mediatool_cmd_##func(mediatool_t* mediatool, int argc, char** argv)                                  \
    {                                                                                                               \
        type1 arg1;                                                                                                 \
        type2 arg2;                                                                                                 \
        type3 arg3;                                                                                                 \
        type4 arg4;                                                                                                 \
        arg1 = (argc > 1) ? GET_ARG(type1, argv[1]) : 0;                                                            \
        arg2 = (argc > 2) ? GET_ARG(type2, argv[2]) : 0;                                                            \
        arg3 = (argc > 3) ? GET_ARG(type3, argv[3]) : 0;                                                            \
        arg4 = (argc > 4) ? GET_ARG(type4, argv[4]) : 0;                                                            \
        return mediatool_cmd_##func##_exec(mediatool, arg1, arg2, arg3, arg4);                                      \
    }                                                                                                               \
    static int mediatool_cmd_##func##_exec(mediatool_t* mediatool, type1 arg1, type2 arg2, type3 arg3, type4 arg4)

#define CMD8(func, t1, a1, t2, a2, t3, a3, t4, a4, t5, a5, t6, a6, t7, a7, t8, a8)                                          \
    static int mediatool_cmd_##func##_exec(mediatool_t* mediatool, t1 a1, t2 a2, t3 a3, t4 a4, t5 a5, t6 a6, t7 a7, t8 a8); \
    static int mediatool_cmd_##func(mediatool_t* mediatool, int argc, char** argv)                                          \
    {                                                                                                                       \
        t1 a1;                                                                                                              \
        t2 a2;                                                                                                              \
        t3 a3;                                                                                                              \
        t4 a4;                                                                                                              \
        t5 a5;                                                                                                              \
        t6 a6;                                                                                                              \
        t7 a7;                                                                                                              \
        t8 a8;                                                                                                              \
        a1 = (argc > 1) ? GET_ARG(t1, argv[1]) : 0;                                                                         \
        a2 = (argc > 2) ? GET_ARG(t2, argv[2]) : 0;                                                                         \
        a3 = (argc > 3) ? GET_ARG(t3, argv[3]) : 0;                                                                         \
        a4 = (argc > 4) ? GET_ARG(t4, argv[4]) : 0;                                                                         \
        a5 = (argc > 5) ? GET_ARG(t5, argv[5]) : 0;                                                                         \
        a6 = (argc > 6) ? GET_ARG(t6, argv[6]) : 0;                                                                         \
        a7 = (argc > 7) ? GET_ARG(t7, argv[7]) : 0;                                                                         \
        a8 = (argc > 8) ? GET_ARG(t8, argv[8]) : 0;                                                                         \
        return mediatool_cmd_##func##_exec(mediatool, a1, a2, a3, a4, a5, a6, a7, a8);                                      \
    }                                                                                                                       \
    static int mediatool_cmd_##func##_exec(mediatool_t* mediatool, t1 a1, t2 a2, t3 a3, t4 a4, t5 a5, t6 a6, t7 a7, t8 a8)

/****************************************************************************
 * Type Declarations
 ****************************************************************************/

typedef struct mediatool_chain_s {
    int id;
    int type;
    void* handle;
    void* extra;

    pthread_t thread;
    int fd;

    bool start;
    bool loop;

    bool direct;
    char* buf;
    int size;

#ifdef CONFIG_LIBUV_EXTENSION
    void* data;
    uv_pipe_t* pipe; /* pipe handle object */
    uv_fs_t fs_req;
    uv_write_t write_req;
#endif
} mediatool_chain_t;

typedef struct mediatool_s {
    mediatool_chain_t chain[MEDIATOOL_MAX_CHAIN];
#ifdef CONFIG_LIBUV_EXTENSION
    uv_loop_t loop;
    uv_async_queue_t asyncq;
#endif
} mediatool_t;

typedef int (*mediatool_func)(mediatool_t* mediatool, int argc, char** argv);

typedef struct mediatool_cmd_s {
    const char* cmd; /* The command text */
    mediatool_func pfunc; /* Pointer to command handler */
    const char* help; /* The help text */
} mediatool_cmd_t;

typedef char* string_t;

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_LIBUV_EXTENSION
/* uv recorder buffer mode functions declaration */
static void mediatool_uv_recorder_alloc_cb(uv_handle_t* handle,
    unsigned suggested_size, uv_buf_t* buf);
static void mediatool_uv_recorder_write_cb(uv_fs_t* req);
static void mediatool_uv_recorder_read_cb(uv_stream_t* stream,
    ssize_t nread, const uv_buf_t* buf);
static void mediatool_uv_recorder_connection_cb(void* cookie, int ret, void* obj);

/* uv player buffer mode functions declaration */
static void mediatool_uv_player_write_cb(uv_write_t* req, int status);
static void mediatool_uv_player_read_cb(uv_fs_t* req);
static void mediatool_uv_player_connection_cb(void* cookie, int ret, void* obj);
#endif

/****************************************************************************
 * Private Function
 ****************************************************************************/

GET_ARG_FUNC(int, arg)
{
    return strtol(arg, NULL, 0);
}

GET_ARG_FUNC(string_t, arg)
{
    if (arg && !strlen(arg))
        return NULL;
    return arg;
}

static void mediatool_controller_callback(void* cookie, int event,
    int ret, const char* extra)
{
    mediatool_chain_t* chain = cookie;

    printf("[%s][%d] id:%d, event:%s(%d) ret:%d extra:%s\n",
        __func__, __LINE__, chain->id, media_event_get_name(event), event, ret, extra);
}

static void mediatool_controllee_callback(void* cookie, int event,
    int ret, const char* extra)
{
    mediatool_chain_t* chain = cookie;

    printf("[%s][%d] id:%d, event:%s(%d) ret:%d extra:%s\n",
        __func__, __LINE__, chain->id, media_event_get_name(event), event, ret, extra);

    /* Assume we've done real work and notify the result. */
    media_session_notify(chain->handle, event, 0, "fake");
}

static void mediatool_controllee_music_callback(void* cookie, int event,
    int ret, const char* extra)
{
    mediatool_chain_t* chain = cookie;

    printf("[%s][%d] id:%d, event:%s(%d) ret:%d extra:%s\n",
        __func__, __LINE__, chain->id, media_event_get_name(event), event, ret, extra);

    /* Handle control message by mediatool api. */
    switch (event) {
    case MEDIA_EVENT_START:
        ret = media_player_start(chain->handle);
        break;

    case MEDIA_EVENT_PAUSE:
        ret = media_player_pause(chain->handle);
        break;

    case MEDIA_EVENT_STOP:
        ret = media_player_stop(chain->handle);
        break;

    case MEDIA_EVENT_INCREASE_VOLUME:
        ret = media_policy_decrease_stream_volume(MEDIA_STREAM_MUSIC);
        break;

    case MEDIA_EVENT_DECREASE_VOLUME:
        ret = media_policy_increase_stream_volume(MEDIA_STREAM_MUSIC);
        break;

    default:
        /* There is no song list in this demo. */
        ret = -ENOSYS;
        break;
    }

    /* XXX: Another implemention for some status control message,
     * notify after received corresponding event from player's event callback */
    media_session_notify(chain->handle, event, ret, NULL);
}

static void mediatool_event_callback(void* cookie, int event,
    int ret, const char* extra)
{
    mediatool_chain_t* chain = cookie;

    printf("[%s][%d] id:%d, event:%s(%d) ret:%d extra:%s\n",
        __func__, __LINE__, chain->id, media_event_get_name(event), event, ret, extra);
}

static void mediatool_takepic_callback(void* cookie, int event,
    int ret, const char* extra)
{
    struct mediatool_chain_s* chain = cookie;

    if (event == MEDIA_EVENT_COMPLETED) {
        media_recorder_finish_picture(chain->handle);
        chain->handle = NULL;
        chain->extra = NULL;
    }

    printf("[%s][%d] id:%d, event:%s(%d) ret:%d extra:%s\n",
        __func__, __LINE__, chain->id, media_event_get_name(event), event, ret, extra);
}

static void mediatool_focus_callback(int suggestion, void* cookie)
{
    mediatool_chain_t* chain = cookie;
    char* str;

    if (suggestion == MEDIA_FOCUS_PLAY) {
        str = "MEDIA_FOCUS_PLAY";
    } else if (suggestion == MEDIA_FOCUS_STOP) {
        str = "MEDIA_FOCUS_STOP";
    } else if (suggestion == MEDIA_FOCUS_PAUSE) {
        str = "MEDIA_FOCUS_PAUSE";
    } else if (suggestion == MEDIA_FOCUS_PLAY_BUT_SILENT) {
        str = "MEDIA_FOCUS_PLAY_BUT_SILENT";
    } else if (suggestion == MEDIA_FOCUS_PLAY_WITH_DUCK) {
        str = "MEDIA_FOCUS_PLAY_WITH_DUCK";
    } else if (suggestion == MEDIA_FOCUS_PLAY_WITH_KEEP) {
        str = "MEDIA_FOCUS_PLAY_WITH_KEEP";
    } else {
        str = "UNKOWN";
    }

    printf("%s, id %d, suggestion %s, suggestion %d, line %d\n",
        __func__, chain->id, str, suggestion, __LINE__);
}

static void mediatool_policy_callback(void* cookie, int number, const char* literal)
{
    mediatool_chain_t* chain = cookie;
    printf("[%s] id:%d number:%d value:%s\n",
        __func__, chain->id, number, literal);
}

static void mediatool_display_metadata(int id, const media_metadata_t* data)
{
    printf("id:%d f:%d st:%d vol:%d pos:%u dur:%u ttl:%s art:%s\n", id,
        data->flags, data->state, data->volume, data->position, data->duration,
        data->title, data->artist);
}

#ifdef CONFIG_LIBUV_EXTENSION
static void mediatool_uv_common_close_cb(void* cookie, int ret)
{
    mediatool_chain_t* chain = cookie;

    printf("[%s] id:%d ret:%d\n", __func__, chain->id, ret);
    chain->handle = NULL;
    chain->id = 0;
}

static void mediatool_uv_common_open_cb(void* cookie, int ret)
{
    mediatool_chain_t* chain = cookie;

    printf("[%s] id:%d ret:%d\n", __func__, chain->id, ret);
}

static void mediatool_uv_common_prepare_cb(void* cookie, int ret)
{
    mediatool_chain_t* chain = cookie;

    printf("[%s] id:%d ret:%d\n", __func__, chain->id, ret);
}

static void mediatool_uv_common_start_cb(void* cookie, int ret)
{
    mediatool_chain_t* chain = cookie;

    printf("[%s] id:%d ret:%d\n", __func__, chain->id, ret);
}

static void mediatool_uv_common_pause_cb(void* cookie, int ret)
{
    mediatool_chain_t* chain = cookie;

    printf("[%s] id:%d ret:%d\n", __func__, chain->id, ret);
}

static void mediatool_uv_common_stop_cb(void* cookie, int ret)
{
    mediatool_chain_t* chain = cookie;

    printf("[%s] id:%d ret:%d\n", __func__, chain->id, ret);
    free(chain->buf);
    chain->buf = NULL;
}

static void mediatool_uv_player_reset_cb(void* cookie, int ret)
{
    mediatool_chain_t* chain = cookie;

    printf("[%s] id:%d ret:%d\n", __func__, chain->id, ret);
}

static void mediatool_uv_common_get_position_cb(void* cookie, int ret, unsigned position)
{
    mediatool_chain_t* chain = cookie;

    printf("[%s] id:%d ret:%d val:%u\n", __func__, chain->id, ret, position);
}

static void mediatool_uv_common_get_duration_cb(void* cookie, int ret, unsigned duration)
{
    mediatool_chain_t* chain = cookie;

    printf("[%s] id:%d ret:%d val:%u\n", __func__, chain->id, ret, duration);
}

static void mediatool_uv_common_get_volume_cb(void* cookie, int ret, float volume)
{
    mediatool_chain_t* chain = cookie;

    printf("[%s] id:%d ret:%d val:%f\n", __func__, chain->id, ret, volume);
}

static void mediatool_uv_common_increase_volume_cb(void* cookie, int ret)
{
    mediatool_chain_t* chain = cookie;

    printf("[%s] id:%d ret:%d\n", __func__, chain->id, ret);
}

static void mediatool_uv_common_decrease_volume_cb(void* cookie, int ret)
{
    mediatool_chain_t* chain = cookie;

    printf("[%s] id:%d ret:%d\n", __func__, chain->id, ret);
}

static void mediatool_uv_common_set_volume_cb(void* cookie, int ret)
{
    mediatool_chain_t* chain = cookie;

    printf("[%s] id:%d ret:%d\n", __func__, chain->id, ret);
}

static void mediatool_uv_common_query_cb(void* cookie, int ret, void* object)
{
    mediatool_chain_t* chain = cookie;

    printf("[%s] id:%d ret:%d\n", __func__, chain->id, ret);
    mediatool_display_metadata(chain->id, object);
}

static void mediatool_uv_player_set_looping_cb(void* cookie, int ret)
{
    mediatool_chain_t* chain = cookie;

    printf("[%s] id:%d ret:%d\n", __func__, chain->id, ret);
}

static void mediatool_uv_common_seek_cb(void* cookie, int ret)
{
    mediatool_chain_t* chain = cookie;

    printf("[%s] id:%d ret:%d\n", __func__, chain->id, ret);
}

static void mediatool_uv_recorder_reset_cb(void* cookie, int ret)
{
    mediatool_chain_t* chain = cookie;

    printf("[%s] id:%d ret:%d\n", __func__, chain->id, ret);
}

static void media_uv_session_prev_song_cb(void* cookie, int ret)
{
    mediatool_chain_t* chain = cookie;

    printf("[%s] id:%d ret:%d\n", __func__, chain->id, ret);
}

static void media_uv_session_next_song_cb(void* cookie, int ret)
{
    mediatool_chain_t* chain = cookie;

    printf("[%s] id:%d ret:%d\n", __func__, chain->id, ret);
}

static void mediatool_uv_controllee_callback(void* cookie, int event,
    int ret, const char* extra)
{
    mediatool_chain_t* chain = cookie;

    printf("[%s][%d] id:%d, event:%s(%d) ret:%d extra:%s\n",
        __func__, __LINE__, chain->id, media_event_get_name(event), event, ret, extra);

    /* Assume we've done real work and notify the result. */
    media_uv_session_notify(chain->handle, event, 0, "fake", NULL, NULL);
}

static void mediatool_cmd_uv_policy_set_string_cb(void* cookie, int ret)
{
    printf("[%s] name:%s ret:%d\n", __func__, (char*)cookie, ret);
    free(cookie);
}

static void mediatool_cmd_uv_policy_include_cb(void* cookie, int ret)
{
    printf("[%s] name:%s ret:%d\n", __func__, (char*)cookie, ret);
    free(cookie);
}

static void mediatool_cmd_uv_policy_set_int_cb(void* cookie, int ret)
{
    printf("[%s] name:%s ret:%d\n", __func__, (char*)cookie, ret);
    free(cookie);
}

static void mediatool_cmd_uv_policy_contain_cb(void* cookie, int ret, int val)
{
    printf("[%s] name:%s ret:%d val:%d\n", __func__, (char*)cookie, ret, val);
    free(cookie);
}

static void mediatool_cmd_uv_policy_get_string_cb(void* cookie, int ret, const char* val)
{
    printf("[%s] name:%s ret:%d val:%s\n", __func__, (char*)cookie, ret, val);
    free(cookie);
}

static void mediatool_cmd_uv_policy_get_int_cb(void* cookie, int ret, int val)
{
    printf("[%s] name:%s ret:%d val:%d\n", __func__, (char*)cookie, ret, val);
    free(cookie);
}

static void mediatool_focus_suggest_cb(int suggest, void* cookie)
{
    mediatool_chain_t* chain = cookie;

    printf("[%s] id:%d suggest:%d\n", __func__, chain->id, suggest);
}

static void mediatool_cmd_uv_policy_increase_cb(void* cookie, int ret)
{
    printf("[%s] name:%s ret:%d\n", __func__, (char*)cookie, ret);
    free(cookie);
}

static void mediatool_cmd_uv_policy_exclude_cb(void* cookie, int ret)
{
    printf("[%s] name:%s ret:%d\n", __func__, (char*)cookie, ret);
    free(cookie);
}

static void mediatool_cmd_uv_policy_decrease_cb(void* cookie, int ret)
{
    printf("[%s] name:%s ret:%d\n", __func__, (char*)cookie, ret);
    free(cookie);
}

static void mediatool_uv_session_open_cb(void* cookie, int ret)
{
    mediatool_chain_t* chain = cookie;

    printf("[%s] id:%d ret:%d\n", __func__, chain->id, ret);
}

static void mediatool_uv_session_update_cb(void* cookie, int ret)
{
    mediatool_chain_t* chain = cookie;

    printf("[%s] id:%d ret:%d\n", __func__, chain->id, ret);
}

static void mediatool_uv_take_picture_complete_cb(void* cookie, int ret)
{
    if (ret < 0)
        printf("take pic failed!\n");
    else
        printf("take pic successed!\n");
}

static void mediatool_uv_recorder_alloc_cb(uv_handle_t* handle,
    unsigned suggested_size, uv_buf_t* buf)
{
    buf->base = malloc(MEDIATOOL_MAX_SIZE);
    assert(buf->base);
    buf->len = MEDIATOOL_MAX_SIZE;
}

static void mediatool_uv_recorder_write_cb(uv_fs_t* req)
{
    mediatool_chain_t* chain = uv_req_get_data((uv_req_t*)req);

    if (req->result < 0) {
        printf("[%s][%d] Recorder write to file Failed: %s\n", __func__,
            __LINE__, uv_strerror(req->result));
        free(chain->buf);
        return;
    } else
        printf("[%s][%d] Recorder Data written to file successfully.\n", __func__, __LINE__);

    free(chain->buf);
    chain->buf = NULL;
    uv_fs_req_cleanup(req);
}

static void mediatool_uv_recorder_read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
{
    mediatool_chain_t* chain = uv_handle_get_data((uv_handle_t*)stream);
    mediatool_t* mediatool = chain->data;
    uv_fs_t close_req;
    uv_buf_t iov;

    chain->buf = buf->base;
    uv_req_set_data((uv_req_t*)&chain->fs_req, chain);
    if (nread < 0) {
        if (nread != UV_EOF)
            printf("[%s][%d] Recorder read error %s\n", __func__, __LINE__, uv_err_name(nread));
        uv_fs_close(&mediatool->loop, &close_req, chain->fd, NULL);
        free(chain->buf);
        chain->buf = NULL;
        return;
    }

    iov = uv_buf_init(buf->base, nread);
    uv_fs_write(&mediatool->loop, &chain->fs_req, chain->fd,
        &iov, 1, -1, mediatool_uv_recorder_write_cb);
}

static void mediatool_uv_recorder_connection_cb(void* cookie, int ret, void* obj)
{
    mediatool_chain_t* chain = cookie;

    printf("[%s][%d] id:%d ret:%d obj:%p\n", __func__, __LINE__, chain->id, ret, obj);
    if (!obj)
        return;

    uv_handle_set_data(obj, cookie);
    uv_read_start(obj, mediatool_uv_recorder_alloc_cb, mediatool_uv_recorder_read_cb);
}

static void mediatool_uv_player_write_cb(uv_write_t* req, int status)
{
    mediatool_chain_t* chain = uv_req_get_data((uv_req_t*)req);
    mediatool_t* mediatool = chain->data;
    uv_buf_t iov;

    if (status < 0) {
        printf("[%s][%d] Player write error: %s\n", __func__, __LINE__, uv_strerror(status));
        free(chain->buf);
        chain->buf = NULL;
        return;
    } else {
        iov = uv_buf_init(chain->buf, chain->size);
        uv_fs_read(&mediatool->loop, &chain->fs_req, chain->fd,
            &iov, 1, -1, mediatool_uv_player_read_cb);
    }
}

static void mediatool_uv_player_read_cb(uv_fs_t* req)
{
    mediatool_chain_t* chain = uv_req_get_data((uv_req_t*)req);
    mediatool_t* mediatool = chain->data;
    uv_fs_t close_req;
    uv_buf_t iov;

    if (req->result < 0) {
        printf("[%s][%d] Player Read error: %s\n", __func__, __LINE__,
            uv_strerror(req->result));
        free(chain->buf);
        chain->buf = NULL;
        return;
    } else if (req->result == 0) {
        printf("[%s][%d] Player read to end of file\n", __func__, __LINE__);
        uv_fs_close(&mediatool->loop, &close_req, chain->fd, NULL);
    } else {
        iov = uv_buf_init(chain->buf, req->result);
        uv_req_set_data((uv_req_t*)&chain->write_req, chain);
        uv_write((uv_write_t*)&chain->write_req, (uv_stream_t*)chain->pipe,
            &iov, 1, mediatool_uv_player_write_cb);
    }
}

static void mediatool_uv_player_connection_cb(void* cookie, int ret, void* obj)
{
    mediatool_chain_t* chain = cookie;
    mediatool_t* mediatool = chain->data;
    uv_buf_t iov;

    printf("[%s][%d] id:%d ret:%d obj:%p\n", __func__, __LINE__, chain->id, ret, obj);
    chain->size = MEDIATOOL_MAX_SIZE;
    chain->buf = malloc(MEDIATOOL_MAX_SIZE);
    assert(chain->buf);
    chain->pipe = obj;

    iov = uv_buf_init(chain->buf, chain->size);
    uv_req_set_data((uv_req_t*)&chain->fs_req, chain);
    uv_fs_read(&mediatool->loop, &chain->fs_req, chain->fd,
        &iov, 1, -1, mediatool_uv_player_read_cb);
}
#endif /* CONFIG_LIBUV_EXTENSION */

static void mediatool_common_stop_thread(mediatool_chain_t* chain)
{
    if (chain->thread) {
        pthread_join(chain->thread, NULL);
        free(chain->buf);

        chain->thread = 0;
        chain->buf = NULL;
    }

    if (chain->fd > 0) {
        close(chain->fd);
        chain->fd = -1;
    }
}

static int mediatool_common_stop_inner(mediatool_chain_t* chain)
{
    int ret = 0;

    switch (chain->type) {
    case MEDIATOOL_PLAYER:
        ret = media_player_stop(chain->handle);
        break;

    case MEDIATOOL_RECORDER:
        ret = media_recorder_stop(chain->handle);
        break;

#ifdef CONFIG_LIBUV_EXTENSION
    case MEDIATOOL_UVPLAYER:
        ret = media_uv_player_stop(chain->handle, mediatool_uv_common_stop_cb, chain);
        break;

    case MEDIATOOL_UVRECORDER:
        ret = media_uv_recorder_stop(chain->handle, mediatool_uv_common_stop_cb, chain);
        break;

    case MEDIATOOL_UVCONTROLLER:
        ret = media_uv_session_stop(chain->handle, mediatool_uv_common_stop_cb, chain);
        break;
#endif
    }

    usleep(1000);

    mediatool_common_stop_thread(chain);

    return ret;
}

CMD1(player_open, string_t, stream_type)
{
    long int i;
    int ret;

    for (i = 0; i < MEDIATOOL_MAX_CHAIN; i++) {
        if (!mediatool->chain[i].handle)
            break;
    }

    if (i == MEDIATOOL_MAX_CHAIN)
        return -ENOMEM;

    mediatool->chain[i].id = i;
    mediatool->chain[i].handle = media_player_open(stream_type);
    if (!mediatool->chain[i].handle) {
        printf("media_player_open error\n");
        return -EINVAL;
    }

    ret = media_player_set_event_callback(mediatool->chain[i].handle,
        &mediatool->chain[i], mediatool_event_callback);
    assert(!ret);

    if (stream_type && !strcmp(stream_type, MEDIA_STREAM_MUSIC)) {
        mediatool->chain[i].extra = media_session_register(
            &mediatool->chain[i], mediatool_controllee_music_callback);
    }

    mediatool->chain[i].type = MEDIATOOL_PLAYER;

    printf("player ID %ld\n", i);

    return 0;
}

CMD1(recorder_open, string_t, stream_type)
{
    long int i;
    int ret;

    for (i = 0; i < MEDIATOOL_MAX_CHAIN; i++) {
        if (!mediatool->chain[i].handle)
            break;
    }

    if (i == MEDIATOOL_MAX_CHAIN)
        return -ENOMEM;

    mediatool->chain[i].id = i;
    mediatool->chain[i].handle = media_recorder_open(stream_type);
    if (!mediatool->chain[i].handle) {
        printf("media_recorder_open error\n");
        return -EINVAL;
    }

    ret = media_recorder_set_event_callback(mediatool->chain[i].handle,
        &mediatool->chain[i],
        mediatool_event_callback);
    assert(!ret);

    mediatool->chain[i].type = MEDIATOOL_RECORDER;

    printf("recorder ID %ld\n", i);

    return 0;
}

CMD1(session_open, string_t, stream_type)
{
    long int i;
    int ret;

    for (i = 0; i < MEDIATOOL_MAX_CHAIN; i++) {
        if (!mediatool->chain[i].handle)
            break;
    }

    if (i == MEDIATOOL_MAX_CHAIN)
        return -ENOMEM;

    mediatool->chain[i].id = i;
    mediatool->chain[i].handle = media_session_open(stream_type);
    if (!mediatool->chain[i].handle) {
        printf("media_session_open error\n");
        return -EINVAL;
    }

    ret = media_session_set_event_callback(mediatool->chain[i].handle,
        &mediatool->chain[i], mediatool_controller_callback);
    assert(!ret);

    mediatool->chain[i].type = MEDIATOOL_CONTROLLER;
    printf("session controller ID %ld\n", i);
    return 0;
}

CMD1(session_register, string_t, param)
{
    long int i;

    for (i = 0; i < MEDIATOOL_MAX_CHAIN; i++) {
        if (!mediatool->chain[i].handle)
            break;
    }

    if (i == MEDIATOOL_MAX_CHAIN)
        return -ENOMEM;

    mediatool->chain[i].id = i;
    mediatool->chain[i].handle = media_session_register(
        &mediatool->chain[i], mediatool_controllee_callback);
    if (!mediatool->chain[i].handle) {
        printf("media_session_register error\n");
        return -EINVAL;
    }

    mediatool->chain[i].type = MEDIATOOL_CONTROLLEE;

    printf("session controllee ID %ld\n", i);

    return 0;
}

CMD2(close, int, id, int, pending_stop)
{
    int ret = -EINVAL;

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !mediatool->chain[id].handle)
        return -EINVAL;

    switch (mediatool->chain[id].type) {
    case MEDIATOOL_PLAYER:
        if (!pending_stop)
            mediatool_common_stop_inner(&mediatool->chain[id]);
        ret = media_player_close(mediatool->chain[id].handle, pending_stop);
        if (ret >= 0 && mediatool->chain[id].extra)
            ret = media_session_unregister(mediatool->chain[id].extra);
        break;

    case MEDIATOOL_RECORDER:
        mediatool_common_stop_inner(&mediatool->chain[id]);
        ret = media_recorder_close(mediatool->chain[id].handle);
        break;

    case MEDIATOOL_CONTROLLER:
        ret = media_session_close(mediatool->chain[id].handle);
        break;

    case MEDIATOOL_CONTROLLEE:
        ret = media_session_unregister(mediatool->chain[id].handle);
        break;

    case MEDIATOOL_FOCUS:
        ret = media_focus_abandon(mediatool->chain[id].handle);
        break;

    case MEDIATOOL_POLICY:
        ret = media_policy_unsubscribe(mediatool->chain[id].handle);
        break;

#ifdef CONFIG_LIBUV_EXTENSION
    case MEDIATOOL_UVPLAYER:
        if (!pending_stop)
            mediatool_common_stop_inner(&mediatool->chain[id]);
        ret = media_uv_player_close(mediatool->chain[id].handle, pending_stop, mediatool_uv_common_close_cb);
        break;

    case MEDIATOOL_UVRECORDER:
        mediatool_common_stop_inner(&mediatool->chain[id]);
        ret = media_uv_recorder_close(mediatool->chain[id].handle, mediatool_uv_common_close_cb);
        break;

    case MEDIATOOL_UVFOCUS:
        ret = media_uv_focus_abandon(mediatool->chain[id].handle, mediatool_uv_common_close_cb);
        break;

    case MEDIATOOL_UVCONTROLLER:
        ret = media_uv_session_close(mediatool->chain[id].handle, mediatool_uv_common_close_cb);
        break;

    case MEDIATOOL_UVCONTROLLEE:
        ret = media_uv_session_unregister(mediatool->chain[id].handle, mediatool_uv_common_close_cb);
        break;
#endif /* CONFIG_LIBUV_EXTENSION */
    }

    mediatool->chain[id].handle = NULL;
    mediatool->chain[id].extra = NULL;

    return ret;
}

CMD1(query, int, id)
{
    const media_metadata_t* data = NULL;
    int ret = -EINVAL;

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !mediatool->chain[id].handle)
        return -EINVAL;

    switch (mediatool->chain[id].type) {
    case MEDIATOOL_CONTROLLER:
        ret = media_session_query(mediatool->chain[id].handle, &data);
        break;

#ifdef CONFIG_LIBUV_EXTENSION
    case MEDIATOOL_UVCONTROLLER:
        return media_uv_session_query(mediatool->chain[id].handle,
            mediatool_uv_common_query_cb, &mediatool->chain[id]);

    case MEDIATOOL_UVPLAYER:
        return media_uv_player_query(mediatool->chain[id].handle,
            mediatool_uv_common_query_cb, &mediatool->chain[id]);
#endif

    default:
        break;
    }

    if (ret < 0 || !data)
        return ret;

    mediatool_display_metadata(id, data);
    return ret;
}

CMD8(update, int, id, int, flags, int, state, int, volume, int, position, int, duration, string_t, title, string_t, artist)
{
    media_metadata_t data = {
        .flags = flags,
        .state = state,
        .volume = volume,
        .position = position,
        .duration = duration,
        .title = title,
        .artist = artist,
    };
    int ret;

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !mediatool->chain[id].handle)
        return -EINVAL;

    switch (mediatool->chain[id].type) {
    case MEDIATOOL_CONTROLLEE:
        ret = media_session_update(mediatool->chain[id].handle, &data);
        break;

#ifdef CONFIG_LIBUV_EXTENSION
    case MEDIATOOL_UVCONTROLLEE:
        ret = media_uv_session_update(mediatool->chain[id].handle, &data,
            mediatool_uv_session_update_cb, &mediatool->chain[id]);
        break;
#endif

    default:
        ret = -EINVAL;
        break;
    }

    return ret;
}

CMD1(reset, int, id)
{
    int ret;

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !mediatool->chain[id].handle)
        return -EINVAL;

    switch (mediatool->chain[id].type) {
    case MEDIATOOL_PLAYER:
        ret = media_player_reset(mediatool->chain[id].handle);
        break;

    case MEDIATOOL_RECORDER:
        ret = media_recorder_reset(mediatool->chain[id].handle);
        break;

#ifdef CONFIG_LIBUV_EXTENSION
    case MEDIATOOL_UVPLAYER:
        ret = media_uv_player_reset(mediatool->chain[id].handle,
            mediatool_uv_player_reset_cb, &mediatool->chain[id]);
        break;

    case MEDIATOOL_UVRECORDER:
        ret = media_uv_recorder_reset(mediatool->chain[id].handle,
            mediatool_uv_recorder_reset_cb, &mediatool->chain[id]);
        break;
#endif

    default:
        return 0;
    }

    mediatool_common_stop_thread(&mediatool->chain[id]);

    return ret;
}

static ssize_t mediatool_process_data(int fd, bool player,
    void* data, size_t len)
{
    int event = player ? POLLOUT : POLLIN;
    struct pollfd fds[1];
    int ret;

    fds[0].fd = fd;
    fds[0].events = event;
    ret = poll(fds, 1, -1);

    if (ret < 0)
        return -errno;

    if (player)
        return send(fd, data, len, 0);
    else
        return recv(fd, data, len, 0);
}

static void* mediatool_buffer_thread(void* arg)
{
    mediatool_chain_t* chain = arg;
    int act, ret, fd = 0;
    char* tmp;

    printf("%s, start, line %d\n", __func__, __LINE__);

    if (chain->direct) {
        if (chain->type == MEDIATOOL_PLAYER)
            fd = media_player_get_socket(chain->handle);
        else
            fd = media_recorder_get_socket(chain->handle);

        if (fd < 0)
            return NULL;

        if (fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK) < 0)
            return NULL;
    }

    if (chain->type == MEDIATOOL_PLAYER) {
        while (1) {
            act = read(chain->fd, chain->buf, chain->size);
            assert(act >= 0);
            if (act == 0) {
                media_player_close_socket(chain->handle);
                break;
            }

            tmp = chain->buf;
            while (act > 0) {
                if (chain->direct)
                    ret = mediatool_process_data(fd, true, tmp, act);
                else
                    ret = media_player_write_data(chain->handle, tmp, act);

                if (ret == 0) {
                    break;
                } else if (ret < 0 && errno == EAGAIN) {
                    continue;
                } else if (ret < 0) {
                    printf("%s, error ret %d errno %d, line %d\n", __func__, ret, errno, __LINE__);
                    goto out;
                }

                tmp += ret;
                act -= ret;
            }
        }
    } else {
        while (1) {
            if (chain->direct)
                ret = mediatool_process_data(fd, false, chain->buf, chain->size);
            else
                ret = media_recorder_read_data(chain->handle, chain->buf, chain->size);

            if (ret == 0) {
                media_recorder_close_socket(chain->handle);
                break;
            } else if (ret < 0 && errno == EAGAIN) {
                continue;
            }

            if (ret <= 0)
                goto out;

            act = write(chain->fd, chain->buf, ret);
            assert(act == ret);
        }
    }

out:
    printf("%s, end, line %d\n", __func__, __LINE__);
    return NULL;
}

CMD4(prepare, int, id, string_t, mode, string_t, path, string_t, options)
{
    bool async_mode = false;
    bool url_mode = false;
    bool direct = false;
    pthread_t thread;
    int ret = 0;

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !mediatool->chain[id].handle)
        return -EINVAL;

    if (!mode || !path)
        return -EINVAL;

    if (!strcmp(mode, "url"))
        url_mode = true;
    else if (!strcmp(mode, "direct"))
        direct = true;

    if (!url_mode) {
        if (mediatool->chain[id].thread) {
            printf("already prepare, can't prepare twice\n");
            return -EPERM;
        }

        if (mediatool->chain[id].type == MEDIATOOL_RECORDER)
            unlink(path);

        if ((mediatool->chain[id].type == MEDIATOOL_RECORDER) || (mediatool->chain[id].type == MEDIATOOL_UVRECORDER))
            mediatool->chain[id].fd = open(path, O_CREAT | O_RDWR | O_CLOEXEC | O_TRUNC, 0666);
        else if ((mediatool->chain[id].type == MEDIATOOL_PLAYER) || (mediatool->chain[id].type == MEDIATOOL_UVPLAYER))
            mediatool->chain[id].fd = open(path, O_RDONLY | O_CLOEXEC, 0666);
        if (mediatool->chain[id].fd < 0) {
            printf("buffer mode, file can't open\n");
            return -EINVAL;
        }
    }

    switch (mediatool->chain[id].type) {
    case MEDIATOOL_PLAYER:
        ret = media_player_prepare(mediatool->chain[id].handle, url_mode ? path : NULL, options);
        break;

    case MEDIATOOL_RECORDER:
        ret = media_recorder_prepare(mediatool->chain[id].handle, url_mode ? path : NULL, options);
        break;

#ifdef CONFIG_LIBUV_EXTENSION
    case MEDIATOOL_UVPLAYER:
        mediatool->chain[id].data = mediatool;
        ret = media_uv_player_prepare(mediatool->chain[id].handle, url_mode ? path : NULL, options,
            mediatool_uv_player_connection_cb, mediatool_uv_common_prepare_cb, &mediatool->chain[id]);
        async_mode = true;
        break;

    case MEDIATOOL_UVRECORDER:
        mediatool->chain[id].data = mediatool;
        ret = media_uv_recorder_prepare(mediatool->chain[id].handle, url_mode ? path : NULL, options,
            mediatool_uv_recorder_connection_cb, mediatool_uv_common_prepare_cb, &mediatool->chain[id]);
        async_mode = true;
        break;
#endif

    default:
        printf("Unsupported type!\n");
        ret = -EINVAL;
        break;
    }

    if (ret < 0)
        goto err;

    if (!async_mode && !url_mode) {
        mediatool->chain[id].direct = direct;
        mediatool->chain[id].size = 512;
        mediatool->chain[id].buf = malloc(mediatool->chain[id].size);
        assert(mediatool->chain[id].buf);

        ret = pthread_create(&thread, NULL, mediatool_buffer_thread, &mediatool->chain[id]);
        assert(!ret);

        pthread_setname_np(thread, "mediatool_file");
        mediatool->chain[id].thread = thread;
    }

    return ret;

err:
    if (!url_mode && mediatool->chain[id].fd >= 0) {
        close(mediatool->chain[id].fd);
        mediatool->chain[id].fd = -1;
    }
    return ret;
}

CMD2(start, int, id, string_t, scenario)
{
    int ret = -EINVAL;

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !mediatool->chain[id].handle)
        return -EINVAL;

    switch (mediatool->chain[id].type) {
    case MEDIATOOL_PLAYER:
        ret = media_player_start(mediatool->chain[id].handle);
        break;

    case MEDIATOOL_RECORDER:
        ret = media_recorder_start(mediatool->chain[id].handle);
        break;

    case MEDIATOOL_CONTROLLER:
        ret = media_session_start(mediatool->chain[id].handle);
        break;

#ifdef CONFIG_LIBUV_EXTENSION
    case MEDIATOOL_UVPLAYER:
        if (scenario)
            ret = media_uv_player_start_auto(mediatool->chain[id].handle, scenario,
                mediatool_uv_common_start_cb, &mediatool->chain[id]);
        else
            ret = media_uv_player_start(mediatool->chain[id].handle,
                mediatool_uv_common_start_cb, &mediatool->chain[id]);
        break;

    case MEDIATOOL_UVRECORDER:
        if (scenario)
            ret = media_uv_recorder_start_auto(mediatool->chain[id].handle, scenario,
                mediatool_uv_common_start_cb, &mediatool->chain[id]);
        else
            ret = media_uv_recorder_start(mediatool->chain[id].handle,
                mediatool_uv_common_start_cb, &mediatool->chain[id]);
        break;

    case MEDIATOOL_UVCONTROLLER:
        ret = media_uv_session_start(mediatool->chain[id].handle,
            mediatool_uv_common_start_cb, &mediatool->chain[id]);
        break;
#endif
    }

    return ret;
}

CMD1(stop, int, id)
{
    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !mediatool->chain[id].handle)
        return -EINVAL;

    return mediatool_common_stop_inner(&mediatool->chain[id]);
}

CMD1(pause, int, id)
{
    int ret = 0;

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !mediatool->chain[id].handle)
        return -EINVAL;

    switch (mediatool->chain[id].type) {
    case MEDIATOOL_PLAYER:
        ret = media_player_pause(mediatool->chain[id].handle);
        break;

    case MEDIATOOL_RECORDER:
        ret = media_recorder_pause(mediatool->chain[id].handle);
        break;

    case MEDIATOOL_CONTROLLER:
        ret = media_session_pause(mediatool->chain[id].handle);
        break;

#ifdef CONFIG_LIBUV_EXTENSION
    case MEDIATOOL_UVPLAYER:
        ret = media_uv_player_pause(mediatool->chain[id].handle,
            mediatool_uv_common_pause_cb, &mediatool->chain[id]);
        break;

    case MEDIATOOL_UVRECORDER:
        ret = media_uv_recorder_pause(mediatool->chain[id].handle,
            mediatool_uv_common_pause_cb, &mediatool->chain[id]);
        break;

    case MEDIATOOL_UVCONTROLLER:
        ret = media_uv_session_pause(mediatool->chain[id].handle,
            mediatool_uv_common_pause_cb, &mediatool->chain[id]);
        break;
#endif
    }

    return ret;
}

CMD2(volume, int, id, string_t, volume_cmd)
{
    float volume_f;
    int volume_i;
    int ret = 0;
    char* ptr;

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !mediatool->chain[id].handle)
        return -EINVAL;

    if (!volume_cmd)
        return -EINVAL;

    switch (mediatool->chain[id].type) {
    case MEDIATOOL_PLAYER:
        if ((ptr = strchr(volume_cmd, '?'))) {
            ret = media_player_get_volume(mediatool->chain[id].handle, &volume_f);
            printf("ID %d, get volume %f\n", id, volume_f);
        } else {
            if ((ptr = strcasestr(volume_cmd, "db")))
                volume_f = pow(10.0, strtof(volume_cmd, NULL) / 20);
            else
                volume_f = strtof(volume_cmd, NULL);
            ret = media_player_set_volume(mediatool->chain[id].handle, volume_f);
            printf("ID %d, set volume %f\n", id, volume_f);
        }
        break;

    case MEDIATOOL_CONTROLLER:
        if ((ptr = strchr(volume_cmd, '?'))) {
            ret = media_session_get_volume(mediatool->chain[id].handle, &volume_i);
            printf("ID %d, get volume %d\n", id, volume_i);
        } else if ((ptr = strchr(volume_cmd, '+'))) {
            ret = media_session_get_volume(mediatool->chain[id].handle, &volume_i);
            ret = media_session_increase_volume(mediatool->chain[id].handle);
            printf("ID %d, increase volume %d++\n", id, volume_i);
        } else if ((ptr = strchr(volume_cmd, '-'))) {
            ret = media_session_get_volume(mediatool->chain[id].handle, &volume_i);
            ret = media_session_decrease_volume(mediatool->chain[id].handle);
            printf("ID %d, decrease volume %d--\n", id, volume_i);
        } else {
            ret = media_session_set_volume(mediatool->chain[id].handle, strtol(volume_cmd, NULL, 10));
            printf("ID %d, set volume %d\n", id, (int)strtol(volume_cmd, NULL, 10));
        }
        break;

#ifdef CONFIG_LIBUV_EXTENSION
    case MEDIATOOL_UVPLAYER:
        if ((ptr = strchr(volume_cmd, '?'))) {
            ret = media_uv_player_get_volume(mediatool->chain[id].handle,
                mediatool_uv_common_get_volume_cb, &mediatool->chain[id]);
        } else {
            if ((ptr = strcasestr(volume_cmd, "db")))
                volume_f = pow(10.0, strtof(volume_cmd, NULL) / 20);
            else
                volume_f = strtof(volume_cmd, NULL);
            ret = media_uv_player_set_volume(mediatool->chain[id].handle, volume_f,
                mediatool_uv_common_set_volume_cb, &mediatool->chain[id]);
        }
        break;

    case MEDIATOOL_UVCONTROLLER:
        if ((ptr = strchr(volume_cmd, '?')))
            ret = -ENOSYS;
        else if ((ptr = strchr(volume_cmd, '+')))
            ret = media_uv_session_increase_volume(mediatool->chain[id].handle,
                mediatool_uv_common_increase_volume_cb, &mediatool->chain[id]);
        else if ((ptr = strchr(volume_cmd, '-')))
            ret = media_uv_session_decrease_volume(mediatool->chain[id].handle,
                mediatool_uv_common_decrease_volume_cb, &mediatool->chain[id]);
        else
            ret = -ENOSYS;
#endif

    case MEDIATOOL_RECORDER:
        return 0;
    }

    return ret;
}

CMD2(loop, int, id, int, isloop)
{
    int ret = 0;

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !mediatool->chain[id].handle)
        return -EINVAL;

    switch (mediatool->chain[id].type) {
    case MEDIATOOL_PLAYER:
        ret = media_player_set_looping(mediatool->chain[id].handle, isloop);
        break;

#ifdef CONFIG_LIBUV_EXTENSION
    case MEDIATOOL_UVPLAYER:
        ret = media_uv_player_set_looping(mediatool->chain[id].handle, isloop,
            mediatool_uv_player_set_looping_cb, &mediatool->chain[id]);
        break;
#endif

    default:
        ret = -EINVAL;
        break;
    }

    return ret;
}

CMD2(seek, int, id, int, msec)
{
    int ret = 0;

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !mediatool->chain[id].handle)
        return -EINVAL;

    switch (mediatool->chain[id].type) {
    case MEDIATOOL_PLAYER:
        ret = media_player_seek(mediatool->chain[id].handle, msec);
        break;

#ifdef CONFIG_LIBUV_EXTENSION
    case MEDIATOOL_UVPLAYER:
        ret = media_uv_player_seek(mediatool->chain[id].handle, msec,
            mediatool_uv_common_seek_cb, &mediatool->chain[id]);
        break;

    case MEDIATOOL_UVCONTROLLER:
        ret = media_uv_session_seek(mediatool->chain[id].handle, msec,
            mediatool_uv_common_seek_cb, &mediatool->chain[id]);
        break;
#endif

    default:
        ret = -EINVAL;
        break;
    }

    return ret;
}

CMD1(position, int, id)
{
    unsigned int position = 0;
    int ret;

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !mediatool->chain[id].handle)
        return -EINVAL;

    switch (mediatool->chain[id].type) {
    case MEDIATOOL_PLAYER:
        ret = media_player_get_position(mediatool->chain[id].handle, &position);
        break;

    case MEDIATOOL_CONTROLLER:
        ret = media_session_get_position(mediatool->chain[id].handle, &position);
        break;

#ifdef CONFIG_LIBUV_EXTENSION
    case MEDIATOOL_UVPLAYER:
        return media_uv_player_get_position(mediatool->chain[id].handle,
            mediatool_uv_common_get_position_cb, &mediatool->chain[id]);
#endif
    default:
        return 0;
    }

    if (ret < 0) {
        printf("Current position ret %d\n", ret);
        return ret;
    }

    printf("Current position %d ms\n", position);

    return 0;
}

CMD1(duration, int, id)
{
    unsigned int duration;
    int ret;

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !mediatool->chain[id].handle)
        return -EINVAL;

    switch (mediatool->chain[id].type) {
    case MEDIATOOL_PLAYER:
        ret = media_player_get_duration(mediatool->chain[id].handle, &duration);
        break;

    case MEDIATOOL_CONTROLLER:
        ret = media_session_get_duration(mediatool->chain[id].handle, &duration);
        break;

#ifdef CONFIG_LIBUV_EXTENSION
    case MEDIATOOL_UVPLAYER:
        return media_uv_player_get_duration(mediatool->chain[id].handle,
            mediatool_uv_common_get_duration_cb, &mediatool->chain[id]);
#endif
    default:
        return 0;
    }

    if (ret < 0) {
        printf("Total duration ret %d\n", ret);
        return ret;
    }

    printf("Total duration %d ms\n", duration);

    return 0;
}

CMD1(isplaying, int, id)
{
    int ret;

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !mediatool->chain[id].handle)
        return -EINVAL;

    if (mediatool->chain[id].type != MEDIATOOL_PLAYER)
        return 0;

    ret = media_player_is_playing(mediatool->chain[id].handle);
    if (ret < 0) {
        printf("is_playing ret %d\n", ret);
        return ret;
    }

    printf("Is_playing %d\n", ret);

    return 0;
}

CMD3(playdtmf, int, id, string_t, mode, string_t, dial_number)
{
    bool direct = false;
    short int* buffer;
    int buffer_size;
    int ret;
    int fd;

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !mediatool->chain[id].handle)
        return -EINVAL;

    if (!mode || mode[0] == '\0' || !dial_number || dial_number[0] == '\0')
        return -EINVAL;

    if (!strcmp(mode, "direct"))
        direct = true;
    else
        direct = false;

    buffer_size = media_dtmf_get_buffer_size(dial_number);
    buffer = (short int*)malloc(buffer_size);
    assert(buffer);

    ret = media_dtmf_generate(dial_number, buffer);
    if (ret < 0)
        goto out;

    ret = media_player_prepare(mediatool->chain[id].handle, NULL, MEDIA_TONE_DTMF_FORMAT);
    if (ret < 0)
        goto out;

    if (direct) {
        fd = media_player_get_socket(mediatool->chain[id].handle);
        if (fd < 0)
            goto out;

        if (fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK) < 0)
            goto out;

        ret = mediatool_process_data(fd, true, buffer, buffer_size);
    } else
        ret = media_player_write_data(mediatool->chain[id].handle, buffer, buffer_size);

    if (ret == buffer_size) {
        media_player_close_socket(mediatool->chain[id].handle);
        ret = 0;
    } else {
        printf("Failed to play DTMF tone.");
    }
out:
    free(buffer);
    buffer = NULL;

    return ret;

    return 0;
}

CMD1(prevsong, int, id)
{
    int ret = 0;

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !mediatool->chain[id].handle)
        return -EINVAL;

    switch (mediatool->chain[id].type) {
    case MEDIATOOL_CONTROLLER:
        ret = media_session_prev_song(mediatool->chain[id].handle);
        break;

#ifdef CONFIG_LIBUV_EXTENSION
    case MEDIATOOL_UVCONTROLLER:
        ret = media_uv_session_prev_song(mediatool->chain[id].handle,
            media_uv_session_prev_song_cb, &mediatool->chain[id]);
        break;
#endif

    default:
        ret = -EINVAL;
        break;
    }

    return ret;
}

CMD1(nextsong, int, id)
{
    int ret = 0;

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !mediatool->chain[id].handle)
        return -EINVAL;

    switch (mediatool->chain[id].type) {
    case MEDIATOOL_CONTROLLER:
        ret = media_session_next_song(mediatool->chain[id].handle);
        break;

#ifdef CONFIG_LIBUV_EXTENSION
    case MEDIATOOL_UVCONTROLLER:
        ret = media_uv_session_next_song(mediatool->chain[id].handle,
            media_uv_session_next_song_cb, &mediatool->chain[id]);
        break;
#endif

    default:
        ret = -EINVAL;
        break;
    }

    return ret;
}

CMD3(take_picture, string_t, filtername, string_t, filename, int, number)
{
    int ret = 0;

    ret = media_recorder_take_picture(filtername, filename, number);
    if (ret < 0) {
        printf("Failed to take_picture.");
    }

    return ret;
}

CMD3(take_picture_async, string_t, filtername, string_t, filename, int, number)
{
    long int i;

    for (i = 0; i < MEDIATOOL_MAX_CHAIN; i++) {
        if (!mediatool->chain[i].handle)
            break;
    }

    if (i == MEDIATOOL_MAX_CHAIN)
        return -ENOMEM;

    mediatool->chain[i].id = i;
    mediatool->chain[i].handle = media_recorder_start_picture(filtername, filename, number, mediatool_takepic_callback, mediatool->chain);
    if (!mediatool->chain[i].handle) {
        printf("media_recorder_start_picture error\n");
        return -EINVAL;
    }

    return 0;
}

static int mediatool_cmd_send(mediatool_t* mediatool, int argc, char** argv)
{
    char arg[64] = {};

    for (int i = 3; i < argc; i++) {
        strlcat(arg, argv[i], sizeof(arg));
        if (i != argc - 1)
            strlcat(arg, " ", sizeof(arg));
    }

    return media_process_command(argv[1], argv[2], arg, NULL, 0);
}

CMD1(dump, string_t, options)
{
    /* pargs not used in neither graph_dump nor policy_dump,
     * so let's make it simple, just "dump".
     */

    media_policy_dump(options);
    media_graph_dump(options);
    media_focus_dump(options);

    return 0;
}

CMD3(setint, string_t, name, int, value, int, apply)
{
    return media_policy_set_int(name, value, apply);
}

CMD1(getint, string_t, name)
{
    int value;
    int ret;

    ret = media_policy_get_int(name, &value);
    if (ret < 0)
        return -EINVAL;

    printf("get criterion %s integer value = %d\n", name, value);

    return 0;
}

CMD3(setstring, string_t, name, string_t, value, int, apply)
{
    return media_policy_set_string(name, value, apply);
}

CMD1(getstring, string_t, name)
{
    char value[64];
    int ret;

    ret = media_policy_get_string(name, value, sizeof(value));
    if (ret < 0)
        return -EINVAL;

    printf("get criterion %s string value = '%s'\n", name, value);

    return 0;
}

CMD3(include, string_t, name, string_t, value, int, apply)
{
    return media_policy_include(name, value, apply);
}

CMD3(exclude, string_t, name, string_t, value, int, apply)
{
    return media_policy_exclude(name, value, apply);
}

CMD2(contain, string_t, name, string_t, value)
{
    int result, ret;

    ret = media_policy_contain(name, value, &result);
    if (ret < 0)
        return -EINVAL;

    printf("criterion %s %s value %s\n", name, result ? "contains" : "doesn't contain", value);

    return 0;
}

CMD2(increase, string_t, name, int, apply)
{
    return media_policy_increase(name, apply);
}

CMD2(decrease, string_t, name, int, apply)
{
    return media_policy_decrease(name, apply);
}

CMD1(subscribe, string_t, name)
{
    long int i;

    for (i = 0; i < MEDIATOOL_MAX_CHAIN; i++) {
        if (!mediatool->chain[i].handle)
            break;
    }

    if (i == MEDIATOOL_MAX_CHAIN)
        return -ENOMEM;

    mediatool->chain[i].id = i;
    mediatool->chain[i].handle = media_policy_subscribe(
        name, mediatool_policy_callback, &mediatool->chain[i]);
    if (!mediatool->chain[i].handle) {
        printf("media_policy_subscribe failed\n");
        return 0;
    }
    mediatool->chain[i].type = MEDIATOOL_POLICY;
    printf("policy ID %ld\n", i);
    return 0;
}

CMD1(focus_request, string_t, name)
{
    int suggestion;
    long int i;

    for (i = 0; i < MEDIATOOL_MAX_CHAIN; i++) {
        if (!mediatool->chain[i].handle)
            break;
    }

    if (i == MEDIATOOL_MAX_CHAIN)
        return -ENOMEM;

    mediatool->chain[i].id = i;
    mediatool->chain[i].handle = media_focus_request(&suggestion, name,
        mediatool_focus_callback, &mediatool->chain[i]);
    if (!mediatool->chain[i].handle) {
        printf("media_player_request failed\n");
        return 0;
    }

    mediatool->chain[i].type = MEDIATOOL_FOCUS;
    printf("focus ID %ld, first suggestion %d\n", i, suggestion);
    return 0;
}

CMD0(quit)
{
    int i;

    for (i = 0; i < MEDIATOOL_MAX_CHAIN; i++) {
        if (mediatool->chain[i].handle)
            mediatool_cmd_close_exec(mediatool, i, 0);
    }

    return 0;
}

static int mediatool_cmd_help(const mediatool_cmd_t cmds[])
{
    int i;

    for (i = 0; cmds[i].cmd; i++)
        printf("%-16s %s\n", cmds[i].cmd, cmds[i].help);

    return 0;
}

#ifdef CONFIG_LIBUV_EXTENSION
CMD1(uv_player_open, string_t, stream_type)
{
    long int i;

    for (i = 0; i < MEDIATOOL_MAX_CHAIN; i++) {
        if (!mediatool->chain[i].handle)
            break;
    }

    if (i == MEDIATOOL_MAX_CHAIN)
        return -ENOMEM;

    mediatool->chain[i].id = i;
    mediatool->chain[i].handle = media_uv_player_open(&mediatool->loop, stream_type,
        mediatool_uv_common_open_cb, &mediatool->chain[i]);
    if (!mediatool->chain[i].handle)
        goto err;

    if (media_uv_player_listen(mediatool->chain[i].handle, mediatool_event_callback) < 0)
        goto err;

    mediatool->chain[i].type = MEDIATOOL_UVPLAYER;
    return 0;

err:
    printf("%s error\n", __func__);
    return -EINVAL;
}

CMD1(uv_recorder_open, string_t, stream_type)
{
    long int i;

    for (i = 0; i < MEDIATOOL_MAX_CHAIN; i++) {
        if (!mediatool->chain[i].handle)
            break;
    }

    if (i == MEDIATOOL_MAX_CHAIN)
        return -ENOMEM;

    mediatool->chain[i].id = i;
    mediatool->chain[i].handle = media_uv_recorder_open(&mediatool->loop, stream_type,
        mediatool_uv_common_open_cb, &mediatool->chain[i]);
    if (!mediatool->chain[i].handle)
        goto err;

    if (media_uv_recorder_listen(mediatool->chain[i].handle, mediatool_event_callback) < 0)
        goto err;

    mediatool->chain[i].type = MEDIATOOL_UVRECORDER;
    return 0;

err:
    printf("%s error\n", __func__);
    return -EINVAL;
}

CMD1(uv_session_open, string_t, param)
{
    long int i;

    for (i = 0; i < MEDIATOOL_MAX_CHAIN; i++) {
        if (!mediatool->chain[i].handle)
            break;
    }

    if (i == MEDIATOOL_MAX_CHAIN)
        return -ENOMEM;

    mediatool->chain[i].id = i;
    mediatool->chain[i].handle = media_uv_session_open(&mediatool->loop, param,
        mediatool_uv_session_open_cb, &mediatool->chain[i]);
    if (!mediatool->chain[i].handle) {
        printf("media_uv_session_open error\n");
        goto err;
    }

    if (media_uv_session_listen(mediatool->chain[i].handle, mediatool_controller_callback) < 0)
        goto err;

    mediatool->chain[i].type = MEDIATOOL_UVCONTROLLER;
    printf("async session controller ID %ld\n", i);
    return 0;

err:
    printf("%s error\n", __func__);
    return -EINVAL;
}

CMD1(uv_session_register, string_t, param)
{
    long int i;

    for (i = 0; i < MEDIATOOL_MAX_CHAIN; i++) {
        if (!mediatool->chain[i].handle)
            break;
    }

    if (i == MEDIATOOL_MAX_CHAIN)
        return -ENOMEM;

    mediatool->chain[i].id = i;
    mediatool->chain[i].handle = media_uv_session_register(&mediatool->loop, param,
        mediatool_uv_controllee_callback, &mediatool->chain[i]);
    if (!mediatool->chain[i].handle) {
        printf("%s error\n", __func__);
        goto err;
    }

    mediatool->chain[i].type = MEDIATOOL_UVCONTROLLEE;
    printf("async session controllee ID %ld\n", i);
    return 0;

err:
    printf("%s error\n", __func__);
    return -EINVAL;
}

CMD3(uv_policy_set_int, string_t, name, int, value, int, apply)
{
    return media_uv_policy_set_int(&mediatool->loop, name, value, apply,
        mediatool_cmd_uv_policy_set_int_cb, strdup(name));
}

CMD1(uv_policy_get_int, string_t, name)
{
    return media_uv_policy_get_int(&mediatool->loop, name,
        mediatool_cmd_uv_policy_get_int_cb, strdup(name));
}

CMD3(uv_policy_set_string, string_t, name, string_t, value, int, apply)
{
    return media_uv_policy_set_string(&mediatool->loop, name, value, apply,
        mediatool_cmd_uv_policy_set_string_cb, strdup(name));
}

CMD1(uv_policy_get_string, string_t, name)
{
    return media_uv_policy_get_string(&mediatool->loop, name,
        mediatool_cmd_uv_policy_get_string_cb, strdup(name));
}

CMD2(uv_policy_increase, string_t, name, int, apply)
{
    return media_uv_policy_increase(&mediatool->loop, name, apply,
        mediatool_cmd_uv_policy_increase_cb, strdup(name));
}

CMD2(uv_policy_decrease, string_t, name, int, apply)
{
    return media_uv_policy_decrease(&mediatool->loop, name, apply,
        mediatool_cmd_uv_policy_decrease_cb, strdup(name));
}

CMD3(uv_policy_include, string_t, name, string_t, value, int, apply)
{
    return media_uv_policy_include(&mediatool->loop, name, value, apply,
        mediatool_cmd_uv_policy_include_cb, strdup(name));
}

CMD3(uv_policy_exclude, string_t, name, string_t, value, int, apply)
{
    return media_uv_policy_exclude(&mediatool->loop, name, value, apply,
        mediatool_cmd_uv_policy_exclude_cb, strdup(name));
}

CMD2(uv_policy_contain, string_t, name, string_t, value)
{
    return media_uv_policy_contain(&mediatool->loop, name, value,
        mediatool_cmd_uv_policy_contain_cb, strdup(name));
}

CMD1(uv_focus_request, string_t, name)
{
    long int i;

    for (i = 0; i < MEDIATOOL_MAX_CHAIN; i++) {
        if (!mediatool->chain[i].handle)
            break;
    }

    if (i == MEDIATOOL_MAX_CHAIN)
        return -ENOMEM;

    mediatool->chain[i].id = i;
    mediatool->chain[i].handle = media_uv_focus_request(&mediatool->loop,
        name, mediatool_focus_suggest_cb, &mediatool->chain[i]);
    if (!mediatool->chain[i].handle) {
        printf("%s failed\n", __func__);
        return 0;
    }

    mediatool->chain[i].type = MEDIATOOL_UVFOCUS;
    printf("focus ID %ld\n", i);
    return 0;
}

CMD3(uv_take_picture, string_t, filtername, string_t, filename, int, number)
{
    return media_uv_recorder_take_picture(&mediatool->loop, filtername,
        filename, number, mediatool_uv_take_picture_complete_cb, NULL);
}

#endif /* CONFIG_LIBUV_EXTENSION */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

static const mediatool_cmd_t g_mediatool_cmds[] = {
    { "open",
        mediatool_cmd_player_open,
        "Create player channel return ID (open [STREAM/FILTER])" },
    { "copen",
        mediatool_cmd_recorder_open,
        "Create recorder channel return ID (copen [SOURCE/FILTER])" },
    { "sopen",
        mediatool_cmd_session_open,
        "Create session channel return ID (sopen [UNUSED])" },
    { "close",
        mediatool_cmd_close,
        "Destroy player/recorder/session channel (close ID [pending_stop(1/0)])" },
    { "sregister",
        mediatool_cmd_session_register,
        "Register as session channel return ID (sregister [UNUSED])" },
    { "sunregister",
        mediatool_cmd_close,
        "Unregister session channel (sunregister ID)" },
    { "query",
        mediatool_cmd_query,
        "Query metadata through session controller (query ID)" },
    { "update",
        mediatool_cmd_update,
        "Update metadata through session controllee (update ID FLAGS STATE VOLUME POSITION DURATION TITLE ARTIST)" },
    { "reset",
        mediatool_cmd_reset,
        "Reset player/recorder channel (reset ID)" },
    { "prepare",
        mediatool_cmd_prepare,
        "Set player/recorder prepare (prepare ID url/buffer/direct url [options])" },
    { "start",
        mediatool_cmd_start,
        "Set player/recorder/session start (start ID [SCENARIO])" },
    { "stop",
        mediatool_cmd_stop,
        "Set player/recorder/session stop (stop ID)" },
    { "pause",
        mediatool_cmd_pause,
        "Set player/recorder/session pause (pause ID)" },
    { "volume",
        mediatool_cmd_volume,
        "Set/Get player/session volume (volume ID ?/+/-/volume)" },
    { "loop",
        mediatool_cmd_loop,
        "Set/Get player loop (loop ID 1/0)" },
    { "seek",
        mediatool_cmd_seek,
        "Set player seek (seek ID time)" },
    { "position",
        mediatool_cmd_position,
        "Get player position time ms(position ID)" },
    { "duration",
        mediatool_cmd_duration,
        "Get player duration time ms(duration ID)" },
    { "isplay",
        mediatool_cmd_isplaying,
        "Get position is playing or not(isplay ID)" },
    { "playdtmf",
        mediatool_cmd_playdtmf,
        "To play dtmf tone(playdtmf ID direct/buffer dialbuttons)" },
    { "prev",
        mediatool_cmd_prevsong,
        "To play previous song in player list(prev ID)" },
    { "next",
        mediatool_cmd_nextsong,
        "To play next song in player list(next ID)" },
    { "takepic",
        mediatool_cmd_take_picture,
        "Sync take picture from camera" },
    { "takepic_async",
        mediatool_cmd_take_picture_async,
        "Async take picture from camera" },
    { "send",
        mediatool_cmd_send,
        "Send cmd to graph. PS:loglevel INFO:32 VERBOSE:40 DEBUG:48 TRACE:56" },
    { "dump",
        mediatool_cmd_dump,
        "Dump graph and policy as well as focus" },
    { "setint",
        mediatool_cmd_setint,
        "Set criterion value with integer(setint NAME VALUE APPLY)" },
    { "getint",
        mediatool_cmd_getint,
        "Get criterion value in integer(getint NAME)" },
    { "setstring",
        mediatool_cmd_setstring,
        "Set criterion value with string(setstring NAME VALUE APPLY)" },
    { "getstring",
        mediatool_cmd_getstring,
        "Get criterion value in string(getstring NAME)" },
    { "include",
        mediatool_cmd_include,
        "Include inclusive criterion values(include NAME VALUE APPLY)" },
    { "exclude",
        mediatool_cmd_exclude,
        "Exclude inclusive criterion values(exclude NAME VALUE APPLY)" },
    { "contain",
        mediatool_cmd_contain,
        "Check wether contain criterion values(contain NAME VALUE)" },
    { "increase",
        mediatool_cmd_increase,
        "Increase criterion value by one(increase NAME APPLY)" },
    { "decrease",
        mediatool_cmd_decrease,
        "Decrease criterion value by one(decrease NAME APPLY)" },
    { "subscribe",
        mediatool_cmd_subscribe,
        "Subscribe criterion value change (subscribe NAME)" },
    { "unsubscribe",
        mediatool_cmd_close,
        "Unsubscribe criterion value change (unsubscribe ID)" },
    { "request",
        mediatool_cmd_focus_request,
        "Request media focus(request SCENARIO)" },
    { "abandon",
        mediatool_cmd_close,
        "Abandon media focus(abandon ID)" },
#ifdef CONFIG_LIBUV_EXTENSION
    { "uv_open",
        mediatool_cmd_uv_player_open,
        "Create an async player return ID (uv_open [STREAM/FILTER])" },
    { "uv_copen",
        mediatool_cmd_uv_recorder_open,
        "Create an async recorder return ID (uv_copen [SOURCE/FILTER])" },
    { "uv_sopen",
        mediatool_cmd_uv_session_open,
        "Create async session channel return ID (uv_sopen [UNUSED])" },
    { "uv_sregister",
        mediatool_cmd_uv_session_register,
        "Register as async session channel return ID (uv_sregister [UNUSED])" },
    { "uv_sunregister",
        mediatool_cmd_close,
        "Unregister async session channel (sunregister ID)" },
    { "uv_setint",
        mediatool_cmd_uv_policy_set_int,
        "Async set numerical value to criterion (uv_setint NAME VALUE APPLY)" },
    { "uv_getint",
        mediatool_cmd_uv_policy_get_int,
        "Async get numerical value from criterion (uv_getint NAME)" },
    { "uv_setstr",
        mediatool_cmd_uv_policy_set_string,
        "Async set lieteral value to criterion (uv_setstr NAME VALUE APPLY)" },
    { "uv_getstr",
        mediatool_cmd_uv_policy_get_string,
        "Async get lieteral value from criterion (uv_getstr NAME)" },
    { "uv_increase",
        mediatool_cmd_uv_policy_increase,
        "Async increase value of numerical criterion (uv_increase NAME)" },
    { "uv_decrease",
        mediatool_cmd_uv_policy_decrease,
        "Async decrease value of numerical criterion (uv_decrease NAME)" },
    { "uv_include",
        mediatool_cmd_uv_policy_include,
        "Async include inclusive criterion values(uv_include NAME VALUE APPLY)" },
    { "uv_exclude",
        mediatool_cmd_uv_policy_exclude,
        "Async exclude inclusive criterion values(uv_exclude NAME VALUE APPLY)" },
    { "uv_contain",
        mediatool_cmd_uv_policy_contain,
        "Async check wether contain criterion values(uv_contain NAME VALUE)" },
    { "uv_request",
        mediatool_cmd_uv_focus_request,
        "Async request focus (uv_request SCENARIO)" },
    { "uv_takepic",
        mediatool_cmd_uv_take_picture,
        "Async take picture from camera" },
#endif /* CONFIG_LIBUV_EXTENSION */
    { "q",
        mediatool_cmd_quit,
        "Quit (q)" },
    { "help",
        NULL,
        "Show this message(help)" },

    { 0 },
};

static int mediatool_execute(mediatool_t* mediatool, char* buffer)
{
    char* argv[MEDIATOOL_MAX_ARGC] = { NULL };
    char* saveptr = NULL;
    int ret = 0;
    int argc;
    int x;

    argv[0] = strtok_r(buffer, " ", &saveptr);
    for (argc = 1; argc < MEDIATOOL_MAX_ARGC - 1; argc++) {
        argv[argc] = strtok_r(NULL, " ", &saveptr);
        if (argv[argc] == NULL)
            break;
    }

    if (!argv[0])
        return ret;

    /* Find the command in our cmd array */

    for (x = 0; g_mediatool_cmds[x].cmd; x++) {
        if (!strcmp(argv[0], "help")) {
            mediatool_cmd_help(g_mediatool_cmds);
            break;
        }

        if (!strcmp(argv[0], g_mediatool_cmds[x].cmd)) {
            ret = g_mediatool_cmds[x].pfunc(mediatool, argc, argv);
            if (ret < 0) {
                printf("cmd %s error %d\n", argv[0], ret);
                ret = 0;
            }

            if (g_mediatool_cmds[x].pfunc == mediatool_cmd_quit)
                ret = -1;

            break;
        }
    }

    if (g_mediatool_cmds[x].cmd == NULL) {
        printf("Unknown cmd: %s\n", argv[0]);
        mediatool_cmd_help(g_mediatool_cmds);
    }

    return ret;
}

#ifdef CONFIG_LIBUV_EXTENSION
static void mediatool_uvasyncq_close_cb(uv_handle_t* handle)
{
    printf("Bye-Bye!\n");
    uv_stop(uv_req_get_data((const uv_req_t*)handle));
}

static void mediatool_uvasyncq_cb(uv_async_queue_t* asyncq, void* data)
{
    mediatool_t* mediatool = asyncq->data;
    int ret;

    ret = mediatool_execute(mediatool, data);
    free(data);
    if (ret < 0) {
        uv_req_set_data((uv_req_t*)&mediatool->asyncq, &mediatool->loop);
        uv_close((uv_handle_t*)&mediatool->asyncq, mediatool_uvasyncq_close_cb);
    }
}

static void* mediatool_uvloop_thread(void* arg)
{
    mediatool_t* mediatool = arg;
    int ret;

    ret = uv_loop_init(&mediatool->loop);
    if (ret < 0)
        return NULL;

    mediatool->asyncq.data = arg;
    ret = uv_async_queue_init(&mediatool->loop, &mediatool->asyncq,
        mediatool_uvasyncq_cb);
    if (ret < 0)
        goto out;

    printf("[%s][%d] running\n", __func__, __LINE__);
    while (1) {
        ret = uv_run(&mediatool->loop, UV_RUN_DEFAULT);
        if (ret == 0)
            break;
    }

out:
    ret = uv_loop_close(&mediatool->loop);
    printf("[%s][%d] out:%d\n", __func__, __LINE__, ret);
    return NULL;
}

int main(int argc, char* argv[])
{
    mediatool_t mediatool;
    pthread_attr_t attr;
    char* buffer = NULL;
    pthread_t thread;
    int ret, len;

    memset(&mediatool, 0, sizeof(mediatool));
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, CONFIG_MEDIA_TOOL_STACKSIZE);
    ret = pthread_create(&thread, &attr, mediatool_uvloop_thread, &mediatool);
    if (ret < 0)
        goto out;

    usleep(1000); /* let uvloop run. */
    while (1) {
        printf("mediatool> ");
        fflush(stdout);

        if (!buffer) {
            buffer = malloc(CONFIG_NSH_LINELEN);
            assert(buffer);
        }

        len = readline_stream(buffer, CONFIG_NSH_LINELEN, stdin, stdout);
        if (len <= 0)
            continue;
        buffer[len] = '\0';

        if (buffer[0] == '!') {
#ifdef CONFIG_SYSTEM_SYSTEM
            system(buffer + 1);
#endif
            continue;
        }

        if (buffer[len - 1] == '\n')
            buffer[len - 1] = '\0';

        if (buffer[0] == '\0')
            continue;

        uv_async_queue_send(&mediatool.asyncq, buffer);
        if (!strcmp(buffer, "q"))
            break;

        buffer = NULL;
    }

out:
    pthread_join(thread, NULL);
    return 0;
}
#else /* CONFIG_LIBUV_EXTENSION */
int main(int argc, char* argv[])
{
    mediatool_t mediatool;
    int ret, len;
    char* buffer;

    memset(&mediatool, 0, sizeof(mediatool));
    buffer = malloc(CONFIG_NSH_LINELEN);
    if (!buffer)
        return -ENOMEM;

    while (1) {
        printf("mediatool> ");
        fflush(stdout);

        len = readline_stream(buffer, CONFIG_NSH_LINELEN, stdin, stdout);
        if (len <= 0)
            continue;
        buffer[len] = '\0';

        if (buffer[0] == '!') {
#ifdef CONFIG_SYSTEM_SYSTEM
            system(buffer + 1);
#endif
            continue;
        }

        if (buffer[len - 1] == '\n')
            buffer[len - 1] = '\0';

        if (buffer[0] == '\0')
            continue;

        ret = mediatool_execute(&mediatool, buffer);

        if (ret < 0) {
            printf("Bye-Bye!\n");
            break;
        }
    }

    free(buffer);
    return 0;
}

#endif /* CONFIG_LIBUV_EXTENSION */
