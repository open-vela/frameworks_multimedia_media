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
#include <poll.h>
#include <pthread.h>
#include <stdlib.h>
#include <system/readline.h>
#include <unistd.h>

#include <media_api.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MEDIATOOL_MAX_CHAIN 16
#define MEDIATOOL_MAX_ARGC 8
#define MEDIATOOL_PLAYER 1
#define MEDIATOOL_RECORDER 2
#define MEDIATOOL_SESSION 3
#define MEDIATOOL_FOCUS 4

#define GET_ARG_FUNC(out_type, arg)                  \
    static out_type get_##out_type##_arg(char* arg); \
    static out_type get_##out_type##_arg(char* arg)

#define GET_ARG(out_type, arg) \
    get_##out_type##_arg(arg)

#define CMD0(func)                                                                    \
    static int mediatool_cmd_##func##_exec(struct mediatool_s* media);                \
    static int mediatool_cmd_##func(struct mediatool_s* media, int argc, char** argv) \
    {                                                                                 \
        return mediatool_cmd_##func##_exec(media);                                    \
    }                                                                                 \
    static int mediatool_cmd_##func##_exec(struct mediatool_s* media)

#define CMD1(func, type1, arg1)                                                       \
    static int mediatool_cmd_##func##_exec(struct mediatool_s* media, type1 arg1);    \
    static int mediatool_cmd_##func(struct mediatool_s* media, int argc, char** argv) \
    {                                                                                 \
        type1 arg1;                                                                   \
        arg1 = (argc > 1) ? GET_ARG(type1, argv[1]) : 0;                              \
        return mediatool_cmd_##func##_exec(media, arg1);                              \
    }                                                                                 \
    static int mediatool_cmd_##func##_exec(struct mediatool_s* media, type1 arg1)

#define CMD2(func, type1, arg1, type2, arg2)                                                   \
    static int mediatool_cmd_##func##_exec(struct mediatool_s* media, type1 arg1, type2 arg2); \
    static int mediatool_cmd_##func(struct mediatool_s* media, int argc, char** argv)          \
    {                                                                                          \
        type1 arg1;                                                                            \
        type2 arg2;                                                                            \
        arg1 = (argc > 1) ? GET_ARG(type1, argv[1]) : 0;                                       \
        arg2 = (argc > 2) ? GET_ARG(type2, argv[2]) : 0;                                       \
        return mediatool_cmd_##func##_exec(media, arg1, arg2);                                 \
    }                                                                                          \
    static int mediatool_cmd_##func##_exec(struct mediatool_s* media, type1 arg1, type2 arg2)

#define CMD3(func, type1, arg1, type2, arg2, type3, arg3)                                                  \
    static int mediatool_cmd_##func##_exec(struct mediatool_s* media, type1 arg1, type2 arg2, type3 arg3); \
    static int mediatool_cmd_##func(struct mediatool_s* media, int argc, char** argv)                      \
    {                                                                                                      \
        type1 arg1;                                                                                        \
        type2 arg2;                                                                                        \
        type3 arg3;                                                                                        \
        arg1 = (argc > 1) ? GET_ARG(type1, argv[1]) : 0;                                                   \
        arg2 = (argc > 2) ? GET_ARG(type2, argv[2]) : 0;                                                   \
        arg3 = (argc > 3) ? GET_ARG(type3, argv[3]) : 0;                                                   \
        return mediatool_cmd_##func##_exec(media, arg1, arg2, arg3);                                       \
    }                                                                                                      \
    static int mediatool_cmd_##func##_exec(struct mediatool_s* media, type1 arg1, type2 arg2, type3 arg3)

