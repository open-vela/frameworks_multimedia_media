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
#define MEDIATOOL_FILE_MAX 64
#define MEDIATOOL_PLAYER 1
#define MEDIATOOL_RECORDER 2
#define MEDIATOOL_SESSION 3
#define MEDIATOOL_FOCUS 4

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
 * Private Function Prototypes
 ****************************************************************************/

static int mediatool_player_cmd_open(struct mediatool_s* media, char* pargs);
static int mediatool_recorder_cmd_open(struct mediatool_s* media, char* pargs);
static int mediatool_recorder_cmd_take_picture(struct mediatool_s* media, char* pargs);
static int mediatool_session_cmd_open(struct mediatool_s* media, char* pargs);
static int mediatool_common_cmd_close(struct mediatool_s* media, char* pargs);
static int mediatool_common_cmd_reset(struct mediatool_s* media, char* pargs);
static int mediatool_common_cmd_prepare(struct mediatool_s* media, char* pargs);
static int mediatool_common_cmd_start(struct mediatool_s* media, char* pargs);
static int mediatool_common_cmd_stop(struct mediatool_s* media, char* pargs);
static int mediatool_player_cmd_pause(struct mediatool_s* media, char* pargs);
static int mediatool_player_cmd_volume(struct mediatool_s* media, char* pargs);
static int mediatool_player_cmd_loop(struct mediatool_s* media, char* pargs);
static int mediatool_player_cmd_seek(struct mediatool_s* media, char* pargs);
static int mediatool_player_cmd_position(struct mediatool_s* media, char* pargs);
static int mediatool_player_cmd_duration(struct mediatool_s* media, char* pargs);
static int mediatool_player_cmd_isplaying(struct mediatool_s* media, char* pargs);
static int mediatool_session_cmd_prevsong(struct mediatool_s* media, char* pargs);
static int mediatool_session_cmd_nextsong(struct mediatool_s* media, char* pargs);
static int mediatool_common_stop_inner(struct mediatool_chain_s* chain);
static int mediatool_common_cmd_send(struct mediatool_s* media, char* pargs);
static int mediatool_server_cmd_dump(struct mediatool_s* media, char* pargs);
static int mediatool_policy_cmd_setint(struct mediatool_s* media, char* pargs);
static int mediatool_policy_cmd_getint(struct mediatool_s* media, char* pargs);
static int mediatool_policy_cmd_setstring(struct mediatool_s* media, char* pargs);
static int mediatool_policy_cmd_getstring(struct mediatool_s* media, char* pargs);
static int mediatool_policy_cmd_include(struct mediatool_s* media, char* pargs);
static int mediatool_policy_cmd_exclude(struct mediatool_s* media, char* pargs);
static int mediatool_policy_cmd_contain(struct mediatool_s* media, char* pargs);
static int mediatool_policy_cmd_increase(struct mediatool_s* media, char* pargs);
static int mediatool_policy_cmd_decrease(struct mediatool_s* media, char* pargs);
static int mediatool_focus_cmd_request(struct mediatool_s* media, char* pargs);
static int mediatool_focus_cmd_abandon(struct mediatool_s* media, char* pargs);
static int mediatool_common_cmd_quit(struct mediatool_s* media, char* pargs);
static int mediatool_common_cmd_help(struct mediatool_s* media, char* pargs);

/****************************************************************************
 * Private Type Declarations
 ****************************************************************************/

typedef int (*mediatool_func)(struct mediatool_s* media, char* pargs);

