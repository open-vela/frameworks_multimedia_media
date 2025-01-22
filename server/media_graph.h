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
typedef struct MediaGraphStream MediaGraphStream;

int media_graph_stream_open(MediaGraphStream** pctx,
                            const char* stream_type,
                            int (*event_cb)(void* udata, int evt, int64_t args),
                            void* udata);
/*
 * Release a audio stream.
 * @ctx: [in,out] audio stream context
 * @return: 0 on success, negative value on error
 *
 * Note that it must also be called during pause. Get it again after resume.
 */
int media_graph_stream_close(MediaGraphStream** pctx);

/*
 * Set options for a audio stream.
 * @ctx: [in,out] audio stream context
 * @options: options string eg. "sample_fmt=1:ch_layout=stereo:sample_rate=48000"
 * @return: 0 on success, negative value on error
 *
 */

int media_graph_stream_set_options(MediaGraphStream* ctx, const char* options);

#endif /* FRAMEWORKS_MEDIA_INCLUDE_MEDIA_GRAPH_H */
