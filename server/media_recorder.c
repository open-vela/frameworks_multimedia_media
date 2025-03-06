/****************************************************************************
 * frameworks/multimedia/media/server/media_recorder.c
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

#include <errno.h>
#include <netinet/in.h>
#include <netpacket/rpmsg.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/sysinfo.h>
#include <sys/time.h>
#include <sys/un.h>

#include "libavcodec/avcodec.h"
#include "libavfilter/filters.h"
#include "libavfilter/framequeue.h"
#include "libavformat/avformat.h"
#include "libavformat/internal.h"
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/bprint.h"
#include "libavutil/opt.h"

#include "media_common.h"
#include "media_graph.h"
#include "media_plugin.h"
#include "media_server.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MEDIA_RECORDER_CMD_QUEUE_IDX  (1 << 0)
#define MEDIA_RECORDER_DATA_QUEUE_IDX (1 << 1)

#define MEDIA_RECORDER_MAX_CNT         10
#define MEDIA_RECORDER_CMD_QUEUE_MAX   16
#define MEDIA_RECORDER_DATA_QUEUE_SIZE 4

/****************************************************************************
 * Private Types
 ****************************************************************************/

enum media_recorder_state {
    MEDIA_RECORDER_STATE_IDLE = 0,
    MEDIA_RECORDER_STATE_PREPARED,
    MEDIA_RECORDER_STATE_STARTED,
    MEDIA_RECORDER_STATE_PAUSED,
    MEDIA_RECORDER_STATE_STOPPED,
    MEDIA_RECORDER_STATE_COMPLETED,
};

enum media_recorder_cmd {
    MEDIA_RECORDER_CMD_OPEN = 1,
    MEDIA_RECORDER_CMD_SET_EVENT,
    MEDIA_RECORDER_CMD_SET_OPTIONS,
    MEDIA_RECORDER_CMD_PREPARE,
    MEDIA_RECORDER_CMD_START,
    MEDIA_RECORDER_CMD_PAUSE,
    MEDIA_RECORDER_CMD_STOP,
    MEDIA_RECORDER_CMD_RESET,
    MEDIA_RECORDER_CMD_CLOSE,
};

typedef struct RecorderCmd {
    SIMPLEQ_ENTRY(RecorderCmd)
    entry;
    int cmd;
    char data[0];
} RecorderCmd;

SIMPLEQ_HEAD(RecorderCmdQueue, RecorderCmd);

typedef struct OutputStream {
    enum AVMediaType type;
    int index;
    int nb_queue_max;
    int64_t sync_pts;
    FFFrameQueue queue;
    AVRational time_base;
    AVCodecContext* enc_ctx;
} OutputStream;

typedef struct MediaRecorderContext {
    int tran_fd;
    int notify_fd;
    uint32_t offset;
    media_parcel parcel;

    int event;
    int cmd_max;
    int state;
    int audio_idx;
    int video_idx;
    char name[64];
    uint32_t nb_streams;                  /* total stream count */
    pthread_mutex_t mutex;
    OutputStream* streams;                /* output stream */
    AVDictionary* format_opt;             /* format options */
    AVFormatContext* format_ctx;          /* output format context */
    const AVOutputFormat* format;         /* output format */
    struct RecorderCmdQueue cmd_queue;

    MediaGraphStream* audio_input;
} MediaRecorderContext;

typedef struct MediaRecorderPriv {
    pthread_mutex_t mutex;
    MediaRecorderContext ctxs[MEDIA_RECORDER_MAX_CNT];
} MediaRecorderPriv;

/****************************************************************************
 * Function declaration
 ****************************************************************************/
static int media_recorder_queue_push(MediaRecorderContext* ctx, int idx, AVFrame* frame);
static int media_recorder_poll_available(MediaRecorderContext* ctx, struct pollfd* fd);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void media_recorder_notify_finalize(MediaRecorderContext* ctx)
{
    if (ctx->notify_fd > 0) {
        close(ctx->notify_fd);
        ctx->notify_fd = 0;
        ctx->offset    = 0;
    }
}

static int media_recorder_notify_event(MediaRecorderContext* ctx, int event,
                                       int result, const char* extra)
{
    media_parcel notify;
    int ret = -EINVAL;
    media_parcel_init(&notify);
    media_parcel_append_printf(&notify, "%i%i%s", event, result, extra);

    if (ctx->notify_fd > 0)
        ret = media_parcel_send(&notify, ctx->notify_fd, MEDIA_PARCEL_NOTIFY, MSG_DONTWAIT);

    media_parcel_deinit(&notify);
    return ret;
}

static void media_recorder_event_cb(MediaRecorderContext* ctx, int event,
                                    int result, const char* extra)
{
    if (ctx->event)
        media_recorder_notify_event(ctx, event, result, extra);
}

static int media_recorder_queue_cnt(MediaRecorderContext* ctx, int idx)
{
    int count = 0;

    pthread_mutex_lock(&ctx->mutex);
    count = ff_framequeue_queued_frames(&ctx->streams[idx].queue);
    pthread_mutex_unlock(&ctx->mutex);

    return count;
}

static bool media_recorder_dat_valid(MediaRecorderContext* ctx)
{
    int i;

    if (ctx->state != MEDIA_RECORDER_STATE_STARTED &&
        ctx->state != MEDIA_RECORDER_STATE_PAUSED)
        return false;

    for (i = 0; i < ctx->nb_streams; i++) {
        if (ff_framequeue_queued_frames(&ctx->streams[i].queue) > 0)
            return true;
    }

    return false;
}

