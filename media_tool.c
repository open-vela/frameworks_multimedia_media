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
#include <malloc.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <system/readline.h>

#include <media_api.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MEDIATOOL_MAX_CHAIN     16
#define MEDIATOOL_FILE_MAX      64

/****************************************************************************
 * Public Type Declarations
 ****************************************************************************/

struct mediatool_chain_s
{
    int       id;
    bool      player;
    void      *handle;

    pthread_t thread;
    int       fd;

    bool      start;
    bool      loop;

    char      *buf;
    int       size;
};

struct mediatool_s
{
    struct mediatool_chain_s chain[MEDIATOOL_MAX_CHAIN];
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int mediatool_player_cmd_open(struct mediatool_s *media, char *pargs);
static int mediatool_recorder_cmd_open(struct mediatool_s *media, char *pargs);
static int mediatool_common_cmd_close(struct mediatool_s *media, char *pargs);
static int mediatool_common_cmd_reset(struct mediatool_s *media, char *pargs);
static int mediatool_common_cmd_prepare(struct mediatool_s *media, char *pargs);
static int mediatool_common_cmd_start(struct mediatool_s *media, char *pargs);
static int mediatool_common_cmd_stop(struct mediatool_s *media, char *pargs);
static int mediatool_player_cmd_pause(struct mediatool_s *media, char *pargs);
static int mediatool_player_cmd_volume(struct mediatool_s *media, char *pargs);
static int mediatool_player_cmd_loop(struct mediatool_s *media, char *pargs);
static int mediatool_player_cmd_seek(struct mediatool_s *media, char *pargs);
static int mediatool_player_cmd_position(struct mediatool_s *media, char *pargs);
static int mediatool_player_cmd_duration(struct mediatool_s *media, char *pargs);
static int mediatool_player_cmd_isplaying(struct mediatool_s *media, char *pargs);
static int mediatool_common_stop_inner(struct mediatool_chain_s *chain);
static int mediatool_common_cmd_send(struct mediatool_s *media, char *pargs);
static int mediatool_server_cmd_dump(struct mediatool_s *media, char *pargs);
static int mediatool_policy_cmd_setint(struct mediatool_s *media, char *pargs);
static int mediatool_policy_cmd_getint(struct mediatool_s *media, char *pargs);
static int mediatool_policy_cmd_setstring(struct mediatool_s *media, char *pargs);
static int mediatool_policy_cmd_getstring(struct mediatool_s *media, char *pargs);
static int mediatool_policy_cmd_include(struct mediatool_s *media, char *pargs);
static int mediatool_policy_cmd_exclude(struct mediatool_s *media, char *pargs);
static int mediatool_policy_cmd_contain(struct mediatool_s *media, char *pargs);
static int mediatool_policy_cmd_increase(struct mediatool_s *media, char *pargs);
static int mediatool_policy_cmd_decrease(struct mediatool_s *media, char *pargs);
static int mediatool_common_cmd_quit(struct mediatool_s *media, char *pargs);
static int mediatool_common_cmd_help(struct mediatool_s *media, char *pargs);

/****************************************************************************
 * Private Type Declarations
 ****************************************************************************/

typedef int (*mediatool_func)(struct mediatool_s *media, char *pargs);

struct mediatool_cmd_s
{
    const char     *cmd;      /* The command text */
    mediatool_func pfunc;     /* Pointer to command handler */
    const char     *help;     /* The help text */
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct mediatool_s g_mediatool;

static struct mediatool_cmd_s g_mediatool_cmds[] =
{
    {
        "open",
        mediatool_player_cmd_open,
        "Create player channel return ID (open [specific filter])"
    },
    {
        "copen",
        mediatool_recorder_cmd_open,
        "Create recorder channel return ID (copen)"
    },
    {
        "close",
        mediatool_common_cmd_close,
        "Destroy player/recorder channel (close ID)"
    },
    {
        "reset",
        mediatool_common_cmd_reset,
        "Reset player/recorder channel (reset ID)"
    },
    {
        "prepare",
        mediatool_common_cmd_prepare,
        "Set player/recorder prepare (start ID url/buffer options)"
    },
    {
        "start",
        mediatool_common_cmd_start,
        "Set player/recorder start (start ID)"
    },
    {
        "stop",
        mediatool_common_cmd_stop,
        "Set player/recorder stop (stop ID)"
    },
    {
        "pause",
        mediatool_player_cmd_pause,
        "Set player/recorder pause (pause ID)"
    },
    {
        "volume",
        mediatool_player_cmd_volume,
        "Set/Get player volume (volume ID ?/volume)"
    },
    {
        "loop",
        mediatool_player_cmd_loop,
        "Set/Get player loop (loop ID 1/0)"
    },
    {
        "seek",
        mediatool_player_cmd_seek,
        "Set player seek (seek ID time)"
    },
    {
        "position",
        mediatool_player_cmd_position,
        "Get player position time ms(position ID)"
    },
    {
        "duration",
        mediatool_player_cmd_duration,
        "Get player duration time ms(duration ID)"
    },
    {
        "isplay",
        mediatool_player_cmd_isplaying,
        "Get position is playing or not(isplay ID)"
    },
    {
        "send",
        mediatool_common_cmd_send,
        "Send cmd to graph"
    },
    {
        "dump",
        mediatool_server_cmd_dump,
        "Dump graph and policy"
    },
    {
        "setint",
        mediatool_policy_cmd_setint,
        "set criterion value with integer(setint NAME VALUE APPLY)"
    },
    {
        "getint",
        mediatool_policy_cmd_getint,
        "get criterion value in integer(getint NAME)"
    },
    {
        "setstring",
        mediatool_policy_cmd_setstring,
        "set criterion value with string(setstring NAME VALUE APPLY)"
    },
    {
        "getstring",
        mediatool_policy_cmd_getstring,
        "get criterion value in string(getstring NAME)"
    },
    {
        "include",
        mediatool_policy_cmd_include,
        "include inclusive criterion values(include NAME VALUE APPLY)"
    },
    {
        "exclude",
        mediatool_policy_cmd_exclude,
        "exclude inclusive criterion values(exclude NAME VALUE APPLY)"
    },
    {
        "contain",
        mediatool_policy_cmd_contain,
        "check wether contain criterion values(contain NAME VALUE)"
    },
    {
        "increase",
        mediatool_policy_cmd_increase,
        "increase criterion value by one(increase NAME APPLY)"
    },
    {
        "decrease",
        mediatool_policy_cmd_decrease,
        "decrease criterion value by one(decrease NAME APPLY)"
    },
    {
        "q",
        mediatool_common_cmd_quit,
        "Quit (q)"
    },
    {
        "help",
        mediatool_common_cmd_help,
        "Show this message (help)"
    },
    {
        0
    },
};

/****************************************************************************
 * Private Function
 ****************************************************************************/

static void mediatool_event_callback(void* cookie, int event,
                                     int ret, const char *data)
{
    struct mediatool_chain_s *chain = cookie;
    char *str;

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
    } else {
        str = "NORMAL EVENT";
    }

