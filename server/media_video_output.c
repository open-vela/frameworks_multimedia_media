/****************************************************************************
 * frameworks/multimedia/media/server/media_video_output.c
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

/****************************************************************************
 * Included Files
 ****************************************************************************/
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "media_common.h"
#include "media_video_output.h"

#include "config.h"
#include "libavcodec/avcodec.h"
#include "libavdevice/avdevice.h"
#include "libavformat/avformat.h"
#include "libavutil/imgutils.h"
#include "libavutil/pixdesc.h"

#if CONFIG_SWSCALE
#include "libswscale/swscale.h"
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/
typedef struct MediaVOutputContext {
    AVFormatContext* fmt_ctx;
#if CONFIG_SWSCALE
    struct SwsContext* sws_ctx;
#endif

    enum AVPixelFormat pix_fmt; /**< output pixel format*/
    int width; /**< output frame width */
    int height; /**< output frame width */

    int started;
    int brescale;
} MediaVOutputContext;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#if CONFIG_SWSCALE
static int media_video_output_scale(MediaVOutputContext* ctx, AVFrame* frame, AVFrame* dst_frame)
{
    int ret;
    ret = av_frame_copy_props(dst_frame, frame);
    if (ret < 0) {
        MEDIA_ERR("Failed to copy frame props\n");
        goto err;
    }

    dst_frame->width = ctx->width;
    dst_frame->height = ctx->height;
    dst_frame->format = ctx->pix_fmt;
    ret = av_frame_get_buffer(dst_frame, 0);
    if (ret < 0) {
        MEDIA_ERR("Failed to allocate dst frame data\n");
        goto err;
    }

    ret = sws_scale(ctx->sws_ctx, (const uint8_t* const*)frame->data,
        frame->linesize, 0, frame->height,
        dst_frame->data, dst_frame->linesize);
    if (ret < 0) {
        MEDIA_ERR("Failed to scale frame\n");
        goto err;
    }

    return 0;

err:
    return ret;
}

