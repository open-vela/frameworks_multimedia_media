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
    bool      exit;

    char      *buf;
    int       size;
};

struct mediatool_s
{
    struct mediatool_chain_s chain[MEDIATOOL_MAX_CHAIN];
    int                      init;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

extern void media_server_start(void);

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
static int mediatool_player_cmd_fadein(struct mediatool_s *media, char *pargs);
static int mediatool_player_cmd_fadeout(struct mediatool_s *media, char *pargs);
static int mediatool_common_cmd_send(struct mediatool_s *media, char *pargs);
static int mediatool_server_cmd_dump(struct mediatool_s *media, char *pargs);
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
        "Set player pause (pause ID)"
    },
    {
        "fadein",
        mediatool_player_cmd_fadein,
        "Set player fadein (player ID duration(ms))"
    },
    {
        "fadeout",
        mediatool_player_cmd_fadeout,
        "Set player fadeout (player ID duration(ms))"
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
        "Grpph dump"
    },
    {
        "dump",
        mediatool_server_cmd_dump,
        "Grpph dump"
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

static int mediatool_common_stop_inner(struct mediatool_chain_s *chain)
{
    int ret = 0;

    if (!chain->player)
        ret = media_recorder_stop(chain->handle);

    usleep(1000);

    if (chain->thread) {

        chain->exit = 1;

        pthread_join(chain->thread, NULL);
        free(chain->buf);
        close(chain->fd);

        chain->exit = 0;
        chain->thread = 0;
        chain->buf = NULL;
        chain->fd = -1;
    }

    if (chain->player)
        ret = media_player_stop(chain->handle);

    return ret;
}

static int mediatool_player_cmd_open(struct mediatool_s *media, char *pargs)
{
    int ret;
    int i;

    if (pargs && !strlen(pargs))
        pargs = NULL;

    if (pargs && isdigit(pargs[0])) {
        i = atoi(pargs);

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
    media->chain[i].handle = media_player_open(0);
    if (!media->chain[i].handle) {
        printf("media_player_open error\n");
        return -EINVAL;
    }

    ret = media_player_set_event_callback(media->chain[i].handle,
                                             &media->chain[i],
                                             mediatool_event_callback);
    assert(!ret);

    media->chain[i].player = true;

    printf("player ID %d\n", i);

    return 0;
}

static int mediatool_recorder_cmd_open(struct mediatool_s *media, char *pargs)
{
    int ret;
    int i;

    if (pargs && !strlen(pargs))
        pargs = NULL;

    if (pargs && isdigit(pargs[0])) {
        i = atoi(pargs);

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
    media->chain[i].handle = media_recorder_open(0);
    if (!media->chain[i].handle) {
        printf("media_recorder_open error\n");
        return -EINVAL;
    }

    ret = media_recorder_set_event_callback(media->chain[i].handle,
                                             &media->chain[i],
                                             mediatool_event_callback);
    assert(!ret);

    media->chain[i].player = false;

    printf("recorder ID %d\n", i);

    return 0;
}

static int mediatool_common_cmd_close(struct mediatool_s *media, char *pargs)
{
    int ret;
    int id;

    if (!strlen(pargs))
        return -EINVAL;

    id = atoi(pargs);

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !media->chain[id].handle)
        return -EINVAL;

    mediatool_common_stop_inner(&media->chain[id]);

    if (media->chain[id].player)
        ret = media_player_close(media->chain[id].handle);
    else
        ret = media_recorder_close(media->chain[id].handle);

    media->chain[id].handle = NULL;

    return ret;
}

static int mediatool_common_cmd_reset(struct mediatool_s *media, char *pargs)
{
    int ret;
    int id;

    if (!strlen(pargs))
        return -EINVAL;

    id = atoi(pargs);

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !media->chain[id].handle)
        return -EINVAL;

    mediatool_common_stop_inner(&media->chain[id]);

    if (media->chain[id].player)
        ret = media_player_reset(media->chain[id].handle);
    else
        ret = media_recorder_reset(media->chain[id].handle);

    media->chain[id].handle = NULL;

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
        while (!chain->exit) {
            act = read(chain->fd, chain->buf, chain->size);
            assert(act >= 0);
            if (act == 0) {
                media_player_write_data(chain->handle, 0, 0);
                break;
            }

            tmp = chain->buf;
            while (!chain->exit && act > 0) {
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

            assert(ret > 0);

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
    char *file;
    char *ptr;
    int ret = 0;
    int id;

    if (!strlen(pargs))
        return -EINVAL;

    ptr = strchr(pargs, ' ');
    if (!ptr)
        return -EINVAL;

    *ptr++ = 0;

    id = atoi(pargs);

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

    if (!url_mode) {
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
    int ret;
    int id;

    if (!strlen(pargs))
        return -EINVAL;

    id = atoi(pargs);

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
    int id;

    if (!strlen(pargs))
        return -EINVAL;

    id = atoi(pargs);

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !media->chain[id].handle)
        return -EINVAL;

    return mediatool_common_stop_inner(&media->chain[id]);
}

static int mediatool_player_cmd_pause(struct mediatool_s *media, char *pargs)
{
    int ret = 0;
    int id;

    if (!strlen(pargs))
        return -EINVAL;

    id = atoi(pargs);

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !media->chain[id].handle)
        return -EINVAL;

    if (media->chain[id].player)
        ret = media_player_pause(media->chain[id].handle);

    return ret;
}

static int mediatool_player_cmd_fadein(struct mediatool_s *media, char *pargs)
{
    unsigned int duration = 0;
    int ret = 0;
    int id;

    if (!strlen(pargs))
        return -EINVAL;

    sscanf(pargs, "%d %d", &id, &duration);

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !media->chain[id].handle)
        return -EINVAL;

    if (media->chain[id].player)
        ret = media_player_set_fadein(media->chain[id].handle, duration);

    return ret;
}

static int mediatool_player_cmd_fadeout(struct mediatool_s *media, char *pargs)
{
    unsigned int duration = 0;
    int ret = 0;
    int id;

    if (!strlen(pargs))
        return -EINVAL;

    sscanf(pargs, "%d %d", &id, &duration);

    if (id < 0 || id >= MEDIATOOL_MAX_CHAIN || !media->chain[id].handle)
        return -EINVAL;

    if (media->chain[id].player)
        ret = media_player_set_fadeout(media->chain[id].handle, duration);

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
    int ret;
    int id;

    if (!strlen(pargs))
        return -EINVAL;

    id = atoi(pargs);

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
    int ret;
    int id;

    if (!strlen(pargs))
        return -EINVAL;

    id = atoi(pargs);

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
    int ret;
    int id;

    if (!strlen(pargs))
        return -EINVAL;

    id = atoi(pargs);

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
    return 0;
}

static int mediatool_server_cmd_dump(struct mediatool_s *media, char *pargs)
{
    media_server_dump((const char *)pargs);
    return 0;
}

static int mediatool_common_cmd_quit(struct mediatool_s *media, char *pargs)
{
    char tmp[8];
    int i;

    if (media->init) {
        for (i = 0; i < MEDIATOOL_MAX_CHAIN; i++) {
            if (media->chain[i].handle) {
                snprintf(tmp, 8, "%d", i);
                mediatool_common_cmd_close(media, tmp);
            }
        }

        media->init = 0;
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
    char *cmd, *arg;
    char *buffer;
    int ret, len;

    media_server_start();

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

        ret = mediatool_execute(&g_mediatool, cmd, arg);
        if (ret > 0)
            break;
    }

    free(buffer);
    return 0;
}