static void media_recorder_clear_queue(MediaRecorderContext* ctx, int what)
{
    AVFrame* frame;
    RecorderCmd* msg;
    int i;

    pthread_mutex_lock(&ctx->mutex);
    if (what & MEDIA_RECORDER_CMD_QUEUE_IDX) {
        while ((msg = SIMPLEQ_FIRST(&ctx->cmd_queue)) != NULL) {
            SIMPLEQ_REMOVE_HEAD(&ctx->cmd_queue, entry);
            av_freep(&msg);
        }
    }

    if (what & MEDIA_RECORDER_DATA_QUEUE_IDX) {
        for (i = 0; i < ctx->nb_streams; i++) {
            while (ff_framequeue_queued_frames(&ctx->streams[i].queue)) {
                frame = ff_framequeue_take(&ctx->streams[i].queue);
                av_frame_free(&frame);
            }
        }
    }
    pthread_mutex_unlock(&ctx->mutex);
}

static AVFrame* media_recorder_queue_pop(MediaRecorderContext* ctx, int idx)
{
    AVFrame* frame = NULL;

    pthread_mutex_lock(&ctx->mutex);
    if (ff_framequeue_queued_frames(&ctx->streams[idx].queue)) {
        frame = ff_framequeue_take(&ctx->streams[idx].queue);
    }

    pthread_mutex_unlock(&ctx->mutex);
    return frame;
}

static int media_recorder_queue_push(MediaRecorderContext* ctx, int idx, AVFrame* frame)
{
    int ret;
    pthread_mutex_lock(&ctx->mutex);
    if (ff_framequeue_queued_frames(&ctx->streams[idx].queue) > ctx->streams[idx].nb_queue_max) {
        MEDIA_WARN("data queue is more than max count(%d).\n", ctx->streams[idx].nb_queue_max);
        AVFrame* last_frame = ff_framequeue_take(&ctx->streams[idx].queue);
        av_frame_free(&last_frame);
    }

    ret = ff_framequeue_add(&ctx->streams[idx].queue, frame);
    pthread_mutex_unlock(&ctx->mutex);

    return ret;
}

static int media_recorder_send_empty_frame(MediaRecorderContext* ctx)
{
    AVFrame* frame;
    int i, ret;

    for (i = 0; i < ctx->nb_streams; i++) {
        frame = av_frame_alloc();
        if (!frame)
            return AVERROR(ENOMEM);

        ret = media_recorder_queue_push(ctx, i, frame);
        if (ret < 0) {
            av_frame_free(&frame);
            return ret;
        }
    }

    return 0;
}

static int media_recorder_encode_frame(MediaRecorderContext* ctx, int idx, AVFrame* frame)
{
    AVPacket* pkt;
    int ret = 0;

    pkt = av_packet_alloc();
    if (!pkt)
        return AVERROR(ENOMEM);

    ret = avcodec_send_frame(ctx->streams[idx].enc_ctx, frame);
    if (ret < 0)
        goto out;

    while (1) {
        ret = avcodec_receive_packet(ctx->streams[idx].enc_ctx, pkt);
        if (ret < 0)
            break;

        pkt->stream_index = idx;

        /* convert pts to time base of AVStream */
        av_packet_rescale_ts(pkt,
            ctx->streams[idx].enc_ctx->time_base,
            ctx->format_ctx->streams[pkt->stream_index]->time_base);

        ret = av_write_frame(ctx->format_ctx, pkt);
        if (ret < 0)
            break;
    }

out:
    if (ret == AVERROR(EAGAIN))
        ret = 0;

    av_packet_free(&pkt);
    return ret;
}