static int media_video_output_scale_init(MediaVOutputContext* ctx, AVFrame* frame)
{
    ctx->sws_ctx = sws_getContext(frame->width, frame->height, frame->format,
        ctx->width, ctx->height, ctx->pix_fmt,
        SWS_BILINEAR, NULL, NULL, NULL);
    if (!ctx->sws_ctx) {
        MEDIA_ERR("Failed to allocate sws context\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

static void media_video_output_scale_uninit(MediaVOutputContext* ctx)
{
    if (ctx && ctx->sws_ctx) {
        sws_freeContext(ctx->sws_ctx);
        ctx->sws_ctx = NULL;
    }
}
#endif

static int media_video_output_control_message(struct AVFormatContext* s, int type,
    void* data, size_t data_size)
{
    MEDIA_INFO("VOutput control message: type = %d\n", type);
    return 0;
}

static int media_video_output_start(MediaVOutputContext* ctx, AVFrame* frame)
{
    const AVPixFmtDescriptor* pixdesc;
    AVStream* st;
    int ret;

    /* if output device parameters are invalid or no configure,
       making output parameters same as input frame. */
    if (ctx->width <= 0)
        ctx->width = frame->width;

    if (ctx->height <= 0)
        ctx->height = frame->height;

    if (ctx->pix_fmt == AV_PIX_FMT_NONE)
        ctx->pix_fmt = frame->format;

    st = ctx->fmt_ctx->streams[0];
    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id = AV_CODEC_ID_RAWVIDEO;
    st->codecpar->format = ctx->pix_fmt;
    st->codecpar->width = ctx->width;
    st->codecpar->height = ctx->height;
    pixdesc = av_pix_fmt_desc_get(ctx->pix_fmt);
    if (!pixdesc) {
        MEDIA_ERR("Invalid pixel format\n");
        return AVERROR(EINVAL);
    }
    st->codecpar->bits_per_coded_sample = av_get_bits_per_pixel(pixdesc);

    ret = avformat_write_header(ctx->fmt_ctx, NULL);
    if (ret < 0) {
        MEDIA_ERR("Failed to write header: %s\n", av_err2str(ret));
        return ret;
    }

#if CONFIG_SWSCALE
    if (frame->width != ctx->width || frame->height != ctx->height || frame->format != ctx->pix_fmt) {
        ret = media_video_output_scale_init(ctx, frame);
        if (ret < 0) {
            MEDIA_ERR("Failed to init scale %d.\n", ret);
            return ret;
        }

        ctx->brescale = 1;
    }
#endif

    ctx->started = 1;

    return 0;
}

static int media_video_output_stop(MediaVOutputContext* ctx)
{
    if (!ctx)
        return -EINVAL;

    if (ctx->fmt_ctx)
        av_write_trailer(ctx->fmt_ctx);
    ctx->started = 0;
    return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int media_video_output_get_pollfd(MediaVOutputContext* ctx, struct pollfd* fds, int count)
{
    int ret = 0;
    int n = 0;
    ret = avdevice_app_to_dev_control_message(ctx->fmt_ctx,
        AV_APP_TO_DEV_GET_POLLFD,
        fds, (count - n) * sizeof(struct pollfd));
    if (ret > 0)
        n += ret;
    return n;
}

int media_video_output_poll_available(MediaVOutputContext* ctx, struct pollfd* fds)
{
    return avdevice_app_to_dev_control_message(ctx->fmt_ctx,
        AV_APP_TO_DEV_POLL_AVAILABLE,
        fds, sizeof(struct pollfd));
}

int media_video_output_write_frame(MediaVOutputContext* ctx, AVFrame* frame)
{
    AVFrame* dst_frame = NULL;
    int ret;

    if (ctx->started == 0) {
        ret = media_video_output_start(ctx, frame);
        if (ret < 0) {
            MEDIA_ERR("Failed to start: %s\n", av_err2str(ret));
            av_frame_free(&frame);
            return ret;
        }
    }

    if (ctx->brescale) {
        dst_frame = av_frame_alloc();
        if (!dst_frame) {
            MEDIA_ERR("Failed to allocate dst frame\n");
            av_frame_free(&frame);
            return AVERROR(ENOMEM);
        }
#if CONFIG_SWSCALE
        ret = media_video_output_scale(ctx, frame, dst_frame);
        if (ret < 0) {
            MEDIA_ERR("Failed to scale frame: %s\n", av_err2str(ret));
            av_frame_free(&dst_frame);
            av_frame_free(&frame);
            return ret;
        }
#endif
        ret = av_write_uncoded_frame(ctx->fmt_ctx, 0, dst_frame);
        av_frame_free(&frame);
        return ret;
    }

    return av_write_uncoded_frame(ctx->fmt_ctx, 0, frame);
}

int media_video_output_open(MediaVOutputContext** pctx, AVDictionary* options)
{
    AVDictionary* tmp_options = NULL;
    AVDictionaryEntry* tag;
    char* devname = NULL;
    char* format = NULL;
    char* endptr;
    AVStream* st;
    int ret;

    MediaVOutputContext* ctx = av_mallocz(sizeof(MediaVOutputContext));
    if (!ctx) {
        MEDIA_ERR("Failed to malloc memory for VOutput Context.\n");
        return -ENOMEM;
    }

    av_dict_copy(&tmp_options, options, 0);

    if ((tag = av_dict_get(tmp_options, "format", NULL, 0))) {
        format = tag->value;
    } else {
        MEDIA_ERR("No format find in tmp_options.\n");
        ret = -EINVAL;
        goto err;
    }

    if ((tag = av_dict_get(tmp_options, "devname", NULL, 0))) {
        devname = tag->value;
    }

    if ((tag = av_dict_get(tmp_options, "pix_fmt", NULL, 0))) {
        endptr = NULL;
        ctx->pix_fmt = strtol(tag->value, &endptr, 0);
        if (*endptr != '\0') {
            MEDIA_ERR("Invalid pix_fmt: %s\n", tag->value);
            ret = -EINVAL;
            goto err;
        }
    } else
        ctx->pix_fmt = AV_PIX_FMT_NONE;

    ret = avformat_alloc_output_context2(&ctx->fmt_ctx, NULL, format, devname);
    if (ret < 0) {
        MEDIA_ERR("Failed to open %s: %s\n", devname, av_err2str(ret));
        goto err;
    }

    ctx->fmt_ctx->flags |= AVFMT_FLAG_NONBLOCK;
    ctx->fmt_ctx->opaque = ctx;
    ctx->fmt_ctx->control_message_cb = media_video_output_control_message;

    st = avformat_new_stream(ctx->fmt_ctx, NULL);
    if (!st) {
        ret = -ENOMEM;
        goto err;
    }

    if ((ret = avformat_init_output(ctx->fmt_ctx, &tmp_options)) < 0)
        goto err;

    av_dict_free(&tmp_options);

    *pctx = ctx;

    return 0;

err:
    if (ctx->fmt_ctx)
        avformat_free_context(ctx->fmt_ctx);

    av_dict_free(&tmp_options);
    av_free(ctx);
    return ret;
}

int media_video_output_close(MediaVOutputContext** pctx)
{
    MediaVOutputContext* ctx = *pctx;

    media_video_output_stop(ctx);

#if CONFIG_SWSCALE
    media_video_output_scale_uninit(ctx);
#endif

    if (ctx) {
        if (ctx->fmt_ctx) {
            avformat_free_context(ctx->fmt_ctx);
            ctx->fmt_ctx = NULL;
        }

        av_free(ctx);
        *pctx = NULL;
    }

    return 0;
}