struct mediatool_cmd_s {
    const char* cmd; /* The command text */
    mediatool_func pfunc; /* Pointer to command handler */
    const char* help; /* The help text */
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct mediatool_s g_mediatool;

static struct mediatool_cmd_s g_mediatool_cmds[] = {
    { "open",
        mediatool_player_cmd_open,
        "Create player channel return ID (open [policy-phrase/filter-name])" },
    { "copen",
        mediatool_recorder_cmd_open,
        "Create recorder channel return ID (copen [policy-phrase])" },
    { "sopen",
        mediatool_session_cmd_open,
        "Create session channel return ID (sopen policy-phrase)" },
    { "close",
        mediatool_common_cmd_close,
        "Destroy player/recorder/session channel (close ID)" },
    { "reset",
        mediatool_common_cmd_reset,
        "Reset player/recorder channel (reset ID)" },
    { "prepare",
        mediatool_common_cmd_prepare,
        "Set player/recorder prepare (start ID url/buffer options)" },
    { "start",
        mediatool_common_cmd_start,
        "Set player/recorder/session start (start ID)" },
    { "stop",
        mediatool_common_cmd_stop,
        "Set player/recorder/session stop (stop ID)" },
    { "pause",
        mediatool_player_cmd_pause,
        "Set player/recorder/session pause (pause ID)" },
    { "volume",
        mediatool_player_cmd_volume,
        "Set/Get player/session volume (volume ID ?/+/-/volume)" },
    { "loop",
        mediatool_player_cmd_loop,
        "Set/Get player loop (loop ID 1/0)" },
    { "seek",
        mediatool_player_cmd_seek,
        "Set player seek (seek ID time)" },
    { "position",
        mediatool_player_cmd_position,
        "Get player position time ms(position ID)" },
    { "duration",
        mediatool_player_cmd_duration,
        "Get player duration time ms(duration ID)" },
    { "isplay",
        mediatool_player_cmd_isplaying,
        "Get position is playing or not(isplay ID)" },
    { "prev",
        mediatool_session_cmd_prevsong,
        "To play previous song in player list(prev ID)" },
    { "next",
        mediatool_session_cmd_nextsong,
        "To play next song in player list(next ID)" },
    { "takepic",
        mediatool_recorder_cmd_take_picture,
        "take picture from camera" },
    { "send",
        mediatool_common_cmd_send,
        "Send cmd to graph. PS:loglevel INFO:32 VERBOSE:40 DEBUG:48 TRACE:56" },
    { "dump",
        mediatool_server_cmd_dump,
        "Dump graph and policy" },
    { "setint",
        mediatool_policy_cmd_setint,
        "set criterion value with integer(setint NAME VALUE APPLY)" },
    { "getint",
        mediatool_policy_cmd_getint,
        "get criterion value in integer(getint NAME)" },
    { "setstring",
        mediatool_policy_cmd_setstring,
        "set criterion value with string(setstring NAME VALUE APPLY)" },
    { "getstring",
        mediatool_policy_cmd_getstring,
        "get criterion value in string(getstring NAME)" },
    { "include",
        mediatool_policy_cmd_include,
        "include inclusive criterion values(include NAME VALUE APPLY)" },
    { "exclude",
        mediatool_policy_cmd_exclude,
        "exclude inclusive criterion values(exclude NAME VALUE APPLY)" },
    { "contain",
        mediatool_policy_cmd_contain,
        "check wether contain criterion values(contain NAME VALUE)" },
    { "increase",
        mediatool_policy_cmd_increase,
        "increase criterion value by one(increase NAME APPLY)" },
    { "decrease",
        mediatool_policy_cmd_decrease,
        "decrease criterion value by one(decrease NAME APPLY)" },
    { "request",
        mediatool_focus_cmd_request,
        "request media focus(request NAME)" },
    { "abandon",
        mediatool_focus_cmd_abandon,
        "abandon media focus(abandon ID)" },
    { "q",
        mediatool_common_cmd_quit,
        "Quit (q)" },
    { "help",
        mediatool_common_cmd_help,
        "Show this message (help)" },
    { 0 },
};

/****************************************************************************
 * Private Function
 ****************************************************************************/

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

static int mediatool_player_cmd_open(struct mediatool_s* media, char* pargs)
{
    long int i;
    int ret;

    if (pargs && !strlen(pargs))
        pargs = NULL;

    if (pargs && isdigit(pargs[0])) {
        i = strtol(pargs, NULL, 10);

        if (media->chain[i].handle)
            return -EBUSY;

        pargs = NULL;
    } else {
        for (i = 0; i < MEDIATOOL_MAX_CHAIN; i++) {
            if (!media->chain[i].handle)
                break;
        }

        if (i == MEDIATOOL_MAX_CHAIN)
            return -ENOMEM;
    }

    media->chain[i].id = i;
    media->chain[i].handle = media_player_open(pargs);
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

static int mediatool_recorder_cmd_open(struct mediatool_s* media, char* pargs)
{
    long int i;
    int ret;

    if (pargs && !strlen(pargs))
        pargs = NULL;

    if (pargs && isdigit(pargs[0])) {
        i = strtol(pargs, NULL, 10);

        if (media->chain[i].handle)
            return -EBUSY;

        pargs = NULL;
    } else {
        for (i = 0; i < MEDIATOOL_MAX_CHAIN; i++) {
            if (!media->chain[i].handle)
                break;
        }

        if (i == MEDIATOOL_MAX_CHAIN)
            return -ENOMEM;
    }

    media->chain[i].id = i;
    media->chain[i].handle = media_recorder_open(pargs);
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

static int mediatool_session_cmd_open(struct mediatool_s* media, char* pargs)
{
    long int i;
    int ret;

    if (pargs && !strlen(pargs))
        pargs = NULL;

    if (pargs && isdigit(pargs[0])) {
        i = strtol(pargs, NULL, 10);

        if (media->chain[i].handle)
            return -EBUSY;

        pargs = NULL;
    } else {
        for (i = 0; i < MEDIATOOL_MAX_CHAIN; i++) {
            if (!media->chain[i].handle)
                break;
        }

        if (i == MEDIATOOL_MAX_CHAIN)
            return -ENOMEM;
    }

    media->chain[i].id = i;
    media->chain[i].handle = media_session_open(pargs);
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

static int mediatool_common_cmd_close(struct mediatool_s* media, char* pargs)
{
    int id;
    int pending_stop;
    int ret = -EINVAL;

    if (!strlen(pargs))
        return ret;

    sscanf(pargs, "%d %d", &id, &pending_stop);

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !media->chain[id].handle)
        return ret;

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

static int mediatool_common_cmd_reset(struct mediatool_s* media, char* pargs)
{
    long int id;
    int ret;

    if (!strlen(pargs))
        return -EINVAL;

    id = strtol(pargs, NULL, 10);

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

static int mediatool_common_cmd_prepare(struct mediatool_s* media, char* pargs)
{
    bool url_mode = false;
    bool direct = false;
    char *ptr, *file;
    pthread_t thread;
    long int id;
    int ret = 0;

    if (!strlen(pargs))
        return -EINVAL;

    ptr = strchr(pargs, ' ');
    if (!ptr)
        return -EINVAL;

    *ptr++ = 0;

    id = strtol(pargs, NULL, 10);

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !media->chain[id].handle)
        return -EINVAL;

    file = ptr;
    ptr = strchr(ptr, ' ');
    if (!ptr)
        return -EINVAL;

    *ptr++ = 0;

    if (!strcmp(file, "url"))
        url_mode = true;
    else if (!strcmp(file, "direct"))
        direct = true;

    file = ptr;
    ptr = strchr(ptr, ' ');
    if (ptr)
        *ptr++ = 0;

    switch (media->chain[id].type) {
    case MEDIATOOL_PLAYER:
        ret = media_player_prepare(media->chain[id].handle, url_mode ? file : NULL, ptr);
        break;

    case MEDIATOOL_RECORDER:
        ret = media_recorder_prepare(media->chain[id].handle, url_mode ? file : NULL, ptr);
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
            unlink(file);

        media->chain[id].fd = open(file, media->chain[id].type == MEDIATOOL_PLAYER ? O_RDONLY : O_CREAT | O_RDWR);
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

static int mediatool_common_cmd_start(struct mediatool_s* media, char* pargs)
{
    int ret = -EINVAL;
    long int id;

    if (!strlen(pargs))
        return ret;

    id = strtol(pargs, NULL, 10);

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !media->chain[id].handle)
        return ret;

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

static int mediatool_common_cmd_stop(struct mediatool_s* media, char* pargs)
{
    long int id;

    if (!strlen(pargs))
        return -EINVAL;

    id = strtol(pargs, NULL, 10);

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !media->chain[id].handle)
        return -EINVAL;

    return mediatool_common_stop_inner(&media->chain[id]);
}

static int mediatool_player_cmd_pause(struct mediatool_s* media, char* pargs)
{
    long int id;
    int ret = 0;

    if (!strlen(pargs))
        return -EINVAL;

    id = strtol(pargs, NULL, 10);

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

static int mediatool_player_cmd_volume(struct mediatool_s* media, char* pargs)
{
    float volume_f;
    int volume_i;
    int ret = 0;
    char* ptr;
    int id;

    if (!strlen(pargs))
        return -EINVAL;

    sscanf(pargs, "%d", &id);

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !media->chain[id].handle)
        return -EINVAL;

    switch (media->chain[id].type) {
    case MEDIATOOL_PLAYER:
        if ((ptr = strchr(pargs, '?'))) {
            ret = media_player_get_volume(media->chain[id].handle, &volume_f);
            printf("ID %d, get volume %f\n", id, volume_f);
        } else {
            sscanf(pargs, "%d %f", &id, &volume_f);
            ret = media_player_set_volume(media->chain[id].handle, volume_f);
            printf("ID %d, set volume %f\n", id, volume_f);
        }
        break;

    case MEDIATOOL_SESSION:
        if ((ptr = strchr(pargs, '?'))) {
            ret = media_session_get_volume(media->chain[id].handle, &volume_i);
            printf("ID %d, get volume %d\n", id, volume_i);
        } else if ((ptr = strchr(pargs, '+'))) {
            ret = media_session_get_volume(media->chain[id].handle, &volume_i);
            ret = media_session_increase_volume(media->chain[id].handle);
            printf("ID %d, increase volume %d++\n", id, volume_i);
        } else if ((ptr = strchr(pargs, '-'))) {
            ret = media_session_get_volume(media->chain[id].handle, &volume_i);
            ret = media_session_decrease_volume(media->chain[id].handle);
            printf("ID %d, decrease volume %d--\n", id, volume_i);
        } else {
            sscanf(pargs, "%d %d", &id, &volume_i);
            ret = media_session_set_volume(media->chain[id].handle, volume_i);
            printf("ID %d, set volume %d\n", id, volume_i);
        }
        break;

    case MEDIATOOL_RECORDER:
        return 0;
    }

    return ret;
}

static int mediatool_player_cmd_loop(struct mediatool_s* media, char* pargs)
{
    int loop;
    int id;

    if (!strlen(pargs))
        return -EINVAL;

    sscanf(pargs, "%d %d", &id, &loop);

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !media->chain[id].handle)
        return -EINVAL;

    if (media->chain[id].type != MEDIATOOL_PLAYER)
        return 0;

    return media_player_set_looping(media->chain[id].handle, loop);
}

static int mediatool_player_cmd_seek(struct mediatool_s* media, char* pargs)
{
    unsigned int seek;
    int id;

    if (!strlen(pargs))
        return -EINVAL;

    sscanf(pargs, "%d %d", &id, &seek);

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !media->chain[id].handle)
        return -EINVAL;

    if (media->chain[id].type != MEDIATOOL_PLAYER)
        return 0;

    return media_player_seek(media->chain[id].handle, seek);
}

static int mediatool_player_cmd_position(struct mediatool_s* media, char* pargs)
{
    unsigned int position;
    long int id;
    int ret;

    if (!strlen(pargs))
        return -EINVAL;

    id = strtol(pargs, NULL, 0);

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

static int mediatool_player_cmd_duration(struct mediatool_s* media, char* pargs)
{
    unsigned int duration;
    long int id;
    int ret;

    if (!strlen(pargs))
        return -EINVAL;

    id = strtol(pargs, NULL, 10);

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

static int mediatool_player_cmd_isplaying(struct mediatool_s* media, char* pargs)
{
    long int id;
    int ret;

    if (!strlen(pargs))
        return -EINVAL;

    id = strtol(pargs, NULL, 10);

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

static int mediatool_session_cmd_prevsong(struct mediatool_s* media, char* pargs)
{
    int ret = -EINVAL;
    long int id;

    if (!strlen(pargs))
        return ret;

    id = strtol(pargs, NULL, 10);

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !media->chain[id].handle)
        return ret;

    if (media->chain[id].type != MEDIATOOL_SESSION)
        return 0;

    return media_session_prev_song(media->chain[id].handle);
}

static int mediatool_session_cmd_nextsong(struct mediatool_s* media, char* pargs)
{
    int ret = -EINVAL;
    long int id;

    if (!strlen(pargs))
        return ret;

    id = strtol(pargs, NULL, 10);

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !media->chain[id].handle)
        return ret;

    if (media->chain[id].type != MEDIATOOL_SESSION)
        return 0;

    return media_session_next_song(media->chain[id].handle);
}

static int mediatool_recorder_cmd_take_picture(struct mediatool_s* media, char* pargs)
{
    char* filtername;
    char* filename;
    size_t number;

    if (!strlen(pargs))
        return -EINVAL;

    /* pargs should look like "PictureSink /DATA/IMG_%d.jpg 3" */
    filtername = strtok_r(pargs, " ", &pargs);
    if (!filtername)
        return -EINVAL;

    filename = strtok_r(NULL, " ", &pargs);
    if (!filename)
        return -EINVAL;

    number = strtoul(pargs, NULL, 10);

    return media_recorder_take_picture(filtername, filename, number);
}

static int mediatool_common_cmd_send(struct mediatool_s* media, char* pargs)
{
    char* target;
    char* cmd;
    if (!strlen(pargs))
        return -EINVAL;
    target = pargs;
    pargs = strchr(pargs, ' ');
    if (!pargs)
        return -EINVAL;
    *pargs = 0;
    pargs++;
    cmd = pargs;
    pargs = strchr(pargs, ' ');
    if (pargs) {
        *pargs = 0;
        pargs++;
    }

    return media_process_command(target, cmd, pargs, NULL, 0);
}

static int mediatool_server_cmd_dump(struct mediatool_s* media, char* pargs)
{
    /* pargs not used in neither graph_dump nor policy_dump,
     * so let's make it simple, just "dump".
     */
    media_policy_dump(pargs);
    media_graph_dump(pargs);
    media_focus_dump(pargs);

    return 0;
}

static int mediatool_policy_cmd_setint(struct mediatool_s* media, char* pargs)
{
    char* name;
    char* value;
    char* apply;

    if (!strlen(pargs))
        return -EINVAL;
    name = pargs;

    pargs = strchr(pargs, ' ');
    if (!pargs)
        return -EINVAL;
    *pargs = 0;
    pargs++;
    value = pargs;

    pargs = strchr(pargs, ' ');
    if (!pargs)
        return -EINVAL;
    *pargs = 0;
    pargs++;
    apply = pargs;

    return media_policy_set_int(name, atoi(value), atoi(apply));
}

static int mediatool_policy_cmd_getint(struct mediatool_s* media, char* pargs)
{
    char* name;
    int value;
    int ret;

    if (!strlen(pargs))
        return -EINVAL;
    name = pargs;

    ret = media_policy_get_int(name, &value);
    if (ret < 0)
        return -EINVAL;

    printf("get criterion %s integer value = %d\n", name, value);

    return 0;
}

static int mediatool_policy_cmd_setstring(struct mediatool_s* media, char* pargs)
{
    char* name;
    char* value;
    char* apply;

    if (!strlen(pargs))
        return -EINVAL;
    name = pargs;

    pargs = strchr(pargs, ' ');
    if (!pargs)
        return -EINVAL;
    *pargs = 0;
    pargs++;
    value = pargs;

    pargs = strchr(pargs, ' ');
    if (!pargs)
        return -EINVAL;
    *pargs = 0;
    pargs++;
    apply = pargs;

    return media_policy_set_string(name, value, atoi(apply));
}

static int mediatool_policy_cmd_getstring(struct mediatool_s* media, char* pargs)
{
    char* name;
    char value[64];
    int ret;

    if (!strlen(pargs))
        return -EINVAL;
    name = pargs;

    ret = media_policy_get_string(name, value, sizeof(value));
    if (ret < 0)
        return -EINVAL;

    printf("get criterion %s string value = '%s'\n", name, value);

    return 0;
}

static int mediatool_policy_cmd_include(struct mediatool_s* media, char* pargs)
{
    char* name;
    char* values;
    char* apply;

    if (!strlen(pargs))
        return -EINVAL;
    name = pargs;

    pargs = strchr(pargs, ' ');
    if (!pargs)
        return -EINVAL;
    *pargs = 0;
    pargs++;
    values = pargs;

    pargs = strchr(pargs, ' ');
    if (!pargs)
        return -EINVAL;
    *pargs = 0;
    pargs++;
    apply = pargs;

    return media_policy_include(name, values, atoi(apply));
}

static int mediatool_policy_cmd_exclude(struct mediatool_s* media, char* pargs)
{
    char* name;
    char* values;
    char* apply;

    if (!strlen(pargs))
        return -EINVAL;
    name = pargs;

    pargs = strchr(pargs, ' ');
    if (!pargs)
        return -EINVAL;
    *pargs = 0;
    pargs++;
    values = pargs;

    pargs = strchr(pargs, ' ');
    if (!pargs)
        return -EINVAL;
    *pargs = 0;
    pargs++;
    apply = pargs;

    return media_policy_exclude(name, values, atoi(apply));
}

static int mediatool_policy_cmd_contain(struct mediatool_s* media, char* pargs)
{
    char* name;
    char* values;
    int result, ret;

    if (!strlen(pargs))
        return -EINVAL;
    name = pargs;

    pargs = strchr(pargs, ' ');
    if (!pargs)
        return -EINVAL;
    *pargs = 0;
    pargs++;
    values = pargs;

    ret = media_policy_contain(name, values, &result);
    if (ret < 0)
        return -EINVAL;

    printf("criterion %s %s value %s\n", name, result ? "contains" : "doesn't contain", values);

    return 0;
}

static int mediatool_policy_cmd_increase(struct mediatool_s* media, char* pargs)
{
    char* name;
    char* apply;

    if (!strlen(pargs))
        return -EINVAL;
    name = pargs;

    pargs = strchr(pargs, ' ');
    if (!pargs)
        return -EINVAL;
    *pargs = 0;
    pargs++;
    apply = pargs;

    return media_policy_increase(name, atoi(apply));
}

static int mediatool_policy_cmd_decrease(struct mediatool_s* media, char* pargs)
{
    char* name;
    char* apply;

    if (!strlen(pargs))
        return -EINVAL;
    name = pargs;

    pargs = strchr(pargs, ' ');
    if (!pargs)
        return -EINVAL;
    *pargs = 0;
    pargs++;
    apply = pargs;

    return media_policy_decrease(name, atoi(apply));
}

static int mediatool_focus_cmd_request(struct mediatool_s* media, char* pargs)
{
    int suggestion;
    long int i;

    if (pargs && !strlen(pargs))
        pargs = NULL;

    if (pargs && isdigit(pargs[0])) {
        i = strtol(pargs, NULL, 10);

        if (media->chain[i].handle)
            return -EBUSY;

        pargs = NULL;
    } else {
        for (i = 0; i < MEDIATOOL_MAX_CHAIN; i++) {
            if (!media->chain[i].handle)
                break;
        }

        if (i == MEDIATOOL_MAX_CHAIN)
            return -ENOMEM;
    }

    media->chain[i].id = i;
    media->chain[i].handle = media_focus_request(&suggestion, pargs,
        mediatool_focus_callback, &media->chain[i]);
    if (!media->chain[i].handle) {
        printf("media_player_request failed\n");
        return 0;
    }

    media->chain[i].type = MEDIATOOL_FOCUS;
    printf("focus ID %ld, first suggestion %d\n", i, suggestion);
    return 0;
}

static int mediatool_focus_cmd_abandon(struct mediatool_s* media, char* pargs)
{
    int ret = -EINVAL;
    long int id;

    if (!strlen(pargs))
        return ret;

    id = strtol(pargs, NULL, 10);

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !media->chain[id].handle)
        return ret;

    if (media->chain[id].type != MEDIATOOL_FOCUS)
        return 0;

    ret = media_focus_abandon(media->chain[id].handle);
    if (ret >= 0) {
        printf("abandon focus ID %ld\n", id);
        media->chain[id].handle = NULL;
    }

    return ret;
}

static int mediatool_common_cmd_quit(struct mediatool_s* media, char* pargs)
{
    char tmp[8];
    int i;

    for (i = 0; i < MEDIATOOL_MAX_CHAIN; i++) {
        if (media->chain[i].handle) {
            snprintf(tmp, 8, "%d", i);
            mediatool_common_cmd_close(media, tmp);
        }
    }

    return 0;
}

static int mediatool_common_cmd_help(struct mediatool_s* media, char* pargs)
{
    int i;

    for (i = 0; g_mediatool_cmds[i].cmd; i++)
        printf("%-16s %s\n", g_mediatool_cmds[i].cmd, g_mediatool_cmds[i].help);

    return 0;
}

static int mediatool_execute(struct mediatool_s* media, char* cmd, char* arg)
{
    int ret = 0;
    int x;

    /* Find the command in our cmd array */

    for (x = 0; g_mediatool_cmds[x].cmd; x++) {
        if (strcmp(cmd, g_mediatool_cmds[x].cmd) == 0) {

            ret = g_mediatool_cmds[x].pfunc(media, arg);
            if (ret < 0) {
                printf("cmd %s error %d\n", cmd, ret);
                ret = 0;
            }

            if (g_mediatool_cmds[x].pfunc == mediatool_common_cmd_quit)
                ret = -1;

            break;
        }
    }

    if (x == sizeof(g_mediatool_cmds) / sizeof(g_mediatool_cmds[0]))
        printf("Unknown cmd: %s\n", cmd);

    return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, char* argv[])
{
    int ret, len, arg_len;
    char *cmd, *arg;
    char* buffer;

    buffer = malloc(CONFIG_NSH_LINELEN);
    if (!buffer)
        return -ENOMEM;

    while (1) {
        printf("mediatool> ");
        fflush(stdout);

        len = readline(buffer, CONFIG_NSH_LINELEN, stdin, stdout);
        buffer[len] = '\0';
        if (len < 0)
            continue;

        if (buffer[0] == '!') {
#ifdef CONFIG_SYSTEM_SYSTEM
            system(buffer + 1);
#endif
            continue;
        }

        if (buffer[len - 1] == '\n')
            buffer[len - 1] = '\0';

        cmd = strtok_r(buffer, " \n", &arg);
        if (cmd == NULL)
            continue;

        while (*arg == ' ')
            arg++;

        arg_len = strlen(arg);

        while (isspace(arg[arg_len - 1]))
            arg_len--;

        arg[arg_len] = '\0';

        ret = mediatool_execute(&g_mediatool, cmd, arg);
        if (ret < 0)
            break;
    }

    free(buffer);
    return 0;
}
