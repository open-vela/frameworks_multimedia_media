/****************************************************************************
 * frameworks/media/include/audio_graph.h
 *
 * Copyright (C) 2020 Xiaomi Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef FRAMEWORKS_MEDIA_INCLUDE_AUDIO_GRAPH_H
#define FRAMEWORKS_MEDIA_INCLUDE_AUDIO_GRAPH_H

/****************************************************************************
 * Included Files
 ****************************************************************************/
#include <stdint.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/*
 * Open a audio stream.
 * @ctx: [in,out] audio stream context
 * @stream_type: stream type, eg. "abuffer@Music0"
 * @on_event_cb: callback function
 * @udata: on_event_cb user data
 * @return: 0 on success, negative value on error
 *
 * on_event_cb: callback function
 *      @udata: user data
 *      @evt: event type
 *      @args: event arguments
 */

int audio_graph_open(AVFilterContext** src, const char* stream_type);

/*
 * Start a audio stream.
 * @ctx: [in,out] audio stream context
 * @format: audio format, eg. AV_SAMPLE_FMT_S16
 * @sample_rate: sample rate
 * @channels: number of channels
 * @on_event_cb: callback function
 * @udata: on_event_cb user data
 * @return: 0 on success, negative value on error
 *
 * on_event_cb: callback function
 *      @udata: user data
 *      @evt: event type
 *      @args: event arguments
 */
int audio_graph_start(AVFilterContext* src, int format, int sample_rate, int channels,
    int (*on_event_cb)(void* udata, int evt, int64_t args), void* udata);

/*
 * Stop a audio stream.
 * @ctx: [in,out] audio stream context
 * @return: 0 on success, negative value on error
 *
 * Note that it must also be called during pause. Get it again after resume.
 */
int audio_graph_stop(AVFilterContext* src);

/*
 * Release a audio stream.
 * @ctx: [in,out] audio stream context
 * @return: 0 on success, negative value on error
 *
 * Note that it must also be called during pause. Get it again after resume.
 */
int audio_graph_close(AVFilterContext** src);

/*
 * Pause the audio stream and all downstream connected filters.
 * Typically called by the player when an audio underflow occurs (e.g., decode queue is empty),
 * to pause audio graph data consumption and prevent invalid playback or resource waste.
 * @ctx: [in,out] audio stream context
 * @return: 0 on success, negative value on error
 */
int audio_graph_pause(AVFilterContext* src);

/*
 * Resume the audio stream and all downstream connected filters.
 * Typically called by the player when the audio data queue is refilled,
 * to resume audio graph data consumption and continue playback.
 * @ctx: [in,out] audio stream context
 * @return: 0 on success, negative value on error
 */
int audio_graph_resume(AVFilterContext* src);

/*
 * Set stream parameter.
 * @ctx: [in,out] audio stream context
 * @param: [in] parameter key
 * @value: [in] parameter value
 * @return: 0 on success, negative value on error
 */
int audio_graph_set_parameter(AVFilterContext* src, const char* param, const char* value);

/*
 * Get stream parameter value.
 * @ctx: [in] audio stream context
 * @param: [in] parameter key
 * @res: [in, out]parameter value
 * @res_len: [in] parameter value length
 * @return: 0 on success, negative value on error
 */
int audio_graph_get_parameter(AVFilterContext* src, const char* key, char* res, int res_len);

#endif /* FRAMEWORKS_MEDIA_INCLUDE_AUDIO_GRAPH_H */
