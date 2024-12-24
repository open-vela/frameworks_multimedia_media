# *Media UV Recorder Demo*

## **Complete sample code**
```c
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <semaphore.h>
#include <media_api.h>
#include <linux/fs.h>
#include <uv.h>

#define RECORDER_IDLE      0
#define RECORDER_PREPARED  1
#define RECORDER_STARTED   2
#define RECORDER_COMPLETED 3
#define RECORDER_STOPPED   4

static struct recorder_global_s {
    sem_t sem;
    int state;
} g_priv = { .state = RECORDER_IDLE };

struct uv_recorder_priv_s {
    uv_loop_t loop;
    void *recorder;
    uv_async_t asyncq;
    uv_timer_t stop_timer; /*Timer to stop the recorder*/
    unsigned int duration_seconds; /*recording duration*/
    char *file_path;
    pthread_mutex_t mutex;
};

void uv_recorder_event_cb(void* cookie, int event,
    int ret, const char* extra)
{
    if (event == MEDIA_EVENT_PREPARED) {
        syslog(LOG_INFO, "MEDIA_EVENT_PREPARED\n");
        g_priv.state = RECORDER_PREPARED;
    } else if (event == MEDIA_EVENT_STARTED) {
        syslog(LOG_INFO, "MEDIA_EVENT_STARTED\n");
        g_priv.state = RECORDER_STARTED;
    } else if (event == MEDIA_EVENT_STOPPED) {
        syslog(LOG_INFO, "MEDIA_EVENT_STOPPED\n");
        g_priv.state = RECORDER_STOPPED;
    } else if (event == MEDIA_EVENT_COMPLETED) {
        syslog(LOG_INFO, "MEDIA_EVENT_COMPLETED\n");
        g_priv.state = RECORDER_COMPLETED;
    }
}
static void mediarecorder_uvasyncq_close_cb(uv_handle_t* handle) {
    printf("Bye-Bye!\n");
    uv_loop_t *loop = (uv_loop_t*)uv_req_get_data((const uv_req_t*)handle);
    if (uv_loop_alive(loop)) {
        uv_stop(loop);
    }
}
static void mediarecorder_uvasyncq_cb(uv_async_t* asyncq) {
    printf("mediarecorder_uvasyncq_cb!\n");
}

static void* media_recorder_uvloop_thread(void* arg) {
    struct uv_recorder_priv_s *priv = (struct uv_recorder_priv_s *)arg;
    int ret;

    ret = uv_loop_init(&priv->loop);
    if (ret < 0) {
        syslog(LOG_ERR, "Failed to initialize loop: %d\n", ret);
        return NULL;
    }

    pthread_mutex_init(&priv->mutex, NULL);

    priv->asyncq.data = arg;
    ret = uv_async_init(&priv->loop, &priv->asyncq, mediarecorder_uvasyncq_cb);
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

static void uv_recorder_close_cb(void *cookie, int ret) {
    struct uv_recorder_priv_s *priv = (struct uv_recorder_priv_s *)cookie;
    if (ret < 0) {
        syslog(LOG_ERR, "Recorder close failed: %d\n", ret);
    } else {
        syslog(LOG_INFO, "Recorder closed successfully.\n");
    }
    priv->recorder = NULL;
    /*Close asynchronous handle.*/
    uv_close((uv_handle_t*)&priv->asyncq, mediarecorder_uvasyncq_close_cb);
}
static void uv_recorder_stop_cb(void *cookie, int ret) {
    struct uv_recorder_priv_s *priv = (struct uv_recorder_priv_s *)cookie;
    if (ret < 0) {
        syslog(LOG_ERR, "Recorder stop failed: %d\n", ret);
    } else {
        syslog(LOG_INFO, "Recorder stopped successfully.\n");

        /* Protect access to the Recorder with a mutex lock.*/
        pthread_mutex_lock(&priv->mutex);
        ret = media_uv_recorder_close(priv->recorder, uv_recorder_close_cb);
        pthread_mutex_unlock(&priv->mutex);

        if (ret < 0) {
            syslog(LOG_ERR, "Recorder: close failed.\n");
        }
    }
}
static void start_timer_cb(uv_timer_t* handle) {
    struct uv_recorder_priv_s *priv = (struct uv_recorder_priv_s *)handle->data;
    int ret;

    pthread_mutex_lock(&priv->mutex);
    ret = media_uv_recorder_stop(priv->recorder, uv_recorder_stop_cb, priv);
    pthread_mutex_unlock(&priv->mutex);

    if (ret < 0) {
        syslog(LOG_ERR, "Recorder: stop failed.\n");
    }
}

static void uv_recorder_start_cb(void *cookie, int ret) {
    struct uv_recorder_priv_s *priv = (struct uv_recorder_priv_s *)cookie;
    if (ret < 0) {
        syslog(LOG_ERR, "Recorder start failed: %d\n", ret);
    } else {
        syslog(LOG_INFO, "Recorder started successfully.\n");

        /* Set timer to stop recording after duration_seconds. */
        uv_timer_init(&priv->loop, &priv->stop_timer);
        priv->stop_timer.data = priv;
        uv_timer_start(&priv->stop_timer, start_timer_cb, priv->duration_seconds * 1000, 0);
    }
}
static void uv_recorder_prepare_cb(void *cookie, int ret) {
    struct uv_recorder_priv_s *priv = (struct uv_recorder_priv_s *)cookie;
    if (ret < 0) {
        syslog(LOG_ERR, "Recorder prepare failed: %d\n", ret);
        media_uv_recorder_close(priv->recorder, uv_recorder_close_cb);
    } else {
        syslog(LOG_INFO, "Recorder prepared successfully.\n");
        g_priv.state = RECORDER_PREPARED;
        /*Call start after preparing the Recorder successfully*/
        ret = media_uv_recorder_start(priv->recorder, uv_recorder_start_cb, priv);
        if (ret < 0) {
            syslog(LOG_ERR, "Recorder: start failed.\n");
            media_uv_recorder_close(priv->recorder, uv_recorder_close_cb);
        }
    }
}

static void uv_recorder_open_cb(void* cookie, int ret) {
    struct uv_recorder_priv_s *priv = (struct uv_recorder_priv_s *)cookie;
    if (ret < 0) {
        syslog(LOG_ERR, "Recorder open failed: %d\n", ret);
    } else {
        syslog(LOG_INFO, "Recorder opened successfully.\n");
        /*Call prepare after opening the recorder successfully.*/
       ret = media_uv_recorder_prepare(priv->recorder, priv->file_path, "format=opus:sample_rate=16000:ch_layout=mono", NULL, uv_recorder_prepare_cb, priv);
        if (ret < 0) {
            syslog(LOG_ERR, "Recorder: prepare failed.\n");
            media_uv_recorder_close(priv->recorder, uv_recorder_close_cb);
        }
    }
}

int main(int argc, char *argv[]) {
    pthread_t thread;
    struct uv_recorder_priv_s priv = {0};
    int ret;

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <output_file> <duration_seconds>\n", argv[0]);
        return EXIT_FAILURE;
    }

    priv.file_path = strdup(argv[1]);
    if (!priv.file_path) {
        perror("strdup");
        return EXIT_FAILURE;
    }
    priv.duration_seconds = atoi(argv[2]);
    if (priv.duration_seconds <= 0) {
        fprintf(stderr, "Duration must be a positive integer.\n");
        free(priv.file_path);
        return EXIT_FAILURE;
    }
    /* Initialize libuv loop before creating thread */
    memset(&priv.loop, 0, sizeof(priv.loop));
    ret = pthread_create(&thread, NULL, media_recorder_uvloop_thread, &priv);
    if (ret < 0) {
        syslog(LOG_ERR, "Failed to create thread: %d\n", ret);
        goto cleanup;
    }

    usleep(1000); /* let uvloop run. */

    /* Initialize the recorder state and semaphore.*/
    if (sem_init(&g_priv.sem, 0, 0) != 0) {
        syslog(LOG_ERR, "Failed to initialize semaphore\n");
        goto cleanup;
    }

    /*Open the media recorder */
    priv.recorder = media_uv_recorder_open(&priv.loop, "cap", uv_recorder_open_cb, &priv);
    if (!priv.recorder) {
        syslog(LOG_ERR, "recorder: open failed.\n");
        goto cleanup;
    }
    /*Listen for recorder events*/
    ret = media_uv_recorder_listen(priv.recorder, uv_recorder_event_cb);

cleanup:
    uv_async_send(&priv.asyncq);
    pthread_join(thread, NULL);

    /* Ensure all resources are cleaned up properly.*/
    if (priv.recorder)
        media_uv_recorder_close(priv.recorder, uv_recorder_close_cb);

    free(priv.file_path);
    sem_destroy(&g_priv.sem);
    syslog(LOG_INFO, "Recorder: closed.\n");

    return 0;
}


```