#define CMD4(func, type1, arg1, type2, arg2, type3, arg3, type4, arg4)                                                 \
    static int mediatool_cmd_##func##_exec(struct mediatool_s* media, type1 arg1, type2 arg2, type3 arg3, type4 arg4); \
    static int mediatool_cmd_##func(struct mediatool_s* media, int argc, char** argv)                                  \
    {                                                                                                                  \
        type1 arg1;                                                                                                    \
        type2 arg2;                                                                                                    \
        type3 arg3;                                                                                                    \
        type4 arg4;                                                                                                    \
        arg1 = (argc > 1) ? GET_ARG(type1, argv[1]) : 0;                                                               \
        arg2 = (argc > 2) ? GET_ARG(type2, argv[2]) : 0;                                                               \
        arg3 = (argc > 3) ? GET_ARG(type3, argv[3]) : 0;                                                               \
        arg4 = (argc > 4) ? GET_ARG(type4, argv[4]) : 0;                                                               \
        return mediatool_cmd_##func##_exec(media, arg1, arg2, arg3, arg4);                                             \
    }                                                                                                                  \
    static int mediatool_cmd_##func##_exec(struct mediatool_s* media, type1 arg1, type2 arg2, type3 arg3, type4 arg4)

/****************************************************************************
 * Public Type Declarations
 ****************************************************************************/

struct mediatool_chain_s {
    int id;
    int type;
    void* handle;

    pthread_t thread;
    int fd;

    bool start;
    bool loop;

    bool direct;
    char* buf;
    int size;
};

struct mediatool_s {
    struct mediatool_chain_s chain[MEDIATOOL_MAX_CHAIN];
};

/****************************************************************************
 * Private Type Declarations
 ****************************************************************************/

typedef int (*mediatool_func)(struct mediatool_s* media, int argc, char** argv);

struct mediatool_cmd_s {
    const char* cmd; /* The command text */
    mediatool_func pfunc; /* Pointer to command handler */
    const char* help; /* The help text */
};

typedef char* string_t;

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

static void mediatool_event_callback(void* cookie, int event,
    int ret, const char* data)
{
    struct mediatool_chain_s* chain = cookie;
    char* str;

    if (event == MEDIA_EVENT_STARTED) {
        str = "MEDIA_EVENT_STARTED";
    } else if (event == MEDIA_EVENT_STOPPED) {
        str = "MEDIA_EVENT_STOPPED";
    } else if (event == MEDIA_EVENT_COMPLETED) {
        str = "MEDIA_EVENT_COMPLETED";
    } else if (event == MEDIA_EVENT_PREPARED) {
        str = "MEDIA_EVENT_PREPARED";
    } else if (event == MEDIA_EVENT_PAUSED) {
        str = "MEDIA_EVENT_PAUSED";
    } else if (event == MEDIA_EVENT_PREVED) {
        str = "MEDIA_EVENT_PREVED";
    } else if (event == MEDIA_EVENT_NEXTED) {
        str = "MEDIA_EVENT_NEXTED";
    } else if (event == MEDIA_EVENT_START) {
        str = "MEDIA_EVENT_START";
    } else if (event == MEDIA_EVENT_PAUSE) {
        str = "MEDIA_EVENT_PAUSE";
    } else if (event == MEDIA_EVENT_STOP) {
        str = "MEDIA_EVENT_STOP";
    } else if (event == MEDIA_EVENT_PREV) {
        str = "MEDIA_EVENT_PREV";
        media_player_notify(chain->handle, MEDIA_EVENT_PREVED, 0, NULL);
    } else if (event == MEDIA_EVENT_NEXT) {
        str = "MEDIA_EVENT_NEXT";
        media_player_notify(chain->handle, MEDIA_EVENT_NEXTED, 0, NULL);
    } else {
        str = "NORMAL EVENT";
    }

    printf("%s, id %d, event %s, event %d, ret %d, line %d\n",
        __func__, chain->id, str, event, ret, __LINE__);
}