static int media_recorder_open_encoder(MediaRecorderContext* ctx, int idx)
{
    int sample_rate, sample_fmt, channels;
    int width, height, bitrate = -1, vbr = -1, level = -1;
    AVDictionary* dict = NULL;
    AVDictionaryEntry* tag;
    AVStream* stream;
    const AVCodec* enc;
    int ret;

    // step1: init option params for encoder
    if ((tag = av_dict_get(ctx->format_opt, "sample_rate", NULL, 0)))
        sample_rate = strtol(tag->value, NULL, 0);

    if ((tag = av_dict_get(ctx->format_opt, "sample_fmt", NULL, 0)))
        sample_fmt = strtol(tag->value, NULL, 0);

    if ((tag = av_dict_get(ctx->format_opt, "channels", NULL, 0)))
        channels = strtol(tag->value, NULL, 0);

    if ((tag = av_dict_get(ctx->format_opt, "width", NULL, 0)))
        width = strtol(tag->value, NULL, 0);

    if ((tag = av_dict_get(ctx->format_opt, "height", NULL, 0)))
        height = strtol(tag->value, NULL, 0);

    if ((tag = av_dict_get(ctx->format_opt, "bitrate", NULL, 0)))
        bitrate = strtol(tag->value, NULL, 0);

    if ((tag = av_dict_get(ctx->format_opt, "vbr", NULL, 0)))
        vbr = strtol(tag->value, NULL, 0);

    if ((tag = av_dict_get(ctx->format_opt, "level", NULL, 0)))
        level = strtol(tag->value, NULL, 0);

    // step2: create encoder
    if (ctx->streams[idx].type == AVMEDIA_TYPE_AUDIO) {
        tag = av_dict_get(ctx->format_opt, "audio_codec", NULL, 0);
        if (tag) {
            enc = avcodec_find_encoder(atoi(tag->value));
        } else {
            enc = avcodec_find_encoder(ctx->format_ctx->oformat->audio_codec);
        }
    } else {
        tag = av_dict_get(ctx->format_opt, "video_codec", NULL, 0);
        if (tag) {
            enc = avcodec_find_encoder(atoi(tag->value));
        } else {
            enc = avcodec_find_encoder(ctx->format_ctx->oformat->video_codec);
        }
    }

    if (!enc) {
        av_log(NULL, AV_LOG_INFO, "not find enc\n");
        return AVERROR(EINVAL);
    }

    ctx->streams[idx].enc_ctx = avcodec_alloc_context3(enc);
    if (!ctx->streams[idx].enc_ctx) {
        return AVERROR(ENOMEM);
    }

    if (ctx->streams[idx].type == AVMEDIA_TYPE_AUDIO) {
        ctx->streams[idx].enc_ctx->sample_fmt  = sample_fmt;
        ctx->streams[idx].enc_ctx->sample_rate = sample_rate;
        av_channel_layout_default(&ctx->streams[idx].enc_ctx->ch_layout, channels);
        ctx->streams[idx].enc_ctx->time_base = (AVRational) { 1, sample_rate };
    } else {
        ctx->streams[idx].enc_ctx->width  = width;
        ctx->streams[idx].enc_ctx->height = height;
    }

    if (ctx->format_ctx->oformat->flags & AVFMT_GLOBALHEADER)
        ctx->streams[idx].enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    ctx->streams[idx].enc_ctx->strict_std_compliance = FF_COMPLIANCE_UNOFFICIAL;

    if (bitrate != -1)
        av_opt_set_int(ctx->streams[idx].enc_ctx, "b", bitrate, 0);

    if (vbr != -1)
        av_opt_set_int(ctx->streams[idx].enc_ctx, "vbr", vbr, AV_OPT_SEARCH_CHILDREN);

    if (level != -1)
        av_opt_set_int(ctx->streams[idx].enc_ctx, "compression_level", level, 0);

    if (ctx->format_opt)
        av_dict_copy(&dict, ctx->format_opt, 0);

    ret = avcodec_open2(ctx->streams[idx].enc_ctx, enc, &dict);
    av_dict_free(&dict);
    if (ret < 0)
        goto out;

    // step3: create stream
    stream = avformat_new_stream(ctx->format_ctx, NULL);
    if (!stream) {
        ret = AVERROR(ENOMEM);
        goto out;
    }

    ret = avcodec_parameters_from_context(stream->codecpar, ctx->streams[idx].enc_ctx);
    if (ret < 0)
        goto out;

    stream->time_base = ctx->streams[idx].enc_ctx->time_base;

    return 0;

out:
    avcodec_free_context(&ctx->streams[idx].enc_ctx);
    return ret;
}

static int media_recorder_init_stream(MediaRecorderContext* ctx)
{
    int stream_cnt;
    int types[2];
    int i;

    // init output stream by stream type.
    // a: only audio, v: only video, others: audio and video
    if (ctx->name[0] == 'a') {
        stream_cnt     = 1;
        types[0]       = AVMEDIA_TYPE_AUDIO;
        ctx->audio_idx = 0;
    } else if (ctx->name[0] == 'v') {
        stream_cnt     = 1;
        types[0]       = AVMEDIA_TYPE_VIDEO;
        ctx->video_idx = 0;
    } else {
        stream_cnt     = 2;
        types[0]       = AVMEDIA_TYPE_AUDIO;
        types[1]       = AVMEDIA_TYPE_VIDEO;
        ctx->audio_idx = 0;
        ctx->video_idx = 1;
    }

    ctx->streams = av_calloc(stream_cnt, sizeof(OutputStream));
    if (!ctx->streams)
        return AVERROR(ENOMEM);

    for (i = 0; i < stream_cnt; i++) {
        ctx->streams[i].index = i;
        ctx->streams[i].type  = types[i];
        ff_framequeue_init(&ctx->streams[i].queue, NULL);
        ctx->streams[i].nb_queue_max = MEDIA_RECORDER_DATA_QUEUE_SIZE;
    }

    ctx->nb_streams = stream_cnt;

    return 0;
}

static int media_recorder_on_event_cb(void *udata, int evt, int64_t args)
{
    MediaRecorderContext* ctx = (MediaRecorderContext*)udata;
    AVFrame* in_frame = (AVFrame*)(uintptr_t)args;
    AVFrame* frame;

    frame = av_frame_clone(in_frame);
    if (!frame)
        return AVERROR(ENOMEM);

    media_recorder_queue_push(ctx, ctx->audio_idx, frame);

    return 0;
}

static void media_recorder_close_muxer(MediaRecorderContext* ctx)
{
    int i;

    for (i = 0; i < ctx->format_ctx->nb_streams; i++) {
        avcodec_free_context(&ctx->streams[i].enc_ctx);
        ctx->streams[i].sync_pts = 0;
    }

    if (ctx->format_ctx) {
        if (ctx->format_ctx->pb)
            avio_close(ctx->format_ctx->pb);

        avformat_free_context(ctx->format_ctx);
        ctx->format_ctx = NULL;
    }

    if (ctx->format_opt)
        av_dict_free(&ctx->format_opt);
}

