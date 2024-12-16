/****************************************************************************
 * frameworks/media/include/media_graph.h
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

#ifndef FRAMEWORKS_MEDIA_INCLUDE_MEDIA_GRAPH_H
#define FRAMEWORKS_MEDIA_INCLUDE_MEDIA_GRAPH_H

/****************************************************************************
 * Included Files
 ****************************************************************************/
#include <stdint.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/
enum MediaGraphTrackEvent {
    MEDIA_GRAPH_EVT_NEED_FRAME = 0,
    MEDIA_GRAPH_EVT_EMIT_FRAME,
    MEDIA_GRAPH_EVT_CMD_UNLINK,
};
/*
 * Open a audio track.
 * @ctx: [in,out] audio track context
 * @stream_type: stream type, eg. "abuffer@Music0"
 * @format: audio format, eg. AV_SAMPLE_FMT_S16 and -1 means unknown
 * @sample_rate: sample rate, eg. 44100 and 0 means unknown
 * @channels: channels, eg. 2 and 0 means unknown
 * @on_event_cb: callback function
 * @udata: on_event_cb user data
 * @return: 0 on success, negative value on error
 *
 * on_event_cb: callback function
 *      @udata: user data
 *      @evt: event eg.
 *        EVT_NEED_DATA, source need more data
 *        EVT_CMD_UNLINK，AS tell PS has unlink-event happened
 *        EVT_EMIT_DATA, if adevsrc was registed cb, means a frame genareted
 *      @args: event arguments
 */
typedef struct MediaGraphTrack MediaGraphTrack;
int media_graph_track_open(MediaGraphTrack **pctx, const char *stream_type,
    int format, int sample_rate, int channels,
    int (*on_event_cb)(void *udata, int evt, int64_t args), void *udata);
/*
 * Release a audio track.
 * @ctx: [in,out] audio track context
 * @return: 0 on success, negative value on error
 *
 * Note that it must also be called during pause. Get it again after resume.
 */
int media_graph_track_close(MediaGraphTrack **pctx);

#endif /* FRAMEWORKS_MEDIA_INCLUDE_MEDIA_GRAPH_H */
