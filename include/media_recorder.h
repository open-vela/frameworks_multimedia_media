/****************************************************************************
 * frameworks/media/include/media_recorder.h
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

#ifndef FRAMEWORKS_MEDIA_INCLUDE_MEDIA_RECORDER_H
#define FRAMEWORKS_MEDIA_INCLUDE_MEDIA_RECORDER_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <media_event.h>

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Name: media_recorder_open
 *
 * Description:
 *   Open one recorder path.
 *
 * Input Parameters:
 *   params - open params
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

void *media_recorder_open(const char *params);

/****************************************************************************
 * Name: media_recorder_close
 *
 * Description:
 *  Close the recorder path.
 *
 * Input Parameters:
 *   handle - The recorder path
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_recorder_close(void *handle);

/****************************************************************************
 * Name: media_recorder_set_event_callback
 *
 * Description:
 *  Set event callback to the recorder path, the callback will be called
 *  when state changed or something user cares.
 *
 * Input Parameters:
 *   handle - The recorder path
 *   cookie - User cookie, will bring back to user when do event_cb
 *   event_cb - Event callback
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_recorder_set_event_callback(void *handle, void *cookie,
                                      media_event_callback event_cb);

/****************************************************************************
 * Name: media_recorder_prepare
 *
 * Description:
 *  Prepare the recorder path.
 *
 * Input Parameters:
 *   handle - Current recorder path
 *   url    - Data dest recorderd to
 *   options  - Extra configure
 *            If url is not NULL: options is the url addtional description
 *            If url is NULL: options descript "buffer" mode,
 *                            e.g. format=s16le,sample_rate=44100,channels=2
 *                            e.g. format=unknown
 *                            then use media_recorder_read_data() read data
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_recorder_prepare(void *handle, const char *url, const char *options);

/****************************************************************************
 * Name: media_recorder_reset
 *
 * Description:
 *  Reset the recorder path.
 *
 * Input Parameters:
 *   handle - The recorder path
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_recorder_reset(void *handle);

/****************************************************************************
 * Name: media_recorder_read_data
 *
 * Description:
 *  Read recorderd data from the recorder path.
 *  This need media_recorder_prepare() url set to NULL.
 *
 * Input Parameters:
 *   handle - Current recorder path
 *   data   - Buffer will be recorderd to
 *   len    - Buffer len
 *
 * Returned Value:
 *   returned >= 0, Actly got len; a negated errno value on failure.
 *
 ****************************************************************************/

ssize_t media_recorder_read_data(void *handle, void *data, size_t len);

/****************************************************************************
 * Name: media_recorder_start
 *
 * Description:
 *  Start the recorder path.
 *
 * Input Parameters:
 *   handle - Current recorder path
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_recorder_start(void *handle);

/****************************************************************************
 * Name: media_recorder_stop
 *
 * Description:
 *  Stop the recorder path.
 *
 * Input Parameters:
 *   handle - Current recorder path
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_recorder_stop(void *handle);

#undef EXTERN
#ifdef __cplusplus
}
#endif
#endif /* FRAMEWORKS_MEDIA_INCLUDE_MEDIA_RECORDER_H */
