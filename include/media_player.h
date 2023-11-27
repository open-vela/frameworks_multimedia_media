/****************************************************************************
 * frameworks/media/include/media_player.h
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

/**
 * @brief Media Player Interface
 *
 * This interface provides functions for media playback.
 * here is an example for using these functions
 * @code
    #include <media_api.h>
    #include <stdio.h>
    #include <stdlib.h>
    #include <syslog.h>

    #define PLAYER_IDLE      0
    #define PLAYER_PREPARED  1
    #define PLAYER_STARTED   2
    #define PLAYER_COMPLETED 3
    #define PLAYER_STOPPED   4

    static struct player_priv_s
    {
    sem_t sem;
    int   state;
    } g_priv;

    static void music_event_callback(void* cookie, int event,
                                     int ret, const char *data)
    {
        char *str;
        if (event == MEDIA_EVENT_STARTED) {
            g_priv.state = PLAYER_STARTED;
            str = "MEDIA_EVENT_STARTED";
            sem_post(&g_priv.sem);
        } else if (event == MEDIA_EVENT_STOPPED) {
            str = "MEDIA_EVENT_STOPPED";
        } else if (event == MEDIA_EVENT_COMPLETED) {
            g_priv.state = PLAYER_COMPLETED;
            str = "MEDIA_EVENT_COMPLETED";
        } else if (event == MEDIA_EVENT_PREPARED) {
            g_priv.state = PLAYER_PREPARED;
            str = "MEDIA_EVENT_PREPARED";
            sem_post(&g_priv.sem);
        } else if (event == MEDIA_EVENT_PAUSED) {
            str = "MEDIA_EVENT_PAUSED";
        } else if (event == MEDIA_EVENT_SEEKED) {
            str = "MEDIA_EVENT_SEEKED";
        } else {
            str = "UNKNOW EVENT";
        }

        syslog(LOG_INFO, "%s, music event %s, event %d, ret %d,
               info %s line %d\n", __func__, str, event, ret,
               data ? data : "NULL", __LINE__);
    }

    int main(int argc, char *argv[])
    {
        unsigned int duration, position;
        float volume;
        void *player;
        int ret;

        sem_init(&g_priv.sem, 0, 0);
        g_priv.state = PLAYER_IDLE;

        player = media_player_open("Music");
        if (!player) {
            syslog(LOG_ERR, "Player: open failed.\n");
            return -1;
        }

        ret = media_player_set_event_callback(player, player,
                                              music_event_callback);
        if (ret < 0) {
            syslog(LOG_ERR, "Player: set callback failed.\n");
            goto out;
        }

        ret = media_player_set_volume(player, 0.2);
        if (ret < 0) {
            syslog(LOG_ERR, "Player: set_volume failed.\n");
            goto out;
        }

        ret = media_player_prepare(player, argv[1], NULL);
        if (ret < 0) {
            syslog(LOG_ERR, "Player: prepare failed.\n");
            goto out;
        }

        sem_wait(&g_priv.sem);

        if (g_priv.state != PLAYER_PREPARED) {
            syslog(LOG_INFO, "Player: prepare event return failed.\n");
            goto out;
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

        syslog(LOG_INFO, "Player: playing %d.\n",
               media_player_is_playing(player));
        media_player_get_duration(player, &duration);
        syslog(LOG_INFO, "Player: get_duration ret %d %d.\n",
               ret, duration);

        media_player_get_volume(player, &volume);
        syslog(LOG_INFO, "Player: get_volume %f.\n", volume);

        while (g_priv.state == PLAYER_STARTED) {
            media_player_get_position(player, &position);
            syslog(LOG_ERR, "Player: get_position %d.\n", position);
            usleep(300 * 1000);
        }

        ret = media_player_stop(player);
        if (ret < 0) {
            syslog(LOG_ERR, "Player: stop failed.\n");
            goto out;
        }

    out:
        media_player_close(player, 0);
        sem_destroy(&g_priv.sem);
        syslog(LOG_INFO, "Player: closed.\n");

        return 0;
    }
 * @endcond
 *
 * @file media_player.h
 *
 */

#ifndef FRAMEWORKS_MEDIA_INCLUDE_MEDIA_PLAYER_H
#define FRAMEWORKS_MEDIA_INCLUDE_MEDIA_PLAYER_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <media_event.h>
#include <media_stream.h>
#include <stddef.h>
#include <sys/socket.h>

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C" {
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/**
 * @brief Open media with player type.
 * @param[in] stream    Stream type, @see MEDIA_STREAM_* in media_wrapper.h.)
 * @return Pointer to created handle on success; NULL on failure.
 */
