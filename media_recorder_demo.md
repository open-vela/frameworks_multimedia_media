# *Media Recorder Demo*

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
#include <sys/time.h>
#include <sys/mount.h>
#include <linux/fs.h>

#define RECORDER_IDLE      0
#define RECORDER_PREPARED  1
#define RECORDER_STARTED   2
#define RECORDER_COMPLETED 3
#define RECORDER_STOPPED   4
#define RECORDER_DEFAULT_SIZE 512

static struct recorder_priv_s {
    sem_t sem;
    int state;
} g_priv;

struct recorder_thread_info_s {
    void *handle;
    char *file;
};

static void recorder_event_callback(void* cookie, int event, int ret, const char *data) {
    char *str;

    if (event == MEDIA_EVENT_STARTED) {
        g_priv.state = RECORDER_STARTED;
        str = "MEDIA_EVENT_STARTED";
        sem_post(&g_priv.sem);
    } else if (event == MEDIA_EVENT_STOPPED) {
        g_priv.state = RECORDER_STOPPED;
        str = "MEDIA_EVENT_STOPPED";
    } else if (event == MEDIA_EVENT_COMPLETED) {
        g_priv.state = RECORDER_COMPLETED;
        str = "MEDIA_EVENT_COMPLETED";
    } else if (event == MEDIA_EVENT_PREPARED) {
        g_priv.state = RECORDER_PREPARED;
        str = "MEDIA_EVENT_PREPARED";
        sem_post(&g_priv.sem);
    } else if (event == MEDIA_EVENT_PAUSED) {
        str = "MEDIA_EVENT_PAUSED";
    } else {
        str = "UNKNOWN EVENT";
    }

    syslog(LOG_INFO, "%s, record event %s, event %d, ret %d, info %s line %d\n",
            __func__, str, event, ret, data ? data : "NULL", __LINE__);
}

static void *recorder_read_thread(void *arg) {
    struct recorder_thread_info_s *info = (struct recorder_thread_info_s *)arg;
    char *buffer = NULL;
    int fd = -1;
    ssize_t ret;

    fd = open(info->file, O_CREAT | O_RDWR | O_CLOEXEC | O_TRUNC, 0666);
    if (fd < 0) {
        syslog(LOG_ERR, "Recorder: open file failed.\n");
        goto out;
    }

    buffer = malloc(RECORDER_DEFAULT_SIZE);
    if (!buffer) goto out;

    while (1) {
        ret = media_recorder_read_data(info->handle, buffer, RECORDER_DEFAULT_SIZE);
        if (ret <= 0) break;

        write(fd, buffer, ret);
    }

out:
    free(buffer);
    if (fd >= 0) close(fd);
    return NULL;
}

int main(int argc, char *argv[]) {
    pthread_t thread;
    struct recorder_thread_info_s thread_info;
    char res[128] = {0};
    int ret;
    unsigned current;
    void *recorder;
    int buffer_mode_enabled = 0;

    /* Parse command line arguments */
    if (argc != 4 || (strcmp(argv[3], "url") && strcmp(argv[3], "buffer"))) {
        fprintf(stderr, "Usage: %s <output file> <duration in seconds> [url|buffer]\n", argv[0]);
        return EXIT_FAILURE;
    }
    buffer_mode_enabled = !strcmp(argv[3], "buffer");

    unsigned int duration_seconds = atoi(argv[2]);
    if (duration_seconds <= 0) {
        fprintf(stderr, "Duration must be a positive integer.\n");
        return EXIT_FAILURE;
    }

    sem_init(&g_priv.sem, 0, 0);
    g_priv.state = RECORDER_IDLE;

    recorder = media_recorder_open("cap");
    if (!recorder) {
        syslog(LOG_ERR, "Recorder: create failed.\n");
        return -1;
    }

    ret = media_recorder_set_event_callback(recorder, recorder, recorder_event_callback);
    if (ret < 0) {
        syslog(LOG_ERR, "Recorder: set callback failed. \n");
        goto out;
    }

    /* Prepare the recorder with the appropriate parameters */
    ret = media_recorder_prepare(recorder, buffer_mode_enabled ? NULL : argv[1], "format=opus:sample_rate=16000:ch_layout=mono");
    if (ret < 0) {
        syslog(LOG_ERR, "Recorder: prepare failed. \n");
        goto out;
    }

    sem_wait(&g_priv.sem);

    if (g_priv.state != RECORDER_PREPARED) {
        syslog(LOG_ERR, "Recorder: prepare event return failed.\n");
        goto out;
    }

    if (buffer_mode_enabled) {
        thread_info.handle = recorder;
        thread_info.file = argv[1];
        ret = pthread_create(&thread, NULL, recorder_read_thread, &thread_info);
        if (ret != 0) {
            syslog(LOG_ERR, "Recorder: create thread failed.\n");
            goto out;
        }
        pthread_detach(thread); /* Detach the thread so it cleans up after itself. */
    }

    ret = media_recorder_start(recorder);
    if (ret < 0) {
        syslog(LOG_ERR, "Recorder: start failed. \n");
        goto out;
    }

    sem_wait(&g_priv.sem);

    if (g_priv.state != RECORDER_STARTED) {
        syslog(LOG_ERR, "RECORDER: start event return failed.\n");
        goto out;
    }

    for (unsigned int cnt = 0; cnt < duration_seconds; ++cnt) {
        sleep(1);

        ret = media_recorder_get_property(recorder, "amoviesink_async", "get_position", res, sizeof(res));
        if (!ret) {
            current = strtoul(res, NULL, 0);
            syslog(LOG_INFO, "current position: %u ms\n", current);
        } else {
            syslog(LOG_WARNING, "Failed to get current position.\n");
        }
    }

    ret = media_recorder_stop(recorder);
    if (ret < 0) {
        syslog(LOG_ERR, "Recorder: stop failed. \n");
        goto out;
    }

out:
    media_recorder_close(recorder);
    sem_destroy(&g_priv.sem);
    return ret < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
```