    printf("%s, id %d, event %s, event %d, ret %d, line %d\n",
            __func__, chain->id, str, event, ret, __LINE__);
}

static void mediatool_common_stop_thread(struct mediatool_chain_s *chain)
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

static int mediatool_common_stop_inner(struct mediatool_chain_s *chain)
{
    int ret = 0;

    if (!chain->player)
        ret = media_recorder_stop(chain->handle);
    else
        ret = media_player_stop(chain->handle);

    usleep(1000);

    mediatool_common_stop_thread(chain);

    return ret;
}

static int mediatool_player_cmd_open(struct mediatool_s *media, char *pargs)
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

    media->chain[i].player = true;

    printf("player ID %ld\n", i);

    return 0;
}

static int mediatool_recorder_cmd_open(struct mediatool_s *media, char *pargs)
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

    media->chain[i].player = false;

    printf("recorder ID %ld\n", i);

    return 0;
}

static int mediatool_common_cmd_close(struct mediatool_s *media, char *pargs)
{
    int ret;
    int id;
    int pending_stop;

    if (!strlen(pargs))
        return -EINVAL;

    sscanf(pargs, "%d %d", &id, &pending_stop);

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !media->chain[id].handle)
        return -EINVAL;

    mediatool_common_stop_inner(&media->chain[id]);

    if (media->chain[id].player)
        ret = media_player_close(media->chain[id].handle, pending_stop);
    else
        ret = media_recorder_close(media->chain[id].handle);

    media->chain[id].handle = NULL;

    return ret;
}

static int mediatool_common_cmd_reset(struct mediatool_s *media, char *pargs)
{
    long int id;
    int ret;

    if (!strlen(pargs))
        return -EINVAL;

    id = strtol(pargs, NULL, 10);

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !media->chain[id].handle)
        return -EINVAL;

    if (media->chain[id].player)
        ret = media_player_reset(media->chain[id].handle);
    else
        ret = media_recorder_reset(media->chain[id].handle);

    mediatool_common_stop_thread(&media->chain[id]);

    return ret;
}