static void media_recorder_clean(MediaRecorderContext* ctx)
{
    if (ctx->state != MEDIA_RECORDER_STATE_STOPPED) {
        media_recorder_close_muxer(ctx);
        ctx->state = MEDIA_RECORDER_STATE_STOPPED;
        media_recorder_notify_event(ctx, MEDIA_EVENT_STOPPED, 0, NULL);
    }
}

static int media_recorder_interrupt(void* opaque)
{
    MediaRecorderContext* ctx = opaque;
    RecorderCmd* msg;
    int interrupt = 0;
    struct pollfd fds[1];
    struct pollfd* fd = &fds[0];
    fds[0].fd         = ctx->tran_fd;
    fds[0].events     = POLLIN;
    fds[0].revents    = 0;

    pthread_mutex_lock(&ctx->mutex);

    media_recorder_poll_available(ctx, fd);

    SIMPLEQ_FOREACH(msg, &ctx->cmd_queue, entry)
    {
        if (msg->cmd >= MEDIA_RECORDER_CMD_STOP) {
            interrupt = 1;
            break;
        }
    }

    pthread_mutex_unlock(&ctx->mutex);
    return interrupt;
}

static int media_recorder_open_muxer(MediaRecorderContext* ctx, const char* filename)
{
    AVDictionary* dict = NULL;
    AVIOInterruptCB cb;
    int ret;

    ret = avformat_alloc_output_context2(&ctx->format_ctx, ctx->format, NULL, filename);
    if (ret < 0)
        return ret;

    ctx->format_ctx->flags |= AVFMT_FLAG_NONBLOCK;

    pthread_mutex_lock(&ctx->mutex);
    if (ctx->format_opt) {
        av_dict_copy(&dict, ctx->format_opt, 0);
        ret = av_opt_set_dict2(ctx->format_ctx, &dict, AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            pthread_mutex_unlock(&ctx->mutex);
            av_dict_free(&dict);
            goto out;
        }
    }
    pthread_mutex_unlock(&ctx->mutex);

    cb.callback = media_recorder_interrupt;
    cb.opaque   = ctx;

    ret = avio_open2(&ctx->format_ctx->pb, filename, AVIO_FLAG_WRITE, &cb, &dict);
    av_dict_free(&dict);
    if (ret < 0)
        goto out;

    ret = media_recorder_init_stream(ctx);
    if (ret < 0)
        goto out;

    return 0;

out:
    avformat_free_context(ctx->format_ctx);
    return ret;
}

static int media_recorder_proc_dat(MediaRecorderContext* ctx)
{
    AVFrame* frame;
    int ret;
    int i;
    for (i = 0; i < ctx->nb_streams; i++) {
        frame = media_recorder_queue_pop(ctx, i);
        if (!frame)
            continue;

        frame->pts -= ctx->streams[i].sync_pts;
        frame->pict_type = AV_PICTURE_TYPE_NONE;

        /* user request stop, flush code which data = 0 */
        if (!frame->data[0]){
            MEDIA_INFO("reveice empty frame\n");
            av_frame_free(&frame);
        } else if (ctx->state == MEDIA_RECORDER_STATE_PAUSED) {
            if (ctx->streams[i].type == AVMEDIA_TYPE_AUDIO)
                ctx->streams[i].sync_pts += frame->nb_samples;
            else
                ctx->streams[i].sync_pts ++;
            av_frame_free(&frame);
            continue;
        }

        ret = media_recorder_encode_frame(ctx, i, frame);
        av_frame_free(&frame);
        if (ret < 0) {
            av_write_trailer(ctx->format_ctx);
            MEDIA_ERR("media_recorder_encode_frame failed: %d\n", ret);
            goto out;
        }
    }

    return 0;

out:

    media_recorder_clear_queue(ctx, MEDIA_RECORDER_DATA_QUEUE_IDX);
    ctx->state = MEDIA_RECORDER_STATE_COMPLETED;
    media_recorder_notify_event(ctx, MEDIA_EVENT_COMPLETED,
                                ret == AVERROR_EOF ? 0 : ret, NULL);
    media_recorder_clean(ctx);
    return ret;
}

static void media_recorder_ctx_init(MediaRecorderContext* ctx)
{
    ctx->state = MEDIA_RECORDER_STATE_STOPPED;
    ctx->cmd_max = MEDIA_RECORDER_CMD_QUEUE_MAX;
    ctx->audio_idx = -1;
    ctx->video_idx = -1;
    SIMPLEQ_INIT(&ctx->cmd_queue);
    media_parcel_init(&ctx->parcel);
    pthread_mutex_init(&ctx->mutex, NULL);
}

static void media_recorder_ctx_release(MediaRecorderContext* ctx)
{
    ctx->state = MEDIA_RECORDER_STATE_IDLE;
    ctx->audio_idx = -1;
    ctx->video_idx = -1;
    ctx->nb_streams = 0;
    ctx->event = 0;
    media_recorder_notify_finalize(ctx);
    media_parcel_deinit(&ctx->parcel);
    pthread_mutex_destroy(&ctx->mutex);
}