static void mediatool_focus_callback(int suggestion, void* cookie)
{
    struct mediatool_chain_s* chain = cookie;
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

static void mediatool_common_stop_thread(struct mediatool_chain_s* chain)
{
    if (chain->thread) {

        pthread_join(chain->thread, NULL);
        free(chain->buf);
        close(chain->fd);

        chain->thread = 0;
        chain->buf = NULL;
        chain->fd = -1;
    }
}

static int mediatool_common_stop_inner(struct mediatool_chain_s* chain)
{
    int ret = 0;

    switch (chain->type) {
    case MEDIATOOL_PLAYER:
        ret = media_player_stop(chain->handle);
        break;

    case MEDIATOOL_RECORDER:
        ret = media_recorder_stop(chain->handle);
        break;

    case MEDIATOOL_SESSION:
        ret = media_session_stop(chain->handle);
        return ret;
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
        if (!media->chain[i].handle)
            break;
    }

    if (i == MEDIATOOL_MAX_CHAIN)
        return -ENOMEM;

    media->chain[i].id = i;
    media->chain[i].handle = media_player_open(stream_type);
    if (!media->chain[i].handle) {
        printf("media_player_open error\n");
        return -EINVAL;
    }

    ret = media_player_set_event_callback(media->chain[i].handle,
        &media->chain[i],
        mediatool_event_callback);
    assert(!ret);

    media->chain[i].type = MEDIATOOL_PLAYER;

    printf("player ID %ld\n", i);

    return 0;
}

CMD1(recorder_open, string_t, stream_type)
{
    long int i;
    int ret;

    for (i = 0; i < MEDIATOOL_MAX_CHAIN; i++) {
        if (!media->chain[i].handle)
            break;
    }

    if (i == MEDIATOOL_MAX_CHAIN)
        return -ENOMEM;

    media->chain[i].id = i;
    media->chain[i].handle = media_recorder_open(stream_type);
    if (!media->chain[i].handle) {
        printf("media_recorder_open error\n");
        return -EINVAL;
    }

    ret = media_recorder_set_event_callback(media->chain[i].handle,
        &media->chain[i],
        mediatool_event_callback);
    assert(!ret);

    media->chain[i].type = MEDIATOOL_RECORDER;

    printf("recorder ID %ld\n", i);

    return 0;
}

CMD1(session_open, string_t, stream_type)
{
    long int i;
    int ret;

    for (i = 0; i < MEDIATOOL_MAX_CHAIN; i++) {
        if (!media->chain[i].handle)
            break;
    }

    if (i == MEDIATOOL_MAX_CHAIN)
        return -ENOMEM;

    media->chain[i].id = i;
    media->chain[i].handle = media_session_open(stream_type);
    if (!media->chain[i].handle) {
        printf("media_session_open error\n");
        return -EINVAL;
    }

    ret = media_session_set_event_callback(media->chain[i].handle,
        &media->chain[i],
        mediatool_event_callback);
    assert(!ret);

    media->chain[i].type = MEDIATOOL_SESSION;

    printf("session ID %ld\n", i);

    return 0;
}

CMD2(close, int, id, int, pending_stop)
{
    int ret = -EINVAL;

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !media->chain[id].handle)
        return -EINVAL;

    switch (media->chain[id].type) {
    case MEDIATOOL_PLAYER:
        mediatool_common_stop_inner(&media->chain[id]);
        ret = media_player_close(media->chain[id].handle, pending_stop);
        break;

    case MEDIATOOL_RECORDER:
        mediatool_common_stop_inner(&media->chain[id]);
        ret = media_recorder_close(media->chain[id].handle);
        break;

    case MEDIATOOL_SESSION:
        ret = media_session_close(media->chain[id].handle);
        break;

    case MEDIATOOL_FOCUS:
        ret = media_focus_abandon(media->chain[id].handle);
        break;
    }

    media->chain[id].handle = NULL;

    return ret;
}

