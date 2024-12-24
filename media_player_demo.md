# *Media Player Demo*

## **Complete sample code**
```c
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <fcntl.h>
#include <semaphore.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <media_api.h>
#include <sys/mount.h>
#include <linux/fs.h>

#define PLAYER_IDLE      0
#define PLAYER_PREPARED  1
#define PLAYER_STARTED   2
#define PLAYER_COMPLETED 3
#define PLAYER_STOPPED   4
#define PLAYER_DEFAULT_SIZE 512

static struct player_global_s {
    sem_t sem;
    int state;
} g_priv;

struct player_priv_s {
    void *handle;
    char *file;
};

static void music_event_callback(void* cookie, int event, int ret, const char *data) {
    char *str = "UNKNOW EVENT";

    switch (event) {
        case MEDIA_EVENT_STARTED:
            g_priv.state = PLAYER_STARTED;
            str = "MEDIA_EVENT_STARTED";
            sem_post(&g_priv.sem);
            break;
        case MEDIA_EVENT_STOPPED:
            str = "MEDIA_EVENT_STOPPED";
            break;
        case MEDIA_EVENT_COMPLETED:
            g_priv.state = PLAYER_COMPLETED;
            str = "MEDIA_EVENT_COMPLETED";
            break;
        case MEDIA_EVENT_PREPARED:
            g_priv.state = PLAYER_PREPARED;
            str = "MEDIA_EVENT_PREPARED";
            sem_post(&g_priv.sem);
            break;
        case MEDIA_EVENT_PAUSED:
            str = "MEDIA_EVENT_PAUSED";
            break;
        case MEDIA_EVENT_SEEKED:
            str = "MEDIA_EVENT_SEEKED";
            break;
    }

    syslog(LOG_INFO, "%s, music event %s, event %d, ret %d, info %s line %d\n",
           __func__, str, event, ret, data ? data : "NULL", __LINE__);
}

/* buffer mode */
static void *player_write_thread(void *arg) {
    struct player_priv_s *priv = (struct player_priv_s *)arg;
    char *buffer = NULL;
    int act, ret;
    int fd = -1;

    fd = open(priv->file, O_RDONLY);
    if (fd < 0) {
        syslog(LOG_ERR, "Failed to open file: %s\n", strerror(errno));
        return NULL;
    }

    buffer = (char *)malloc(PLAYER_DEFAULT_SIZE);
    if (!buffer) {
        close(fd);
        return NULL;
    }

    syslog(LOG_INFO, "%s, start, line %d\n", __func__, __LINE__);

    while (1) {
        act = read(fd, buffer, PLAYER_DEFAULT_SIZE);
        if (act <= 0)
            break;

        char *tmp = buffer;
        while (act > 0) {
            ret = media_player_write_data(priv->handle, tmp, act);
            if (ret == 0) {
                break;
            } else if (ret < 0 && errno == EAGAIN) {
                sleep(1);
                continue;
            } else if (ret < 0) {
                syslog(LOG_ERR, "%s, error ret %d errno %d, line %d\n", __func__, ret, errno, __LINE__);
                goto out;
            }

            tmp += ret;
            act -= ret;
        }
    }

out:
    free(buffer);
    close(fd);
    g_priv.state = PLAYER_COMPLETED;
    return NULL;
}

int main(int argc, char *argv[]) {
    unsigned int duration, position;
    float volume;
    void *player;
    int ret;
    int buffer_mode_enabled = 0; /* Default to not using buffer mode */
    pthread_t thread;
    struct player_priv_s priv;

    if (argc >= 3) {
        if (strcmp(argv[2], "1") == 0) {
            buffer_mode_enabled = 1;
        } else if (strcmp(argv[2], "2") == 0) {
            buffer_mode_enabled = 0;
        } else {
            syslog(LOG_ERR, "Invalid buffer mode selection. Use 1 to buffer or 2 to url.\n");
            exit(EXIT_FAILURE);
        }
    } else {
        syslog(LOG_ERR, "Usage: %s <music_file> [1|2] (1=enable buffer mode, 2=url mode)\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    sem_init(&g_priv.sem, 0, 0);
    g_priv.state = PLAYER_IDLE;

    player = media_player_open("Music");
    if (!player) {
        syslog(LOG_ERR, "Player: open failed.\n");
        return -1;
    }

    ret = media_player_set_event_callback(player, player, music_event_callback);
    if (ret < 0) {
        syslog(LOG_ERR, "Player: set callback failed.\n");
        goto out;
    }

    ret = media_player_set_volume(player, 0.6);
    if (ret < 0) {
        syslog(LOG_ERR, "Player: set_volume failed.\n");
        goto out;
    }

    ret = media_player_prepare(player,argv[1], NULL);
    if (ret < 0) {
        syslog(LOG_ERR, "Player: prepare failed.\n");
        goto out;
    }

    sem_wait(&g_priv.sem);

    if (g_priv.state != PLAYER_PREPARED) {
        syslog(LOG_INFO, "Player: prepare event return failed.\n");
        goto out;
    }

    /* Initialize buffer mode if selected */
    if (buffer_mode_enabled) {
        priv.handle = player;
        priv.file = strdup(argv[1]); /* Use the music file provided as an argument */
        if (!priv.file) {
            syslog(LOG_ERR, "Failed to duplicate file path.\n");
            goto out;
        }
        ret = pthread_create(&thread, NULL, player_write_thread, &priv);
        if (ret != 0) {
            syslog(LOG_ERR, "Player: create thread failed.\n");
            free(priv.file);
            goto out;
        }
        pthread_detach(thread);
    }

    ret = media_player_start(player);
    if (ret < 0) {
        syslog(LOG_ERR, "Player: start failed.\n");
        goto out;
    }

    sem_wait(&g_priv.sem);

    if (g_priv.state != PLAYER_STARTED) {
        syslog(LOG_ERR, "Player: start event return failed.\n");
        goto out;
    }

    syslog(LOG_INFO, "Player: playing %d.\n", media_player_is_playing(player));

    media_player_get_duration(player, &duration);
    syslog(LOG_INFO, "Player: get_duration ret %d %d.\n", ret, duration);

    media_player_get_volume(player, &volume);
    syslog(LOG_INFO, "Player: get_volume %f.\n", volume);

    while (g_priv.state == PLAYER_STARTED) {
        media_player_get_position(player, &position);
        sleep(30);
    }

    ret = media_player_stop(player);
    if (ret < 0) {
        syslog(LOG_ERR, "Player: stop failed.\n");
        goto out;
    }

out:
    if (priv.file) {
        free(priv.file);
    }

    media_player_close(player, 0);
    sem_destroy(&g_priv.sem);
    syslog(LOG_INFO, "Player: closed.\n");

    return 0;
}
```