static int media_recorder_pause(MediaRecorderContext* ctx)
{
    int ret = AVERROR(EPERM);

    if (ctx->state == MEDIA_RECORDER_STATE_STARTED) {
        ctx->state = MEDIA_RECORDER_STATE_PAUSED;
        ret = 0;
    }

    pthread_mutex_lock(&ctx->mutex);
    if (ctx->audio_input)
        media_graph_stream_close(&ctx->audio_input);
    pthread_mutex_unlock(&ctx->mutex);

    media_recorder_event_cb(ctx, MEDIA_EVENT_PAUSED, ret, NULL);
    return 0;
}

static int media_recorder_stop(MediaRecorderContext* ctx)
{
    int ret;
    if (ctx->state == MEDIA_RECORDER_STATE_STOPPED)
        return 0;

    pthread_mutex_lock(&ctx->mutex);
    if (ctx->audio_input)
        media_graph_stream_close(&ctx->audio_input);
    pthread_mutex_unlock(&ctx->mutex);

    if (ctx->state == MEDIA_RECORDER_STATE_PREPARED || ctx->state == MEDIA_RECORDER_STATE_COMPLETED)
        goto out;

    // send empty frame to flush encoder
    ret = media_recorder_send_empty_frame(ctx);
    while (!ret) {
        ret = media_recorder_proc_dat(ctx);
    }

out:
    media_recorder_clean(ctx);
    return 0;
}

static int media_recorder_close(MediaRecorderContext* ctx)
{
    int i;

    media_recorder_stop(ctx);

    for (i = 0; i < ctx->nb_streams; i++) {
        ff_framequeue_free(&ctx->streams[i].queue);
    }

    av_freep(&ctx->streams);
    return 0;
}

static int media_recorder_start(MediaRecorderContext* ctx)
{
    AVDictionaryEntry* tag;
    int ret = AVERROR(EPERM);
    int sample_rate;
    int sample_fmt;
    int channels;

    if (ctx->state != MEDIA_RECORDER_STATE_PREPARED && ctx->state != MEDIA_RECORDER_STATE_PAUSED)
        goto out;

    if (!ctx->audio_input) {

        if ((tag = av_dict_get(ctx->format_opt, "sample_rate", NULL, 0)))
            sample_rate = strtol(tag->value, NULL, 0);
        else {
            MEDIA_ERR("sample_rate not found in format options.\n");
            return AVERROR(EINVAL);
        }

        if ((tag = av_dict_get(ctx->format_opt, "sample_fmt", NULL, 0)))
            sample_fmt = strtol(tag->value, NULL, 0);
        else {
            MEDIA_ERR("sample_fmt not found in format options.\n");
            return AVERROR(EINVAL);
        }

        if ((tag = av_dict_get(ctx->format_opt, "channels", NULL, 0)))
            channels = strtol(tag->value, NULL, 0);
        else {
            MEDIA_ERR("channels not found in format options.\n");
            return AVERROR(EINVAL);
        }

        ret = media_graph_stream_open(&ctx->audio_input, ctx->name,
                                      sample_fmt, sample_rate, channels,
                                      media_recorder_on_event_cb, ctx);
        if (ret < 0)
            MEDIA_ERR("media_graph_stream_open failed, ret %d.\n", ret);
        else {
            MEDIA_INFO("media_graph_stream_open success.\n");
            ctx->state = MEDIA_RECORDER_STATE_STARTED;
        }
        return ret;
    }

out:
    media_recorder_event_cb(ctx, MEDIA_EVENT_STARTED, ret, NULL);
    return ret;
}

static int media_recorder_prepare(MediaRecorderContext* ctx, const char* filename)
{
    int ret = AVERROR(EPERM);
    AVDictionaryEntry* tag;
    char* format = NULL;
    int i;

    if (ctx->state != MEDIA_RECORDER_STATE_STOPPED)
        goto out;

    if ((tag = av_dict_get(ctx->format_opt, "format", NULL, 0)))
        format = tag->value;

    ctx->format = av_guess_format(format, filename, NULL);
    if (!ctx->format) {
        MEDIA_ERR("unknown format.\n");
        ret = AVERROR(EINVAL);
        goto out;
    }

    ret = media_recorder_open_muxer(ctx, filename);
    if (ret < 0)
        goto out;

    for (i = 0; i < ctx->nb_streams; i++) {
        ret = media_recorder_open_encoder(ctx, i);
        if (ret < 0)
            goto out;
    }

    ret = avformat_write_header(ctx->format_ctx, NULL);
    if (ret < 0)
        goto out;

    ctx->state = MEDIA_RECORDER_STATE_PREPARED;

out:
    media_recorder_event_cb(ctx, MEDIA_EVENT_PREPARED, ret, NULL);
    return ret;
}

static int media_recorder_send_cmd(MediaRecorderContext* ctx, const int cmd, const void* data, size_t size)
{
    RecorderCmd *msg, *tmp;
    int cnt = 0;

    msg = av_malloc(sizeof(RecorderCmd) + size);
    if (!msg)
        return AVERROR(ENOMEM);

    msg->cmd = cmd;

    if (data && size)
        memcpy(msg->data, data, size);

    pthread_mutex_lock(&ctx->mutex);

    SIMPLEQ_FOREACH(tmp, &ctx->cmd_queue, entry)
    cnt++;
    if (cnt >= ctx->cmd_max && msg->cmd < MEDIA_RECORDER_CMD_STOP) {
        pthread_mutex_unlock(&ctx->mutex);
        av_freep(&msg);
        return AVERROR(ENOMEM);
    }

    SIMPLEQ_INSERT_TAIL(&ctx->cmd_queue, msg, entry);
    pthread_mutex_unlock(&ctx->mutex);

    return 0;
}