CMD1(reset, int, id)
{
    int ret;

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !media->chain[id].handle)
        return -EINVAL;

    switch (media->chain[id].type) {
    case MEDIATOOL_PLAYER:
        ret = media_player_reset(media->chain[id].handle);
        break;

    case MEDIATOOL_RECORDER:
        ret = media_recorder_reset(media->chain[id].handle);
        break;

    default:
        return 0;
    }

    mediatool_common_stop_thread(&media->chain[id]);

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
    struct mediatool_chain_s* chain = arg;
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
    bool url_mode = false;
    bool direct = false;
    pthread_t thread;
    int ret = 0;

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !media->chain[id].handle)
        return -EINVAL;

    if (!mode || !path)
        return -EINVAL;

    if (!strcmp(mode, "url"))
        url_mode = true;
    else if (!strcmp(mode, "direct"))
        direct = true;

    switch (media->chain[id].type) {
    case MEDIATOOL_PLAYER:
        ret = media_player_prepare(media->chain[id].handle, url_mode ? path : NULL, options);
        break;

    case MEDIATOOL_RECORDER:
        ret = media_recorder_prepare(media->chain[id].handle, url_mode ? path : NULL, options);
        break;

    default:
        return 0;
    }

    if (ret < 0)
        return ret;

    if (!url_mode) {
        if (media->chain[id].thread) {
            printf("already prepare, can't prepare twice\n");
            return -EPERM;
        }

        if (media->chain[id].type == MEDIATOOL_RECORDER)
            unlink(path);

        media->chain[id].fd = open(path, media->chain[id].type == MEDIATOOL_PLAYER ? O_RDONLY : O_CREAT | O_RDWR);
        if (media->chain[id].fd < 0) {
            printf("buffer mode, file can't open\n");
            return -EINVAL;
        }

        media->chain[id].direct = direct;
        media->chain[id].size = 512;
        media->chain[id].buf = malloc(media->chain[id].size);
        assert(media->chain[id].buf);

        ret = pthread_create(&thread, NULL, mediatool_buffer_thread, &media->chain[id]);
        assert(!ret);

        pthread_setname_np(thread, "mediatool_file");
        media->chain[id].thread = thread;
    }

    return ret;
}

CMD1(start, int, id)
{
    int ret = -EINVAL;

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !media->chain[id].handle)
        return -EINVAL;

    switch (media->chain[id].type) {
    case MEDIATOOL_PLAYER:
        ret = media_player_start(media->chain[id].handle);
        break;

    case MEDIATOOL_RECORDER:
        ret = media_recorder_start(media->chain[id].handle);
        break;

    case MEDIATOOL_SESSION:
        ret = media_session_start(media->chain[id].handle);
        break;
    }

    return ret;
}

CMD1(stop, int, id)
{
    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !media->chain[id].handle)
        return -EINVAL;

    return mediatool_common_stop_inner(&media->chain[id]);
}

CMD1(pause, int, id)
{
    int ret = 0;

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !media->chain[id].handle)
        return -EINVAL;

    switch (media->chain[id].type) {
    case MEDIATOOL_PLAYER:
        ret = media_player_pause(media->chain[id].handle);
        break;

    case MEDIATOOL_RECORDER:
        ret = media_recorder_pause(media->chain[id].handle);
        break;

    case MEDIATOOL_SESSION:
        ret = media_session_pause(media->chain[id].handle);
        break;
    }

    return ret;
}

CMD2(volume, int, id, string_t, volume_cmd)
{
    float volume_f;
    int volume_i;
    int ret = 0;
    char* ptr;

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !media->chain[id].handle)
        return -EINVAL;

    if (!volume_cmd)
        return -EINVAL;

    switch (media->chain[id].type) {
    case MEDIATOOL_PLAYER:
        if ((ptr = strchr(volume_cmd, '?'))) {
            ret = media_player_get_volume(media->chain[id].handle, &volume_f);
            printf("ID %d, get volume %f\n", id, volume_f);
        } else {
            ret = media_player_set_volume(media->chain[id].handle, strtof(volume_cmd, NULL));
            printf("ID %d, set volume %f\n", id, strtof(volume_cmd, NULL));
        }
        break;

    case MEDIATOOL_SESSION:
        if ((ptr = strchr(volume_cmd, '?'))) {
            ret = media_session_get_volume(media->chain[id].handle, &volume_i);
            printf("ID %d, get volume %d\n", id, volume_i);
        } else if ((ptr = strchr(volume_cmd, '+'))) {
            ret = media_session_get_volume(media->chain[id].handle, &volume_i);
            ret = media_session_increase_volume(media->chain[id].handle);
            printf("ID %d, increase volume %d++\n", id, volume_i);
        } else if ((ptr = strchr(volume_cmd, '-'))) {
            ret = media_session_get_volume(media->chain[id].handle, &volume_i);
            ret = media_session_decrease_volume(media->chain[id].handle);
            printf("ID %d, decrease volume %d--\n", id, volume_i);
        } else {
            ret = media_session_set_volume(media->chain[id].handle, strtol(volume_cmd, NULL, 10));
            printf("ID %d, set volume %d\n", id, (int)strtol(volume_cmd, NULL, 10));
        }
        break;

    case MEDIATOOL_RECORDER:
        return 0;
    }

    return ret;
}