static void *mediatool_common_thread(void *arg)
{
    struct mediatool_chain_s *chain = arg;
    char *tmp;
    int act;
    int ret;

    printf("%s, start, line %d\n", __func__, __LINE__);

    if (chain->player) {
        while (1) {
            act = read(chain->fd, chain->buf, chain->size);
            assert(act >= 0);
            if (act == 0)
                break;

            tmp = chain->buf;
            while (act > 0) {
                ret = media_player_write_data(chain->handle, tmp, act);
                if (ret == 0) {
                    break;
                } else if (ret < 0 && errno == EAGAIN) {
                    usleep(1000);
                    continue;
                } else if (ret < 0){
                    printf("%s, error ret %d errno %d, line %d\n", __func__, ret, errno, __LINE__);
                    goto out;
                }

                tmp += ret;
                act -= ret;
            }
        }
    } else {
        while (1) {
            ret = media_recorder_read_data(chain->handle, chain->buf, chain->size);
            if (ret == 0) {
                break;
            } else if (ret < 0 && errno == EAGAIN){
                usleep(1000);
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

static int mediatool_common_cmd_prepare(struct mediatool_s *media, char *pargs)
{
    pthread_t thread;
    bool url_mode;
    long int id;
    char *file;
    char *ptr;
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
    else
        url_mode = false;

    file = ptr;
    ptr = strchr(ptr, ' ');
    if (ptr)
        *ptr++ = 0;

    if (media->chain[id].player)
        ret = media_player_prepare(media->chain[id].handle, url_mode ? file : NULL, ptr);
    else
        ret = media_recorder_prepare(media->chain[id].handle, url_mode ? file : NULL, ptr);

    if (ret < 0)
        return ret;

    if (!url_mode) {
        if (media->chain[id].thread) {
            printf("already prepare, can't prepare twice\n");
            return -EPERM;
        }

        if (!media->chain[id].player)
            unlink(file);

        media->chain[id].fd = open(file, media->chain[id].player ? O_RDONLY : O_CREAT | O_RDWR);
        if (media->chain[id].fd < 0) {
            printf("buffer mode, file can't open\n");
            return -EINVAL;
        }

        media->chain[id].size = 512;
        media->chain[id].buf = malloc(media->chain[id].size);
        assert(media->chain[id].buf);

        ret = pthread_create(&thread, NULL, mediatool_common_thread, &media->chain[id]);
        assert(!ret);

        pthread_setname_np(thread, "mediatool_file");
        media->chain[id].thread = thread;
    }

    return ret;
}

static int mediatool_common_cmd_start(struct mediatool_s *media, char *pargs)
{
    long int id;
    int ret;

    if (!strlen(pargs))
        return -EINVAL;

    id = strtol(pargs, NULL, 10);

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !media->chain[id].handle)
        return -EINVAL;

    if (media->chain[id].player)
        ret = media_player_start(media->chain[id].handle);
    else
        ret = media_recorder_start(media->chain[id].handle);

    return ret;
}

static int mediatool_common_cmd_stop(struct mediatool_s *media, char *pargs)
{
    long int id;

    if (!strlen(pargs))
        return -EINVAL;

    id = strtol(pargs, NULL, 10);

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !media->chain[id].handle)
        return -EINVAL;

    return mediatool_common_stop_inner(&media->chain[id]);
}

static int mediatool_player_cmd_pause(struct mediatool_s *media, char *pargs)
{
    long int id;
    int ret = 0;

    if (!strlen(pargs))
        return -EINVAL;

    id = strtol(pargs, NULL, 10);

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !media->chain[id].handle)
        return -EINVAL;

    if (media->chain[id].player)
        ret = media_player_pause(media->chain[id].handle);
    else
        ret = media_recorder_pause(media->chain[id].handle);

    return ret;
}

static int mediatool_player_cmd_volume(struct mediatool_s *media, char *pargs)
{
    float volume;
    int ret = 0;
    char *ptr;
    int id;

    if (!strlen(pargs))
        return -EINVAL;

    ptr = strchr(pargs, '?');
    if (ptr)
        sscanf(pargs, "%d", &id);
    else
        sscanf(pargs, "%d %f", &id, &volume);

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !media->chain[id].handle)
        return -EINVAL;

    if (!media->chain[id].player)
        return 0;

    if (ptr)
        ret = media_player_get_volume(media->chain[id].handle, &volume);
    else
        ret = media_player_set_volume(media->chain[id].handle, volume);

    printf("ID %d, %s volume %f\n", id, ptr ? "get" : "set", volume);

    return ret;
}

static int mediatool_player_cmd_loop(struct mediatool_s *media, char *pargs)
{
    int loop;
    int id;

    if (!strlen(pargs))
        return -EINVAL;

    sscanf(pargs, "%d %d", &id, &loop);

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !media->chain[id].handle)
        return -EINVAL;

    if (!media->chain[id].player)
        return 0;

    return media_player_set_looping(media->chain[id].handle, loop);
}

static int mediatool_player_cmd_seek(struct mediatool_s *media, char *pargs)
{
    unsigned int seek;
    int id;

    if (!strlen(pargs))
        return -EINVAL;

    sscanf(pargs, "%d %d", &id, &seek);

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !media->chain[id].handle)
        return -EINVAL;

    if (!media->chain[id].player)
        return 0;

    return media_player_seek(media->chain[id].handle, seek);
}