static int media_recorder_proc_cmd(MediaRecorderContext* ctx, RecorderCmd* msg)
{
    int exit = false;

    switch (msg->cmd) {
    case MEDIA_RECORDER_CMD_SET_EVENT:
        break;

    case MEDIA_RECORDER_CMD_SET_OPTIONS:
        av_dict_parse_string(&ctx->format_opt, msg->data, "=", ":", 0);
        break;

    case MEDIA_RECORDER_CMD_PREPARE:
        media_recorder_prepare(ctx, msg->data);
        break;

    case MEDIA_RECORDER_CMD_START:
        media_recorder_start(ctx);
        break;

    case MEDIA_RECORDER_CMD_PAUSE:
        media_recorder_pause(ctx);
        break;

    case MEDIA_RECORDER_CMD_CLOSE:
        media_recorder_close(ctx);
        exit = true;
        break;
    case MEDIA_RECORDER_CMD_STOP:
    case MEDIA_RECORDER_CMD_RESET:
        media_recorder_stop(ctx);
        break;
    default:
        break;
    }

    av_freep(&msg);
    return exit;
}

int media_recorder_process_cmd(MediaRecorderContext* ctx, const char* target,
                               const char* cmd, const char* arg,char* res, int res_len)
{
    char url[PATH_MAX];
    int ret = 0;

    if (!ctx)
        return -EINVAL;

    MEDIA_INFO("cmd: %s, arg %s, target %s.\n", cmd, arg ? arg : "NULL", target ? target : "NULL");

    if (!strcmp(cmd, "set_event")) {
        ctx->event = true;
        return 0;
    }

    if (!strcmp(cmd, "prepare")) {
        if (target) {
            /* Buffer mode, use `target` as cpuname, `arg` as sockname. */
            if (!strcmp(target, CONFIG_RPMSG_LOCAL_CPUNAME))
                snprintf(url, sizeof(url), "unix:%s?listen=0", arg);
            else
                snprintf(url, sizeof(url), "rpmsg:%s:%s?listen=0", arg, target);
            arg = url;
            MEDIA_INFO("url: %s.\n", url);
        }
        if (!arg)
            return AVERROR(EINVAL);
        ret = media_recorder_send_cmd(ctx, MEDIA_RECORDER_CMD_PREPARE, arg, strlen(arg) + 1);
    } else if (!strcmp(cmd, "close") && arg) {
        media_recorder_clear_queue(ctx, MEDIA_RECORDER_CMD_QUEUE_IDX);
        ret = media_recorder_send_cmd(ctx, MEDIA_RECORDER_CMD_CLOSE, arg, strlen(arg) + 1);
    } else if (!strcmp(cmd, "start")) {
        ret = media_recorder_send_cmd(ctx, MEDIA_RECORDER_CMD_START, NULL, 0);
    } else if (!strcmp(cmd, "stop")) {
        ret = media_recorder_send_cmd(ctx, MEDIA_RECORDER_CMD_STOP, NULL, 0);
    } else if (!strcmp(cmd, "reset")) {
        media_recorder_clear_queue(ctx, MEDIA_RECORDER_CMD_QUEUE_IDX);
        ret = media_recorder_send_cmd(ctx, MEDIA_RECORDER_CMD_RESET, NULL, 0);
    } else if (!strcmp(cmd, "pause")) {
        ret = media_recorder_send_cmd(ctx, MEDIA_RECORDER_CMD_PAUSE, NULL, 0);
    } else if (!strcmp(cmd, "set_options") && arg) {
        ret = media_recorder_send_cmd(ctx, MEDIA_RECORDER_CMD_SET_OPTIONS, arg, strlen(arg) + 1);
    } else {
        MEDIA_ERR("unknown cmd: %s.\n", cmd);
        return AVERROR(EINVAL);
    }

    return ret;
}

int media_recorder_onreceive(MediaRecorderContext* ctx, media_parcel* in, media_parcel* out)
{
    const char *target = NULL, *cmd = NULL, *arg = NULL;
    int32_t len = 0, flags = 0, id = 0, ret;
    char* response = NULL;

    media_parcel_read_int32(in, &id);

    switch (id) {
    case MEDIA_ID_RECORDER:
        media_parcel_read_scanf(in, "%s%s%s%i", &target, &cmd, &arg, &len);
        if (len > 0)
            response = zalloc(len);

        ret = media_recorder_process_cmd(ctx, target, cmd, arg, response, len);
        break;
    default:
        UNUSED(target);
        UNUSED(cmd);
        UNUSED(arg);
        UNUSED(len);
        UNUSED(flags);
        ret = -ENOSYS;
        MEDIA_ERR("unsupported id %d\n", id);
        break;
    }

    if (out)
        media_parcel_append_printf(out, "%i%s", ret, response);

    if (response)
        free(response);

    return ret;
}