void* media_player_open(const char* stream);

/**
 * @brief Close the player with handle.
 * @param[in] handle       The player path to be destroyed, return value of
 * media_player_open function.
 * @param[in] pending_stop whether pending command.
 *                         - 0: close immediately
 *                         - 1: pending command,
 *                              close automatically after playback complete
 * @return Zero on success; a negated errno value on failure.
 */
int media_player_close(void* handle, int pending_stop);

/**
 * @brief  Set event callback to the player path, the callback will be
 * called when state changed.
 * @param[in] handle    The player path, return value of media_player_open
 * function.
 * @param[in] cookie    User cookie, will be brought back to user when do
 * event_cb
 * @param[in] event_cb  The function pointer of event callback
 * @return Zero on success; a negated errno value on failure.
 */
int media_player_set_event_callback(void* handle, void* cookie,
    media_event_callback event_cb);

/**
 * @brief Prepare resource file to be played by media.
 * @param[in] handle    The player path, return value of media_player_open
 * function.
 * @param[in] url       the path of resource file to be played by media, can
 * be a local path(e.g. /music/1.mp3) or an online path(e.g.
 * http://10.221.110.236/1.mp3) or NULL(buffer mode).
 * @param[in] options   Extra options configure
 *   - If url is not NULL: options is the url addtional description
 *   - If url is NULL: options descript "buffer" mode, exmaple:
 *     - "format=s16le,sample_rate=44100,channels=2"
 *     - "format=unknown"
 *   then use media_player_write_data() or fd returned by
 *   media_player_get_socket() to write data
 * @return Zero on success; a negated errno value on failure.
 */
int media_player_prepare(void* handle, const char* url, const char* options);

/**
 * @brief Reset media with player type.
 * @param[in] handle The player path, return value of media_player_open
 * function.
 * @return Zero on success; a negated errno value on failure.
 */
int media_player_reset(void* handle);

/**
 * @brief Write data to media with player type.
 * Used only in buffer mode, need media_player_prepare() url set to NULL.
 * Only need to choose either this function or the media_player_get_socket()
 * to transfer data for the media.
 * @note: this function is blocked
 * @param[in] handle    The player path, return value of media_player_open
 * function.
 * @param[in] data      Buffer will be played
 * @param[in] len       Buffer len
 * @return Actly sent len on succeed; a negated errno value on failure.
 */
ssize_t media_player_write_data(void* handle, const void* data, size_t len);

/**
 * @brief Get sockaddr for unblock mode write data.
 * @deprecated This function is deprecated now, please use the new function
 * media_player_get_socket() instead.
 * @param[in] handle    The player path, return value of media_player_open
 * function.
 * @param[in] addr      The sockaddr pointer
 * @return Actly sent len on succeed; a negated errno value on failure.
 */
int media_player_get_sockaddr(void* handle, struct sockaddr_storage* addr);

/**
 * @brief Get socket fd for unblock mode write data.
 *
 * @code
 *  int  ret;
 *  void *handle;
 *  handle = media_player_open(MEDIA_STREAM_MUSIC);
 *  // set event callback
 *  // set prepare mode buffer
 *  ret = media_player_prepare(handle, NULL, NULL);
 *  int socketfd = media_player_get_socket(handle);
 * @endcode
 *
 * @param[in] handle    The player handle.
 * @return fd; a negated errno value on failure.
 */
int media_player_get_socket(void* handle);

/**
 * @brief Close socket fd when player finish read data.
 *
 * @param[in] handle    The player handle.
 */
void media_player_close_socket(void* handle);

/**
 * Start the player path to play
 * @param[in] handle    The player path
 * @return Zero on success; a negated errno value on failure.
 */
int media_player_start(void* handle);

/**
 * Stop the player path
 * @param[in] handle    The player path
 * @return Zero on success; a negated errno value on failure.
 */
int media_player_stop(void* handle);

/**
 * Pause the player path
 * @param[in] handle    The player path
 * @return Zero on success; a negated errno value on failure.
 */
int media_player_pause(void* handle);

/**
 * Seek to msec position from begining
 * @param[in] handle    The player path
 * @param[in] mesc      Which postion should seek from begining
 * @return Zero on success; a negated errno value on failure.
 */
int media_player_seek(void* handle, unsigned int msec);