static int mediatool_player_cmd_position(struct mediatool_s *media, char *pargs)
{
    unsigned int position;
    long int id;
    int ret;

    if (!strlen(pargs))
        return -EINVAL;

    id = strtol(pargs, NULL, 0);

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !media->chain[id].handle)
        return -EINVAL;

    if (!media->chain[id].player)
        return 0;

    ret = media_player_get_position(media->chain[id].handle, &position);
    if (ret < 0)
        return ret;

    printf("Current position %d ms\n", position);

    return 0;
}

static int mediatool_player_cmd_duration(struct mediatool_s *media, char *pargs)
{
    unsigned int duration;
    long int id;
    int ret;

    if (!strlen(pargs))
        return -EINVAL;

    id = strtol(pargs, NULL, 10);

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !media->chain[id].handle)
        return -EINVAL;

    if (!media->chain[id].player)
        return 0;

    ret = media_player_get_duration(media->chain[id].handle, &duration);
    if (ret < 0) {
        printf("Total duration ret %d\n", ret);
        return ret;
    }

    printf("Total duration %d ms\n", duration);

    return 0;
}

static int mediatool_player_cmd_isplaying(struct mediatool_s *media, char *pargs)
{
    long int id;
    int ret;

    if (!strlen(pargs))
        return -EINVAL;

    id = strtol(pargs, NULL, 10);

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !media->chain[id].handle)
        return -EINVAL;

    if (!media->chain[id].player)
        return 0;

    ret = media_player_is_playing(media->chain[id].handle);
    if (ret < 0) {
        printf("is_playing ret %d\n", ret);
        return ret;
    }

    printf("Is_playing %d\n", ret);

    return 0;
}

static int mediatool_common_cmd_send(struct mediatool_s *media, char *pargs)
{
    char *target;
    char *cmd;
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

static int mediatool_server_cmd_dump(struct mediatool_s *media, char *pargs)
{
    /* pargs not used in neither graph_dump nor policy_dump,
     * so let's make it simple, just "dump".
     */
    media_policy_dump(pargs);
    media_graph_dump(pargs);

    return 0;
}

static int mediatool_policy_cmd_setint(struct mediatool_s *media, char *pargs)
{
    char *name;
    char *value;
    char *apply;

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

static int mediatool_policy_cmd_getint(struct mediatool_s *media, char *pargs)
{
    char *name;
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

static int mediatool_policy_cmd_setstring(struct mediatool_s *media, char *pargs)
{
    char *name;
    char *value;
    char *apply;

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

static int mediatool_policy_cmd_getstring(struct mediatool_s *media, char *pargs)
{
    char *name;
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

static int mediatool_policy_cmd_include(struct mediatool_s *media, char *pargs)
{
    char *name;
    char *values;
    char *apply;

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

static int mediatool_policy_cmd_exclude(struct mediatool_s *media, char *pargs)
{
    char *name;
    char *values;
    char *apply;

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

static int mediatool_policy_cmd_contain(struct mediatool_s *media, char *pargs)
{
    char *name;
    char *values;
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

static int mediatool_policy_cmd_increase(struct mediatool_s *media, char *pargs)
{
    char *name;
    char *apply;

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

static int mediatool_policy_cmd_decrease(struct mediatool_s *media, char *pargs)
{
    char *name;
    char *apply;

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

static int mediatool_common_cmd_quit(struct mediatool_s *media, char *pargs)
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

static int mediatool_common_cmd_help(struct mediatool_s *media, char *pargs)
{
    int i;

    for (i = 0; g_mediatool_cmds[i].cmd; i++)
        printf("%-16s %s\n", g_mediatool_cmds[i].cmd, g_mediatool_cmds[i].help);

    return 0;
}

static int mediatool_execute(struct mediatool_s *media, char *cmd, char *arg)
{
    int ret = 0;
    int x;

    /* Find the command in our cmd array */

    for (x = 0; g_mediatool_cmds[x].cmd; x++) {
        if (strcmp(cmd, g_mediatool_cmds[x].cmd) == 0) {

            ret = g_mediatool_cmds[x].pfunc(media, arg);
            if (ret < 0)
                printf("cmd %s error %d\n", cmd, ret);

            if (g_mediatool_cmds[x].pfunc == mediatool_common_cmd_quit)
                ret = 1;

            break;
        }
    }

    if (x == sizeof(g_mediatool_cmds)/sizeof(g_mediatool_cmds[0]))
        printf("Unknown cmd: %s\n", cmd);

    return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, char *argv[])
{
    int ret, len, arg_len;
    char *cmd, *arg;
    char *buffer;

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
        if (ret > 0)
            break;
    }

    free(buffer);
    return 0;
}