static int media_recorder_create_notify(MediaRecorderContext* ctx, media_parcel* parcel)
{
    struct sockaddr_un local_addr;
    struct sockaddr_rpmsg rpmsg_addr;
    struct sockaddr* addr;
    const char* key;
    const char* cpu;
    int fd;
    int family;
    int len;
    int ret;

    key = media_parcel_read_string(parcel);
    cpu = media_parcel_read_string(parcel);

    if (key == NULL)
        return -EINVAL;

    if (strcmp(cpu, CONFIG_RPMSG_LOCAL_CPUNAME)) {
        family               = AF_RPMSG;
        rpmsg_addr.rp_family = AF_RPMSG;
        strlcpy(rpmsg_addr.rp_name, key, RPMSG_SOCKET_NAME_SIZE);
        strlcpy(rpmsg_addr.rp_cpu, cpu, RPMSG_SOCKET_CPU_SIZE);
        addr = (struct sockaddr*)&rpmsg_addr;
        len  = sizeof(struct sockaddr_rpmsg);
    } else {
        family                = PF_LOCAL;
        local_addr.sun_family = AF_LOCAL;
        strlcpy(local_addr.sun_path, key, UNIX_PATH_MAX);
        addr = (struct sockaddr*)&local_addr;
        len  = sizeof(struct sockaddr_un);
    }

    fd = socket(family, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -errno;

    ret = connect(fd, addr, len);
    if (ret < 0) {
        close(fd);
        return -errno;
    }

    return fd;
}

static void media_recorder_conn_close(MediaRecorderContext* ctx)
{
    close(ctx->tran_fd);
    ctx->tran_fd = -EPERM;
    ctx->offset  = 0;
    media_parcel_deinit(&ctx->parcel);
}

static int media_recorder_poll_available(MediaRecorderContext* ctx, struct pollfd* fd)
{
    int ret = -EINVAL;
    uint32_t code;
    media_parcel ack;

    if (fd->revents & POLLERR)
        goto out;

    while (1) {
        ret = media_parcel_recv(&ctx->parcel, ctx->tran_fd, &ctx->offset, MSG_DONTWAIT);
        if (ret < 0)
            break;

        code = media_parcel_get_code(&ctx->parcel);
        switch (code) {
        case MEDIA_PARCEL_SEND:
            media_recorder_onreceive(ctx, &ctx->parcel, NULL);
            break;

        case MEDIA_PARCEL_SEND_ACK:
            media_parcel_init(&ack);
            media_recorder_onreceive(ctx, &ctx->parcel, &ack);
            ret = media_parcel_send(&ack, ctx->tran_fd, MEDIA_PARCEL_REPLY, 0);
            media_parcel_deinit(&ack);
            break;

        case MEDIA_PARCEL_CREATE_NOTIFY:
            ret = media_recorder_create_notify(ctx, &ctx->parcel);
            if (ret > 0)
                ctx->notify_fd = ret;
            else
                MEDIA_ERR("create notify failed %d\n", ret);
            break;
        default:
            break;
        }

        media_parcel_reinit(&ctx->parcel);
        ctx->offset = 0;
    }

    if (((fd->revents & POLLIN) && ret == -EPIPE) || (fd->revents & POLLHUP))
        goto out;

    return ret;

out:
    MEDIA_DEBUG("fd:%d revent:%d\n", fd->fd, (int)fd->revents);
    media_recorder_conn_close(ctx);
    return 0;
}

static MediaRecorderContext* media_recorder_get_available_session(MediaRecorderPriv* priv)
{
    MediaRecorderContext* ctx = NULL;
    int i;

    for (i = 0; i < MEDIA_RECORDER_MAX_CNT; i++) {
        ctx = &priv->ctxs[i];
        if (ctx->state == MEDIA_RECORDER_STATE_IDLE)
            break;
    }

    return ctx;
}

static void media_recorder_dump(MediaRecorderPriv* priv)
{
    MediaRecorderContext* ctx;
    AVBPrint buf;
    int i;

    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_UNLIMITED);
    av_bprintf(&buf, "\n--------------recorder dump start-------------\n");
    for (i = 0; i < MEDIA_RECORDER_MAX_CNT; i++) {
        ctx = &priv->ctxs[i];
        if (ctx->state == MEDIA_RECORDER_STATE_IDLE)
            continue;
        av_bprintf(&buf, "recorder[%d, %s] state:%d", i, ctx->name, ctx->state);
        if (ctx->audio_idx >= 0)
            av_bprintf(&buf, ", a: %d %s %" PRId64 " %d %d %d",
                       ctx->audio_idx,
                       avcodec_get_name(ctx->streams[ctx->audio_idx].enc_ctx->codec_id),
                       ctx->streams[ctx->audio_idx].enc_ctx->bit_rate,
                       ctx->streams[ctx->audio_idx].enc_ctx->sample_rate,
                       ctx->streams[ctx->audio_idx].enc_ctx->ch_layout.nb_channels,
                       media_recorder_queue_cnt(ctx, ctx->audio_idx));
        if (ctx->video_idx >= 0)
            av_bprintf(&buf, ", v: %d %s %d %d %d",
                       ctx->video_idx,
                       avcodec_get_name(ctx->streams[ctx->video_idx].enc_ctx->codec_id),
                       ctx->streams[ctx->audio_idx].enc_ctx->width,
                       ctx->streams[ctx->audio_idx].enc_ctx->height,
                       media_recorder_queue_cnt(ctx, ctx->video_idx));
    }
    av_bprintf(&buf, "\n--------------recorder dump end---------------\n");
    MEDIA_INFO("%s\n", buf.str);
    av_bprint_finalize(&buf, NULL);
}