/**
 * Set the player path looping play, defult is not looping
 * @param[in] handle    The player path
 * @param[in] loop      Loop count (-1: forever, 0: not loop)
 * @return Zero on success; a negated errno value on failure.
 */
int media_player_set_looping(void* handle, int loop);

/**
 * @brief Check if the media player is currently playing.
 *
 * @code
 *  // Example
 *  void *handle;
 *  int ret;
 *  handle = media_player_open("Music");
 *  // set event callback
 *  ret = media_player_prepare(handle, "/music/1.mp3", NULL);
 *  ret = media_player_start(handle);
 *  ret = media_player_is_playing(handle);
 *  syslog(LOG_INFO, "Player: playing %d.\n", ret);
 * @endcode
 *
 * @param[in] handle    The player handle.
 * @return 1 on playing, 0 on not-playing, else on error.
 */
int media_player_is_playing(void* handle);

/**
 * @brief Get the playing duration after the music starts playing.
 *
 * @code
 *  void *handle;
 *  int ret;
 *  unsigned int position;
 *  handle = media_player_open(MEDIA_STREAM_MUSIC);
 *  // set event callback
 *  ret = media_player_prepare(handle, "/music/1.mp3", NULL);
 *  ret = media_player_start(handle);
 *  ret = media_player_get_position(handle, &position);
 * @endcode
 *
 * @param[in] handle    The player handle.
 * @param[in] mesc      Playback postion (from begining)
 * @return Zero on success; a negated errno value on failure.
 */
int media_player_get_position(void* handle, unsigned int* msec);

/**
 * @brief Get playback file duration (Total play time).
 *
 * @code
 *  void *handle;
 *  int ret;
 *  unsigned int duration;
 *  handle = media_player_open(MEDIA_STREAM_MUSIC);
 *  // set event callback
 *  ret = media_player_prepare(handle, "/music/1.mp3", NULL);
 *  ret = media_player_start(handle);
 *  ret = media_player_get_duration(handle, &duration);
 * @endcode
 *
 * @param[in] handle    The player handle.
 * @param[in] mesc      Store playing time of the media.
 * @param[in] mesc      File duration
 * @return Zero on success; a negated errno value on failure.
 */
int media_player_get_duration(void* handle, unsigned int* msec);

/**
 * @brief Set the player path volume.
 *
 * @code
 *  void *handle;
 *  int ret;
 *  handle = media_player_open(MEDIA_STREAM_MUSIC);
 *  ret = media_player_set_volume(handle, 0.2);
 * @endcode
 *
 * @param[in] handle    The player handle.
 * @param[in] volume    Volume with range of 0.0 - 1.0
 * @return Zero on success; a negated errno value on failure.
 */
int media_player_set_volume(void* handle, float volume);

/**
 * @brief Get the player handle volume.
 *
 * @code
 *  void *handle;
 *  int ret;
 *  float volume;
 *  handle = media_player_open(MEDIA_STREAM_MUSIC);
 *  // set event callback
 *  ret = media_player_prepare(handle, "/music/1.mp3", NULL);
 *  ret = media_player_start(handle);
 *  ret = media_player_get_volume(player, &volume);
 * @endcode
 *
 * @param[in] handle    The player path
 * @param[in] volume    Volume with range of 0.0 - 1.0
 * @return Zero on success; a negated errno value on failure.
 */
int media_player_get_volume(void* handle, float* volume);

/**
 * @brief Set properties of the player path.
 *
 * @param[in] handle      Current player path
 * @param[in] target      Target filter
 * @param[in] key         Key
 * @param[in] value       Value
 * @return Zero on success; a negated errno value on failure.
 */
int media_player_set_property(void* handle, const char* target, const char* key, const char* value);

/**
 * @brief Get properties of the player path.
 *
 * @param[in] handle      Current player path
 * @param[in] target      Target filter
 * @param[in] key         Key
 * @param[in] value       Buffer of value
 * @param[in] value_len   Buffer length of value
 * @return Zero on success; a negated errno value on failure.
 */
int media_player_get_property(void* handle, const char* target, const char* key, char* value, int value_len);

#ifdef CONFIG_LIBUV
/**
 * @brief Open an async player.
 *
 * @param[in] loop          Loop handle of current thread.
 * @param[in] stream        Stream type, @see MEDIA_STREAM_* .
 * @param[in] on_open       Open callback, called after open is done.
 * @param[in] cookie        Long-term callback context for:
 *                          on_open, on_event, on_close.
 * @return void* Handle of player.
 */