CMD2(loop, int, id, int, isloop)
{
    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !media->chain[id].handle)
        return -EINVAL;

    if (media->chain[id].type != MEDIATOOL_PLAYER)
        return 0;

    return media_player_set_looping(media->chain[id].handle, isloop);
}

CMD2(seek, int, id, int, msec)
{
    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !media->chain[id].handle)
        return -EINVAL;

    if (media->chain[id].type != MEDIATOOL_PLAYER)
        return 0;

    return media_player_seek(media->chain[id].handle, msec);
}

CMD1(position, int, id)
{
    unsigned int position;
    int ret;

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !media->chain[id].handle)
        return -EINVAL;

    switch (media->chain[id].type) {
    case MEDIATOOL_PLAYER:
        ret = media_player_get_position(media->chain[id].handle, &position);
        break;

    case MEDIATOOL_SESSION:
        ret = media_session_get_position(media->chain[id].handle, &position);
        break;

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

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !media->chain[id].handle)
        return -EINVAL;

    switch (media->chain[id].type) {
    case MEDIATOOL_PLAYER:
        ret = media_player_get_duration(media->chain[id].handle, &duration);
        break;

    case MEDIATOOL_SESSION:
        ret = media_session_get_duration(media->chain[id].handle, &duration);
        break;

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

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !media->chain[id].handle)
        return -EINVAL;

    if (media->chain[id].type != MEDIATOOL_PLAYER)
        return 0;

    ret = media_player_is_playing(media->chain[id].handle);
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

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !media->chain[id].handle)
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

    ret = media_player_prepare(media->chain[id].handle, NULL, MEDIA_TONE_DTMF_FORMAT);
    if (ret < 0)
        goto out;

    if (direct) {
        fd = media_player_get_socket(media->chain[id].handle);
        if (fd < 0)
            goto out;

        if (fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK) < 0)
            goto out;

        ret = mediatool_process_data(fd, true, buffer, buffer_size);
    } else
        ret = media_player_write_data(media->chain[id].handle, buffer, buffer_size);

    if (ret == buffer_size) {
        media_player_close_socket(media->chain[id].handle);
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
    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !media->chain[id].handle)
        return -EINVAL;

    if (media->chain[id].type != MEDIATOOL_SESSION)
        return 0;

    return media_session_prev_song(media->chain[id].handle);
}

CMD1(nextsong, int, id)
{
    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !media->chain[id].handle)
        return -EINVAL;

    if (media->chain[id].type != MEDIATOOL_SESSION)
        return 0;

    return media_session_next_song(media->chain[id].handle);
}

CMD3(take_picture, string_t, filtername, string_t, filename, int, number)
{
    return media_recorder_take_picture(filtername, filename, number, mediatool_event_callback, media->chain);
}

