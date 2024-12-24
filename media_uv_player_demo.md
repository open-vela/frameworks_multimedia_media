# *Media UV Player Demo*

## **Complete sample code**
```c
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <semaphore.h>
#include <media_api.h>
#include <linux/fs.h>
#include <uv.h>

#define PLAYER_IDLE      0
#define PLAYER_PREPARED  1
#define PLAYER_STARTED   2
#define PLAYER_COMPLETED 3
#define PLAYER_STOPPED   4

static struct player_global_s {
    sem_t sem;
    int state;
} g_priv = { .state = PLAYER_IDLE };

struct uv_player_priv_s {
    uv_loop_t loop;
    void *player;
    uv_async_t asyncq;
    uv_timer_t stop_timer; /*Timer to stop the player */
    char *file_path;
    pthread_mutex_t mutex;
};

void uv_player_event_cb(void* cookie, int event,
    int ret, const char* extra)
{
    if (event == MEDIA_EVENT_PREPARED) {
        syslog(LOG_INFO, "MEDIA_EVENT_PREPARED\n");
        g_priv.state = PLAYER_PREPARED;
    } else if (event == MEDIA_EVENT_STARTED) {
        syslog(LOG_INFO, "MEDIA_EVENT_STARTED\n");
        g_priv.state = PLAYER_STARTED;
    } else if (event == MEDIA_EVENT_STOPPED) {
        syslog(LOG_INFO, "MEDIA_EVENT_STOPPED\n");
        g_priv.state = PLAYER_STOPPED;
    } else if (event == MEDIA_EVENT_COMPLETED) {
        syslog(LOG_INFO, "MEDIA_EVENT_COMPLETED\n");
        g_priv.state = PLAYER_COMPLETED;
    }
}
static void media_player_uvasyncq_close_cb(uv_handle_t* handle) {
    printf("Bye-Bye!\n");
    uv_loop_t *loop = (uv_loop_t*)uv_req_get_data((const uv_req_t*)handle);
    if (uv_loop_alive(loop)) {
        uv_stop(loop);
    }
}
static void media_player_uvasyncq_cb(uv_async_t* asyncq) {
   printf("mediaplayer_uvasyncq_cb!\n");
}

static void* media_player_uvloop_thread(void* arg) {
    struct uv_player_priv_s *priv = (struct uv_player_priv_s *)arg;
    int ret;

    ret = uv_loop_init(&priv->loop);
    if (ret < 0) {
        syslog(LOG_ERR, "Failed to initialize loop: %d\n", ret);
        return NULL;
    }

    pthread_mutex_init(&priv->mutex, NULL);

    priv->asyncq.data = arg;
    ret = uv_async_init(&priv->loop, &priv->asyncq, media_player_uvasyncq_cb);
    if (ret < 0) {
        syslog(LOG_ERR, "Failed to initialize async queue: %d\n", ret);
        goto out;
    }

    printf("[%s][%d] running\n", __func__, __LINE__);
    while (1) {
        ret = uv_run(&priv->loop, UV_RUN_DEFAULT);
        if (ret == 0)
            break;
    }
out:
    ret = uv_loop_close(&priv->loop);
    printf("[%s][%d] out.\n", __func__, __LINE__);

    pthread_mutex_destroy(&priv->mutex);

    return NULL;
}
static void uv_player_close_cb(void *cookie, int ret) {
    struct uv_player_priv_s *priv = (struct uv_player_priv_s *)cookie;
    if (ret < 0) {
        syslog(LOG_ERR, "Player close failed: %d\n", ret);
    } else {
        syslog(LOG_INFO, "Player closed successfully.\n");
    }
    priv->player = NULL;
    /*Close asynchronous handle.*/
    uv_close((uv_handle_t*)&priv->asyncq, media_player_uvasyncq_close_cb);
}
static void uv_player_stop_cb(void *cookie, int ret) {
    struct uv_player_priv_s *priv = (struct uv_player_priv_s *)cookie;
    if (ret < 0) {
        syslog(LOG_ERR, "Player stop failed: %d\n", ret);
    } else {
        syslog(LOG_INFO, "Player stopped successfully.\n");

        pthread_mutex_lock(&priv->mutex);
        ret = media_uv_player_close(priv->player, 0, uv_player_close_cb);
        pthread_mutex_unlock(&priv->mutex);

        if (ret < 0) {
            syslog(LOG_ERR, "Player: close failed.\n");
        }
    }
}
static void start_timer_cb(uv_timer_t* handle) {
    struct uv_player_priv_s *priv = (struct uv_player_priv_s *)handle->data;
    int ret;

    pthread_mutex_lock(&priv->mutex);
    ret = media_uv_player_stop(priv->player, uv_player_stop_cb, priv);
    pthread_mutex_unlock(&priv->mutex);

    if (ret < 0) {
        syslog(LOG_ERR, "Player: stop failed.\n");
    }
}

static void uv_player_start_cb(void *cookie, int ret) {
    struct uv_player_priv_s *priv = (struct uv_player_priv_s *)cookie;
    if (ret < 0) {
        syslog(LOG_ERR, "Player start failed: %d\n", ret);
    } else {
        syslog(LOG_INFO, "Player started successfully.\n");

        /* Set timer to stop playback after 10 seconds. */
        uv_timer_init(&priv->loop, &priv->stop_timer);
        priv->stop_timer.data = priv;
        uv_timer_start(&priv->stop_timer, start_timer_cb, 10000, 0);
    }
}
static void uv_player_prepare_cb(void *cookie, int ret) {
    struct uv_player_priv_s *priv = (struct uv_player_priv_s *)cookie;
    if (ret < 0) {
        syslog(LOG_ERR, "Player prepare failed: %d\n", ret);
        media_uv_player_close(priv->player, 0, uv_player_close_cb);
    } else {
        syslog(LOG_INFO, "Player prepared successfully.\n");
        g_priv.state = PLAYER_PREPARED;
        /*Call start after preparing the player successfully*/
        ret = media_uv_player_start(priv->player, uv_player_start_cb, priv);
        if (ret < 0) {
            syslog(LOG_ERR, "Player: start failed.\n");
            media_uv_player_close(priv->player, 0, uv_player_close_cb);
        }
    }
}

static void uv_player_open_cb(void* cookie, int ret) {
    struct uv_player_priv_s *priv = (struct uv_player_priv_s *)cookie;
    if (ret < 0) {
        syslog(LOG_ERR, "Player open failed: %d\n", ret);
    } else {
        syslog(LOG_INFO, "Player opened successfully.\n");
        /*Call prepare after opening the player successfully.*/
        ret = media_uv_player_prepare(priv->player, priv->file_path, NULL, NULL, uv_player_prepare_cb, priv);
        if (ret < 0) {
            syslog(LOG_ERR, "Player: prepare failed.\n");
            media_uv_player_close(priv->player, 0, uv_player_close_cb);
        }
    }
}

static void uv_player_set_volume_cb(void *cookie, int ret) {
    if (ret < 0) {
        syslog(LOG_ERR, "Set volume failed: %d\n", ret);
    } else {
        syslog(LOG_INFO, "Volume set successfully.\n");
    }
}

int main(int argc, char *argv[]) {
    pthread_t thread;
    struct uv_player_priv_s priv={0};
    int ret;

    /* Parse command line arguments */
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <input_file>\n", argv[0]);
        return EXIT_FAILURE;
    }

    priv.file_path = strdup(argv[1]);
    /* Initialize libuv loop before creating thread */
    memset(&priv.loop, 0, sizeof(priv.loop));

    ret = pthread_create(&thread, NULL, media_player_uvloop_thread, &priv);
    if (ret < 0) {
        syslog(LOG_ERR, "Failed to create thread: %d\n", ret);
        goto cleanup;
    }

    usleep(1000); /* let uvloop run. */

    if (sem_init(&g_priv.sem, 0, 0) != 0) {
        syslog(LOG_ERR, "Failed to initialize semaphore\n");
        goto cleanup;
    }

    priv.player = media_uv_player_open(&priv.loop, "Music", uv_player_open_cb, &priv);
    if (!priv.player) {
        syslog(LOG_ERR, "Player: open failed.\n");
        goto cleanup;
    }

    ret = media_uv_player_listen(priv.player, uv_player_event_cb);
    if (ret < 0) {
        syslog(LOG_ERR, "Player: listen failed.\n");
        goto cleanup;
    }

    ret = media_uv_player_set_volume(priv.player, 0.6, uv_player_set_volume_cb, &priv);
    if (ret < 0) {
        syslog(LOG_ERR, "Player: set_volume failed.\n");
        goto cleanup;
    }

cleanup:
    uv_async_send(&priv.asyncq);
    pthread_join(thread, NULL);

    /* Ensure all resources are cleaned up properly.*/
    if (priv.player)
        media_uv_player_close(priv.player, 0, uv_player_close_cb);

    free(priv.file_path);
    sem_destroy(&g_priv.sem);
    syslog(LOG_INFO, "Player: closed.\n");

    return 0;
}

```