static void media_recorder_poll(MediaRecorderContext* ctx)
{
    struct pollfd fds[1];
    struct pollfd* fd = &fds[0];
    fds[0].fd         = ctx->tran_fd;
    fds[0].events     = POLLIN;
    fds[0].revents    = 0;
    int ret;

    ret = poll(fds, 1, 2);
    if (ret == -1) {
        MEDIA_ERR("poll failed err=%d\n", -errno);
    } else if (ret == 0)
        MEDIA_DEBUG("poll timeout\n");

    ret = media_recorder_poll_available(ctx, fd);
    if (ret < 0 && ret != -EAGAIN && ret != -EPIPE)
        MEDIA_ERR("poll_available failed %d\n", ret);
}

static void* media_recorder_thread(void* arg)
{
    MediaRecorderContext* ctx = (MediaRecorderContext*)arg;
    int exit = false;
    RecorderCmd* msg;

    while (1) {
        media_recorder_poll(ctx);
        if ((msg = SIMPLEQ_FIRST(&ctx->cmd_queue)) != NULL) {
            SIMPLEQ_REMOVE_HEAD(&ctx->cmd_queue, entry);
            pthread_mutex_unlock(&ctx->mutex);
            if (media_recorder_proc_cmd(ctx, msg))
                exit = true;
        } else if (media_recorder_dat_valid(ctx)) {
            pthread_mutex_unlock(&ctx->mutex);
            if (media_recorder_proc_dat(ctx) == AVERROR_EOF)
                exit = true;
        } else if (exit) {
            ctx->state = MEDIA_RECORDER_STATE_IDLE;
            pthread_mutex_unlock(&ctx->mutex);
            break;
        } else {
            pthread_mutex_unlock(&ctx->mutex);
        }
    }

    media_recorder_ctx_release(ctx);

    MEDIA_INFO("exit recorder thread.\n");
    return NULL;
}

static int media_recorder_open(MediaRecorderContext* ctx, const char* name)
{
    struct sched_param param;
    pthread_attr_t attr;
    pthread_t thread;
    int ret;

    if (ctx->state != MEDIA_RECORDER_STATE_IDLE || name == NULL)
        return AVERROR(EINVAL);

    strlcpy(ctx->name, name, sizeof(ctx->name));

    media_recorder_ctx_init(ctx);

    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, CONFIG_MEDIA_RECORDER_STACKSIZE);
    param.sched_priority = CONFIG_MEDIA_RECORDER_PRIORITY;
    pthread_attr_setschedparam(&attr, &param);
    ret = pthread_create(&thread, &attr, media_recorder_thread, ctx);
    if (ret != 0) {
        ctx->state = MEDIA_RECORDER_STATE_IDLE;
        return AVERROR(ret);
    }

    pthread_setname_np(thread, ctx->name);
    pthread_detach(thread);

    return 0;
}

static int media_recorder_init(MediadPlugin* handle)
{
    MediaRecorderPriv* priv = handle->priv;

    pthread_mutex_init(&priv->mutex, NULL);

    return 0;
}

static int media_recorder_uninit(MediadPlugin* handle)
{
    MediaRecorderPriv* priv = handle->priv;

    pthread_mutex_destroy(&priv->mutex);

    return 0;
}

static int media_recorder_handler(MediadPlugin* handle, struct media_server_conn* conn,
                                  const char* target, const char* cmd, const char* arg,
                                  int flags, char* res, int res_len)
{
    MediaRecorderPriv* priv = handle->priv;
    char stream_name[64] = { 0 };
    int ret;

    MEDIA_INFO("cmd: %s, arg %s, target %s.\n",
               cmd, arg ? arg : "NULL", target ? target : "NULL");

    pthread_mutex_lock(&priv->mutex);

    if (!strcmp(cmd, "open")) {
        ret = media_stub_get_stream_name(arg, stream_name, sizeof(stream_name));
        if (ret < 0) {
            MEDIA_ERR("get stream name failed %d\n", ret);
            goto out;
        }

        MediaRecorderContext* ctx = media_recorder_get_available_session(priv);
        if (!ctx) {
            MEDIA_ERR("recorder open failed...\n");
            ret = -ENOMEM;
            goto out;
        }

        ctx->tran_fd = media_server_get_tran_fd(conn);
        if (ctx->tran_fd < 0) {
            MEDIA_ERR("recorder get tran fd failed...\n");
            ret = -EINVAL;
            goto out;
        }

        media_server_clean_conn(conn);

        ret = media_recorder_open(ctx, stream_name);
        if (ret < 0)
            goto out;

        MEDIA_INFO("open recorder success...\n");
    } else if (!strcmp(cmd, "dump")) {
        media_recorder_dump(priv);
    }

out:
    pthread_mutex_unlock(&priv->mutex);

    return ret;
}

MediadPlugin media_recorder_plugin = {
    .name            = "media_recorder",
    .priv_size       = sizeof(struct MediaRecorderPriv),
    .priv            = NULL,
    .init            = media_recorder_init,
    .get             = NULL,
    .available       = NULL,
    .run_once        = NULL,
    .uninit          = media_recorder_uninit,
    .process_command = media_recorder_handler,
};