CMD3(send, string_t, target, string_t, cmd, string_t, arg)
{
    return media_process_command(target, cmd, arg, NULL, 0);
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

CMD1(focus_request, string_t, name)
{
    int suggestion;
    long int i;

    for (i = 0; i < MEDIATOOL_MAX_CHAIN; i++) {
        if (!media->chain[i].handle)
            break;
    }

    if (i == MEDIATOOL_MAX_CHAIN)
        return -ENOMEM;

    media->chain[i].id = i;
    media->chain[i].handle = media_focus_request(&suggestion, name,
        mediatool_focus_callback, &media->chain[i]);
    if (!media->chain[i].handle) {
        printf("media_player_request failed\n");
        return 0;
    }

    media->chain[i].type = MEDIATOOL_FOCUS;
    printf("focus ID %ld, first suggestion %d\n", i, suggestion);
    return 0;
}

CMD1(focus_abandon, int, id)
{
    int ret = -EINVAL;

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !media->chain[id].handle)
        return -EINVAL;

    if (media->chain[id].type != MEDIATOOL_FOCUS)
        return 0;

    ret = media_focus_abandon(media->chain[id].handle);
    if (ret >= 0) {
        printf("abandon focus ID %d\n", id);
        media->chain[id].handle = NULL;
    }

    return ret;
}

CMD0(quit)
{
    int i;

    for (i = 0; i < MEDIATOOL_MAX_CHAIN; i++) {
        if (media->chain[i].handle)
            mediatool_cmd_close(media, i, 0);
    }

    return 0;
}

static int mediatool_cmd_help(const struct mediatool_cmd_s cmds[])
{
    int i;

    for (i = 0; cmds[i].cmd; i++)
        printf("%-16s %s\n", cmds[i].cmd, cmds[i].help);

    return 0;
}

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct mediatool_cmd_s g_mediatool_cmds[] = {
    { "open",
        mediatool_cmd_player_open,
        "Create player channel return ID (open [policy-phrase/filter-name])" },
    { "copen",
        mediatool_cmd_recorder_open,
        "Create recorder channel return ID (copen [policy-phrase])" },
    { "sopen",
        mediatool_cmd_session_open,
        "Create session channel return ID (sopen policy-phrase)" },
    { "close",
        mediatool_cmd_close,
        "Destroy player/recorder/session channel (close ID [pending_stop(1/0)])" },
    { "reset",
        mediatool_cmd_reset,
        "Reset player/recorder channel (reset ID)" },
    { "prepare",
        mediatool_cmd_prepare,
        "Set player/recorder prepare (prepare ID url/buffer/direct url [options])" },
    { "start",
        mediatool_cmd_start,
        "Set player/recorder/session start (start ID)" },
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
        "Take picture from camera" },
    { "send",
        mediatool_cmd_send,
        "Send cmd to graph. PS:loglevel INFO:32 VERBOSE:40 DEBUG:48 TRACE:56" },
    { "dump",
        mediatool_cmd_dump,
        "Dump graph and policy" },
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
    { "request",
        mediatool_cmd_focus_request,
        "Request media focus(request NAME)" },
    { "abandon",
        mediatool_cmd_focus_abandon,
        "Abandon media focus(abandon ID)" },
    { "q",
        mediatool_cmd_quit,
        "Quit (q)" },
    { "help",
        NULL,
        "Show this message(help)" },

    { 0 },
};

static int mediatool_execute(struct mediatool_s* media, char* buffer)
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

    /* Find the command in our cmd array */

    for (x = 0; g_mediatool_cmds[x].cmd; x++) {
        if (!strcmp(argv[0], "help")) {
            mediatool_cmd_help(g_mediatool_cmds);
            break;
        }

        if (!strcmp(argv[0], g_mediatool_cmds[x].cmd)) {
            ret = g_mediatool_cmds[x].pfunc(media, argc, argv);
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

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, char* argv[])
{
    struct mediatool_s mediatool = { 0 };
    int ret, len;
    char* buffer;

    buffer = malloc(CONFIG_NSH_LINELEN);
    if (!buffer)
        return -ENOMEM;

    while (1) {
        printf("mediatool> ");
        fflush(stdout);

        len = readline(buffer, CONFIG_NSH_LINELEN, stdin, stdout);
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
