/****************************************************************************
 * frameworks/multimedia/media/server/media_video_output.h
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

#ifndef FRAMEWORKS_MEDIA_INCLUDE_MEDIA_VIDEO_OUTPUT_H
#define FRAMEWORKS_MEDIA_INCLUDE_MEDIA_VIDEO_OUTPUT_H

/****************************************************************************
 * Included Files
 ****************************************************************************/
#include "libavutil/dict.h"
#include "libavutil/frame.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/**
 * @brief Open the video output device
 *
 * This function initializes a video output device based on the provided parameters and creates a context object.
 * The context object will be returned via the pctx parameter and will be used for subsequent operations.
 *
 * @param pctx A pointer to a pointer to MediaVOutputContext.
 * @param options Configuration options for the video output device, passed as a string.
 *        eg: "format=fbdev:devname=/dev/fb0:width=640:height=480"
 * @return Returns 0 on success, or a negative error code on failure.
 */

typedef struct MediaVOutputContext MediaVOutputContext;

int media_video_output_open(MediaVOutputContext** pctx, AVDictionary* options);

/**
 * @brief Close the video output device
 *
 * This function is used to close the video output device.
 *
 * @param pctx Pointer to a pointer to MediaVOutputContext, used to release the context object.
 * @return Returns 0 on success, a negative error code on failure.
 */
int media_video_output_close(MediaVOutputContext** pctx);

/**
 * @brief Write a frame of video data to the video output device
 *
 * This function is used to write a frame of video data to the video output device.
 *
 * @param ctx Pointer to MediaVOutputContext.
 * @param frame Pointer to AVFrame, representing the video frame data to be written.
 * @return Returns 0 on success, a negative error code on failure.
 */
int media_video_output_write_frame(MediaVOutputContext* ctx, AVFrame* frame);

/**
 * @brief Get the pollfd for the video output device
 *
 * This function retrieves the file descriptors associated with the video output device
 * that can be used with the poll() system call. It populates the provided array of
 * struct pollfd with the relevant file descriptors and events to monitor.
 *
 * @param ctx Pointer to MediaVOutputContext.
 * @param fds Pointer to an array of struct pollfd where the file descriptors and events will be stored.
 * @param count The number of elements in the fds array.
 * @return Returns the number of valid file descriptors added to the fds array on success.
 *         Returns a negative error code on failure.
 */
int media_video_output_get_pollfd(MediaVOutputContext* ctx, struct pollfd* fds, int count);

/**
 * @brief Notify video output device poll available
 *
 * This function is used to notify if the video output device is available for I/O operations.
 *
 * @param ctx Pointer to MediaVOutputContext.
 * @param fds Pointer to a `struct pollfd` where the file descriptor and
 * events of the video output device will be stored.
 * @return Returns 0 on success, a negative error code on failure.
 */
int media_video_output_poll_available(MediaVOutputContext* ctx, struct pollfd* fds);

#endif /* FRAMEWORKS_MEDIA_INCLUDE_MEDIA_VIDEO_OUTPUT_H */