void* media_uv_player_open(void* loop, const char* stream,
    media_uv_callback on_open, void* cookie);

/**
 * @brief Listen to status change event by setting callback.
 *
 * @param[in] handle    Async player handle.
 * @param[in] on_event  Event callback, call after receiving notification.
 * @return int Zero on success, negative errno on failure.
 */
int media_uv_player_listen(void* handle, media_event_callback on_event);

/**
 * @brief Close the async player.
 *
 * @param[in] handle    Async player handle.
 * @param[in] pending   Pending close or not.
 * @param[in] on_close  Release callback, called after releasing internal resources.
 * @return int Zero on success, negative errno on illegal handle.
 */
int media_uv_player_close(void* handle, int pending,
    media_uv_callback on_close);

/**
 * @brief Prepare resource file.
 *
 * @param[in] handle        Async player handle.
 * @param[in] url           Path of resources.
 * @param[in] options       Resource options, @see media_player_prpare.
 * @param[in] on_prepare    Call after receiving result, will give an uv_pipe_t
 *                          to write data in buffer mode.
 * @param[in] cookie        One-time callback context.
 * @return int Zero on success, negative errno on failure.
 */
int media_uv_player_prepare(void* handle, const char* url, const char* options,
    media_uv_object_callback on_prepare, void* cookie);

/**
 * @brief  Play or resume the prepared source with auto focus request.
 *
 * @param handle    Async player handle.
 * @param on_play   Call after receiving result or focus request failed.
 * @param cookie    One-time callback context.
 * @return int Zero on success, negative errno on failure.
 */
int media_uv_player_play(void* handle, media_uv_callback on_play, void* cookie);

/**
 * @brief Play or resume the prepared resouce.
 *
 * @param[in] handle    Async player handle.
 * @param[in] on_start  Call after receiving result.
 * @param[in] cookie    One-time callback context.
 * @return int Zero on success, negative errno on failure.
 */
int media_uv_player_start(void* handle, media_uv_callback on_start, void* cookie);

/**
 * @brief Pause the playing.
 *
 * @param[in] handle    Async player handle.
 * @param[in] on_pause  Call after receiving result.
 * @param[in] cookie    One-time callback context.
 * @return int Zero on success, negative errno on failure.
 */
int media_uv_player_pause(void* handle, media_uv_callback on_pause, void* cookie);

/**
 * @brief Stop the playing, clear the prepared resource file.
 *
 * @param[in] handle    Async player handle.
 * @param[in] on_stop   Call after receiving result.
 * @param[in] cookie    One-time callback context.
 * @return int Zero on success, negative errno on failure.
 */
int media_uv_player_stop(void* handle, media_uv_callback on_stop, void* cookie);

/**
 * @brief Set player volume.
 *
 * @param[in] handle    Async player handle.
 * @param[in] volume    Volume in [0.0, 1.0].
 * @param[in] on_volume Call after receiving result.
 * @param[in] cookie    One-time callback context.
 * @return int Zero on success, negative errno on failure.
 */
int media_uv_player_set_volume(void* handle, float volume,
    media_uv_callback on_volume, void* cookie);

/**
 * @brief Get current playing status.
 *
 * @param handle        Async player handle.
 * @param on_playing    Call after receiving result.
 * @param cookie        One-time callback context.
 * @return int Zero on success, negative errno on failure.
 */
int media_uv_player_get_playing(void* handle,
    media_uv_int_callback on_playing, void* cookie);

/**
 * @brief Get current playing position.
 *
 * @param[in] handle        Async player handle.
 * @param[in] on_position   Call after receiving result.
 * @param[in] cookie        One-time callback context.
 * @return int Zero on success, negative errno on failure.
 */
int media_uv_player_get_position(void* handle,
    media_uv_unsigned_callback on_position, void* cookie);

/**
 * @brief Get duration of current playing resource.
 *
 * @param handle        Async player handle.
 * @param on_duration   Call after receiving result.
 * @param cookie        One-time callback context.
 * @return int Zero on success, negative errno on failure.
 */
int media_uv_player_get_duration(void* handle,
    media_uv_unsigned_callback on_duration, void* cookie);
#endif /* CONFIG_LIBUV */

#undef EXTERN
#ifdef __cplusplus
}
#endif
#endif /* FRAMEWORKS_MEDIA_INCLUDE_MEDIA_PLAYER_H */
