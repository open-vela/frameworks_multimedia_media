/****************************************************************************
 * frameworks/multimedia/media/server/media_player.c
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
#include <sys/un.h>
#include <sys/sysinfo.h>

#include "libavcodec/avcodec.h"
#include "libavfilter/filters.h"
#include "libavfilter/framequeue.h"
#include "libavformat/avformat.h"
#include "libavformat/internal.h"
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/bprint.h"
#include "libavutil/time.h"
#include "libavutil/opt.h"

#include "media_common.h"
#include "media_plugin.h"
#include "media_server.h"
#include "media_graph.h"
#include "media_video_output.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MEDIA_PLAYER_CMD_QUEUE_IDX  (1 << 0)
#define MEDIA_PLAYER_DATA_QUEUE_IDX (1 << 1)

#define MEDIA_PLAYER_MAX_CNT 10
#define MEDIA_PLAYER_CMD_QUEUE_MAX 16
#define MEDIA_PLAYER_DATA_QUEUE_SIZE 4

#define MEDIA_PLAYER_SLIENCE_FRAME_DURATION 20

/****************************************************************************
 * Private Types
 ****************************************************************************/
enum MediaPlayerSyncMode {
    MEDIA_PLAYER_SYNC_MODE_AUDIO,
    MEDIA_PLAYER_SYNC_MODE_SYSTEM,
    MEDIA_PLAYER_SYNC_MODE_BYPASS,
};

enum MediaPlayerState {
    MEDIA_PLAYER_STATE_IDLE = 0,
    MEDIA_PLAYER_STATE_PREPARED,
    MEDIA_PLAYER_STATE_STARTED,
    MEDIA_PLAYER_STATE_PAUSED,
    MEDIA_PLAYER_STATE_STOPPED,
    MEDIA_PLAYER_STATE_COMPLETED,
};

enum MediaPlayerCmd {
    MEDIA_PLAYER_CMD_OPEN = 1,
    MEDIA_PLAYER_CMD_SET_EVENT,
    MEDIA_PLAYER_CMD_SET_OPTIONS,
    MEDIA_PLAYER_CMD_SET_LOOP,
    MEDIA_PLAYER_CMD_PREPARE,
    MEDIA_PLAYER_CMD_START,
    MEDIA_PLAYER_CMD_PAUSE,
    MEDIA_PLAYER_CMD_SEEK,
    MEDIA_PLAYER_CMD_STOP,
    MEDIA_PLAYER_CMD_RESET,
    MEDIA_PLAYER_CMD_CLOSE,
};

typedef struct PlayerCmd {
    SIMPLEQ_ENTRY(PlayerCmd) entry;
    int cmd;
    char data[0];
} PlayerCmd;

SIMPLEQ_HEAD(PlayerCmdQueue, PlayerCmd);

typedef struct OutputStream {
    enum AVMediaType type;
    int index;
    int nb_queue_max;
    AVCodecContext* codec_ctx;
    FFFrameQueue queue;
    AVRational time_base;
    AVRational frame_rate;
    int64_t next_pts;
} OutputStream;

typedef struct MediaPlayerContext {
    /* communication with media client */
    int                 tran_fd;
    int                 notify_fd;
    uint32_t            offset;
    media_parcel        parcel;

    int                 event;
    int                 cmd_max;
    int                 state;
    int                 loop_count;
    int                 offload;
    int                 pending_stop;
    int                 audio_idx;
    int                 video_idx;
    uint32_t            nb_streams;
    uint32_t            current_ms;     /** < current timestamp of the decoded frame */
    uint32_t            duration_ms;    /** < duration of whole stream */
    char                name[16];
    pthread_mutex_t     mutex;
    char*               protocol_map;
    struct PlayerCmdQueue cmd_queue;

    AVDictionary*       format_opt;
    AVDictionary*       global_opts;
    AVFormatContext*    format_ctx;
    OutputStream*       streams;        /**< array of all streams, one per output */

    /* avsync parameters */
    int                 frame_duration; /** < frame duration in ms */
    int                 max_latency;    /** < max latency in ms */
    int64_t             ts_base;        /** < pts base for avsync */
    int64_t             lat_base;       /** < audio latency base for avsync */
    enum MediaPlayerSyncMode sync_mode;

    /* audio or video output */
    MediaGraphStream*    audio_output;
    MediaVOutputContext* video_output;
} MediaPlayerContext;

typedef struct MediaPlayerPriv {
    pthread_mutex_t mutex;
    MediaPlayerContext ctxs[MEDIA_PLAYER_MAX_CNT];
} MediaPlayerPriv;

/****************************************************************************
 * Function declaration
 ****************************************************************************/
static int media_player_seek(MediaPlayerContext* ctx, uint32_t ms, int flush);
static int media_player_stop(MediaPlayerContext* ctx);
static int media_player_poll_available(MediaPlayerContext* ctx);
static AVFrame* media_player_queue_pop(MediaPlayerContext* ctx, int idx);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static AVFrame* media_player_generate_slience_frame(MediaPlayerContext* ctx)
{
    AVFrame* frame = av_frame_alloc();
    if (frame) {
        frame->sample_rate = ctx->streams[ctx->audio_idx].codec_ctx->sample_rate;
        frame->format = ctx->streams[ctx->audio_idx].codec_ctx->sample_fmt;
        frame->ch_layout = ctx->streams[ctx->audio_idx].codec_ctx->ch_layout;
        frame->nb_samples = frame->sample_rate * av_get_bytes_per_sample(frame->format) *
                            MEDIA_PLAYER_SLIENCE_FRAME_DURATION / 1000;

        if (av_frame_get_buffer(frame, 0) < 0) {
            av_frame_free(&frame);
            MEDIA_ERR("av_frame_get_buffer failed for slience frame.");
            return NULL;
        }

        av_samples_set_silence((uint8_t**)frame->extended_data, 0, frame->nb_samples,
                               frame->ch_layout.nb_channels, frame->format);
    } else {
        MEDIA_ERR("av_frame_alloc failed for slience frame.");
    }

    return frame;
}

static int media_player_on_event_cb(void *udata, int evt, int64_t args)
{
    MediaPlayerContext* ctx = (MediaPlayerContext*)udata;
    AVFrame* frame;

    MEDIA_DEBUG("audio audio_output event: %d", evt);

    frame = media_player_queue_pop(ctx, ctx->audio_idx);
    if (!frame)
        frame = media_player_generate_slience_frame(ctx);
    if (frame) {
        AVFrame* out_frame = (AVFrame*)(uintptr_t)args;
        av_frame_move_ref(out_frame, frame);
        av_frame_free(&frame);
    } else {
        MEDIA_ERR("audio audio_output recv dat failed.");
    }

    return 0;
}

static inline int media_player_stream_inactive(MediaPlayerContext* ctx, int idx)
{
    return ctx->streams[idx].index == -1 || ctx->streams[idx].codec_ctx == NULL;
}

static int media_player_loop(MediaPlayerContext* ctx)
{
    int ret = AVERROR_EOF;

    if (ctx->loop_count > 0) {
        ret = media_player_seek(ctx, 0, false);
        ctx->loop_count -= 1;
    }

    return ret;
}

static int media_player_queue_cnt(MediaPlayerContext* ctx, int idx)
{
    int count = 0;

    if (idx < 0)
        return 0;

    pthread_mutex_lock(&ctx->mutex);
    count = ff_framequeue_queued_frames(&ctx->streams[idx].queue);
    pthread_mutex_unlock(&ctx->mutex);

    return count;
}

static inline int media_player_is_need_process(MediaPlayerContext* ctx)
{
     return ((ctx->audio_idx != -1 &&
            (media_player_queue_cnt(ctx, ctx->audio_idx) <
             ctx->streams[ctx->audio_idx].nb_queue_max)) ||
            (ctx->video_idx != -1 &&
             (media_player_queue_cnt(ctx, ctx->video_idx) <
             ctx->streams[ctx->video_idx].nb_queue_max)));
}

static AVFrame *media_player_queue_pop(MediaPlayerContext* ctx, int idx)
{
    AVFrame *frame = NULL;

    pthread_mutex_lock(&ctx->mutex);
    if (ff_framequeue_queued_frames(&ctx->streams[idx].queue)) {
        frame = ff_framequeue_take(&ctx->streams[idx].queue);
    }
    pthread_mutex_unlock(&ctx->mutex);
    return frame;
}

static AVFrame *media_player_queue_peek(MediaPlayerContext* ctx, int idx)
{
    AVFrame *frame = NULL;

    pthread_mutex_lock(&ctx->mutex);
    if (ff_framequeue_queued_frames(&ctx->streams[idx].queue)) {
        frame = ff_framequeue_peek(&ctx->streams[idx].queue, 0);
    }
    pthread_mutex_unlock(&ctx->mutex);
    return frame;
}


static int media_player_read_frame(MediaPlayerContext* ctx)
{
    AVPacket pkt = { 0 };
    int i, ret;

    /* read a new packet from input stream */
    ret = av_read_frame(ctx->format_ctx, &pkt);
    if (ret == AVERROR_EOF) {
        /* EOF -> set all decoders for flushing */
        for (i = 0; i < ctx->format_ctx->nb_streams; i++) {
            if (media_player_stream_inactive(ctx, i))
                continue;

            if (!ctx->offload) {
                ret = avcodec_send_packet(ctx->streams[i].codec_ctx, NULL);
                if (ret < 0 && ret != AVERROR_EOF)
                    return ret;
            }
        }
    }

    if (ret < 0)
        return ret;

    /* send the packet to its decoder, if any */
    for (i = 0; i < ctx->format_ctx->nb_streams; i++) {
        if (!media_player_stream_inactive(ctx, i) && pkt.stream_index == ctx->streams[i].index) {
            if (!ctx->offload)
                ret = avcodec_send_packet(ctx->streams[i].codec_ctx, &pkt);
            else //todo
                MEDIA_ERR("don't support offload play\n");

            break;
        }
    }

    av_packet_unref(&pkt);
    return ret == AVERROR_INVALIDDATA ? 0 : ret;
}

static int media_player_queue_push(MediaPlayerContext* ctx, int idx, AVFrame* frame)
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

static void media_player_clear_queue(MediaPlayerContext* ctx, int what)
{
    AVFrame* frame;
    PlayerCmd* msg;
    int i;

    pthread_mutex_lock(&ctx->mutex);
    if (what & MEDIA_PLAYER_CMD_QUEUE_IDX) {
        while ((msg = SIMPLEQ_FIRST(&ctx->cmd_queue)) != NULL) {
            SIMPLEQ_REMOVE_HEAD(&ctx->cmd_queue, entry);
            av_freep(&msg);
        }
    }

    if (what & MEDIA_PLAYER_DATA_QUEUE_IDX) {
        for (i = 0; i < ctx->nb_streams; i++) {
            while (ff_framequeue_queued_frames(&ctx->streams[i].queue)) {
                frame = ff_framequeue_take(&ctx->streams[i].queue);
                av_frame_free(&frame);
            }
        }
    }
    pthread_mutex_unlock(&ctx->mutex);
}

static int media_player_dec_frame(MediaPlayerContext* ctx, int idx, AVFrame** oframe)
{
    AVFrame* frame;
    int ret;

    frame = av_frame_alloc();
    if (!frame)
        return AVERROR(ENOMEM);

    ret = avcodec_receive_frame(ctx->streams[idx].codec_ctx, frame);
    if (ret < 0) {
        av_frame_free(&frame);
        return ret;
    }

    if (frame->pts == AV_NOPTS_VALUE && ctx->streams[idx].next_pts != AV_NOPTS_VALUE)
        frame->pts = ctx->streams[idx].next_pts;

    if (frame->pts != AV_NOPTS_VALUE)
        ctx->streams[idx].next_pts = frame->pts + frame->nb_samples;

    frame->time_base = ctx->streams[idx].time_base;
    ctx->current_ms  = frame->pts * av_q2d(ctx->streams[idx].time_base) * 1000;

    *oframe = frame;
    return 0;
}

static int media_player_dec_frames(MediaPlayerContext* ctx)
{
    int got_frame = 0;
    AVFrame* frame;
    int ret, i;

    for (i = 0; i < ctx->format_ctx->nb_streams; i++) {
        if (media_player_stream_inactive(ctx, i))
            continue;

        if (ctx->offload) {
            ret = AVERROR(EAGAIN);
            continue;
        }

        /* read frame from decoder, add frame queue */
        ret = media_player_dec_frame(ctx, i, &frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            continue;
        else if (ret < 0)
            return ret;

        ret = media_player_queue_push(ctx, i, frame);
        if (ret < 0) {
            av_frame_free(&frame);
            return ret;
        }

        got_frame = 1;
    }

    return got_frame ? 0 : ret;
}

static int media_player_interrupt(void* opaque)
{
    MediaPlayerContext* ctx = opaque;
    PlayerCmd* msg;
    int interrupt = 0;

    pthread_mutex_lock(&ctx->mutex);

    media_player_poll_available(ctx);

    SIMPLEQ_FOREACH(msg, &ctx->cmd_queue, entry)
    {
        if (msg->cmd >= MEDIA_PLAYER_CMD_STOP) {
            interrupt = 1;
            break;
        }
    }

    pthread_mutex_unlock(&ctx->mutex);
    return interrupt;
}

static void media_player_map_protocol(
    MediaPlayerContext* ctx, const char* url, char* dst, int length)
{
    AVDictionary* opts = NULL;
    AVDictionaryEntry* tag;
    char proto[128];

    av_url_split(proto, sizeof(proto), NULL, 0, NULL, 0, NULL, NULL, 0, url);

    if (ctx->protocol_map && proto[0]) {
        av_dict_parse_string(&opts, ctx->protocol_map, ">", "|", 0);
        if ((tag = av_dict_get(opts, proto, NULL, 0))) {
            snprintf(dst, length, "%s:%s", tag->value, url);
            av_dict_free(&opts);
            return;
        }

        av_dict_free(&opts);
    }

    av_strlcpy(dst, url, length);
}

static int media_player_open_decoder(MediaPlayerContext* ctx, OutputStream* stream, AVCodecParameters* codecpar)
{
    const AVCodec* codec;
    int ret;

    codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec) {
        MEDIA_ERR("Failed to find any codec\n");
        return AVERROR(EINVAL);
    }

    stream->codec_ctx = avcodec_alloc_context3(codec);
    if (!stream->codec_ctx) {
        ret = AVERROR(ENOMEM);
        goto out;
    }

    ret = avcodec_parameters_to_context(stream->codec_ctx, codecpar);
    if (ret < 0)
        goto out;

    stream->codec_ctx->thread_count = get_nprocs();

    if ((ret = avcodec_open2(stream->codec_ctx, codec, NULL)) < 0) {
        MEDIA_ERR("Failed to open codec ret %d %s.\n", ret, av_err2str(ret));
        goto out;
    }

    return 0;

out:

    avcodec_free_context(&stream->codec_ctx);
    return ret;
}

static int media_player_init_stream(MediaPlayerContext* ctx)
{
    int i, ret;

    if (!ctx->format_ctx || ctx->format_ctx->nb_streams <= 0)
        return -EINVAL;

    ctx->streams = av_calloc(ctx->format_ctx->nb_streams, sizeof(OutputStream));
    if (!ctx->streams)
        return AVERROR(ENOMEM);

    ctx->nb_streams = ctx->format_ctx->nb_streams;

    for (i = 0; i < ctx->format_ctx->nb_streams; i++) {
        AVCodecParameters* codecpar = ctx->format_ctx->streams[i]->codecpar;
        OutputStream* stream_out = &ctx->streams[i];
        AVStream* stream = ctx->format_ctx->streams[i];

        // step1: init data queue
        if (codecpar->codec_type == AVMEDIA_TYPE_AUDIO  && ctx->audio_idx < 0)
            ctx->audio_idx = i;
        else if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO && ctx->video_idx < 0)
            ctx->video_idx = i;
        else
            continue;

        stream_out->nb_queue_max = MEDIA_PLAYER_DATA_QUEUE_SIZE;
        stream_out->type = codecpar->codec_type;
        ff_framequeue_init(&stream_out->queue, NULL);

        // step2: find best stream by stream type
        ret = av_find_best_stream(ctx->format_ctx, stream->codecpar->codec_type, -1, -1, NULL, 0);
        if (ret < 0) {
            MEDIA_ERR("Failed to find best stream ret %d %s.\n", ret, av_err2str(ret));
            goto out;
        }

        /* Use specify ch_layout if possible, follow guess_input_channel_layout() in ffmpeg.c */
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO &&
            stream->codecpar->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
            av_channel_layout_default(&stream->codecpar->ch_layout,
                stream->codecpar->ch_layout.nb_channels);

        // step3: using codecparam to create decoder
        ret = media_player_open_decoder(ctx, &ctx->streams[i], stream->codecpar);
        if (ret < 0)
            goto out;

        stream->discard = AVDISCARD_DEFAULT;
        if (stream_out->codec_ctx->codec_type == AVMEDIA_TYPE_AUDIO &&
            !av_channel_layout_check(&stream_out->codec_ctx->ch_layout)) {
            ret = av_channel_layout_copy(&stream_out->codec_ctx->ch_layout,
                &stream->codecpar->ch_layout);
            if (ret < 0)
                goto out;
        }

        // step4: init output stream info
        stream_out->index = stream->index;
        stream_out->time_base = stream->time_base;
        stream_out->frame_rate = stream->r_frame_rate;
        stream_out->next_pts = AV_NOPTS_VALUE;
        stream_out->codec_ctx->pkt_timebase = stream->time_base;

        if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            char options[128] = {0};
            // these option is set by user
            snprintf(options, sizeof(options), "format=%s:devname=%s:pix_fmt=%d",
                     "fbdev", "/dev/fb0", AV_PIX_FMT_BGRA);

            ret = media_video_output_open(&ctx->video_output, MEDIA_VOUTPUT_FBDEV, options);
            if (ret < 0) {
                MEDIA_ERR("Failed to open video_output\n");
                goto out;
            }

            ctx->frame_duration = av_rescale(AV_TIME_BASE, stream_out->frame_rate.den,
                                             stream_out->frame_rate.num);
            ctx->max_latency = ctx->frame_duration;
        }
    }

    return 0;

out:

    return ret;
}

static void media_player_close_demuxer(MediaPlayerContext* ctx)
{
    OutputStream* stream;
    int i;

    if (ctx->format_opt)
        av_dict_free(&ctx->format_opt);

    if (ctx->format_ctx) {
        for (i = 0; i < ctx->format_ctx->nb_streams; i++) {
            if (media_player_stream_inactive(ctx, i))
                continue;

            stream = &ctx->streams[i];
            if (stream) {
                stream->index = -1;
                avcodec_free_context(&stream->codec_ctx);
            }
        }
        avformat_close_input(&ctx->format_ctx);
    }

    ctx->current_ms = 0;
}

static int media_player_open_demuxer(MediaPlayerContext* ctx, const char* filename)
{
    const AVInputFormat* iformat = NULL;
    char* name;
    uint32_t seek_point = 0;
    AVDictionaryEntry* tag;
    int ret;

    if ((tag = av_dict_get(ctx->format_opt, "format", NULL, 0))) {
        iformat = av_find_input_format(tag->value);
        if (!iformat)
            return AVERROR(EINVAL);
    }

    name = av_mallocz(MAX_URL_SIZE);
    if (!name)
        return AVERROR(ENOMEM);

    ctx->format_ctx = avformat_alloc_context();
    if (!ctx->format_ctx)
        return AVERROR(ENOMEM);

    ctx->format_ctx->interrupt_callback.callback = media_player_interrupt;
    ctx->format_ctx->interrupt_callback.opaque   = ctx;

    if (ctx->global_opts)
        av_dict_copy(&ctx->format_opt, ctx->global_opts, 0);

    media_player_map_protocol(ctx, filename, name, MAX_URL_SIZE);

    MEDIA_INFO("DEBUG: url %s start open input.\n", name);
    ret = avformat_open_input(&ctx->format_ctx, name, iformat, &ctx->format_opt);
    if (ret < 0) {
        MEDIA_ERR("Failed to avformat_open_input ret %d, %s.\n", ret, av_err2str(ret));
        goto out;
    }

    MEDIA_INFO("DEBUG: url %s open input done.\n", name);

    ret = avformat_find_stream_info(ctx->format_ctx, NULL);
    if (ret < 0) {
        MEDIA_ERR("Failed to find stream info ret %d, %s.\n", ret, av_err2str(ret));
        goto out;
    }

    MEDIA_INFO("DEBUG: url %s find stream info done.\n", name);

    ret = media_player_init_stream(ctx);
    if (ret < 0) {
        MEDIA_ERR("Failed to init movie stream, ret %d, %s.\n", ret, av_err2str(ret));
        goto out;
    }

    if (ctx->format_ctx->duration == AV_NOPTS_VALUE)
        ctx->duration_ms = 0;
    else
        ctx->duration_ms = av_rescale(ctx->format_ctx->duration, 1000, AV_TIME_BASE);

    /* do seek if requested */
    if ((tag = av_dict_get(ctx->format_opt, "seek_point", NULL, 0))) {
        seek_point = strtoul(tag->value, NULL, 0);
    }

    if (seek_point > 0)
        media_player_seek(ctx, seek_point, false);

    av_free(name);
    return 0;

out:

    av_free(name);
    media_player_close_demuxer(ctx);
    return ret;
}

static void media_player_notify_finalize(MediaPlayerContext* ctx)
{
    if (ctx->notify_fd > 0) {
        close(ctx->notify_fd);
        ctx->notify_fd = 0;
        ctx->offset = 0;
    }
}

static int media_player_notify_event(MediaPlayerContext* ctx, int event, int result, const char* extra)
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

static void media_player_event_cb(MediaPlayerContext* ctx, int event, int result, const char* extra)
{
    if (ctx->event)
        media_player_notify_event(ctx, event, result, extra);
}

static int media_player_proc_dat(MediaPlayerContext* ctx)
{
    int ret;
    ret = media_player_dec_frames(ctx);
    if (ret == AVERROR(EAGAIN)) {
        ret = media_player_read_frame(ctx);
        if (ret == AVERROR_EOF && !ctx->offload) {
            do {
                ret = media_player_dec_frames(ctx);
            } while (ret == 0);
        }

        if (ret == AVERROR_EOF)
            ret = media_player_loop(ctx);
    }

    if (ret >= 0 || ret == AVERROR_EXIT)
        return false;
    else if (ret == AVERROR_EOF)
        ret = 0;

    ctx->state = MEDIA_PLAYER_STATE_COMPLETED;
    media_player_event_cb(ctx, MEDIA_EVENT_COMPLETED, ret, NULL);

    if (!ctx->pending_stop)
        return false;

    media_player_stop(ctx);
    return true;
}

static int media_player_seek(MediaPlayerContext* ctx, uint32_t ms, int flush)
{
    int64_t timestamp = ms * 1000LL;
    int i, ret = AVERROR(EPERM);

    if (!ctx->format_ctx)
        goto end;

    if (ctx->format_ctx->start_time != AV_NOPTS_VALUE)
        timestamp += ctx->format_ctx->start_time;

    if (flush)
        media_player_clear_queue(ctx, MEDIA_PLAYER_DATA_QUEUE_IDX);

    ret = avformat_seek_file(ctx->format_ctx, -1, INT64_MIN, timestamp, INT64_MAX, 0);
    if (ret < 0)
        goto end;

    for (i = 0; i < ctx->format_ctx->nb_streams; i++) {
        if (media_player_stream_inactive(ctx, i))
            continue;

        avcodec_flush_buffers(ctx->streams[i].codec_ctx);
    }

    ctx->current_ms = ms;
    ctx->ts_base = AV_NOPTS_VALUE;

end:

    media_player_event_cb(ctx, MEDIA_EVENT_SEEKED, ret, NULL);
    return ret;
}

static void media_player_ctx_init(MediaPlayerContext* ctx)
{
    ctx->state = MEDIA_PLAYER_STATE_STOPPED;
    ctx->cmd_max = MEDIA_PLAYER_CMD_QUEUE_MAX;
    ctx->audio_idx = -1;
    ctx->video_idx = -1;
    ctx->sync_mode = MEDIA_PLAYER_SYNC_MODE_SYSTEM;
    ctx->ts_base = AV_NOPTS_VALUE;
    ctx->lat_base = AV_NOPTS_VALUE;
    SIMPLEQ_INIT(&ctx->cmd_queue);
    media_parcel_init(&ctx->parcel);
    pthread_mutex_init(&ctx->mutex, NULL);
}

static void media_player_ctx_release(MediaPlayerContext* ctx)
{
    ctx->state = MEDIA_PLAYER_STATE_IDLE;
    ctx->audio_idx      = -1;
    ctx->video_idx      = -1;
    ctx->loop_count     = 0;
    ctx->nb_streams     = 0;
    ctx->offload        = 0;
    ctx->pending_stop   = 0;
    ctx->event          = 0;
    media_player_notify_finalize(ctx);
    media_parcel_deinit(&ctx->parcel);
    pthread_mutex_destroy(&ctx->mutex);
}

static int media_player_close(MediaPlayerContext* ctx)
{
    int i;

    for (i = 0; i < ctx->nb_streams; i++) {
        ff_framequeue_free(&ctx->streams[i].queue);
    }

    if (ctx->video_output)
        media_video_output_close(&ctx->video_output);

    av_freep(&ctx->streams);
    return 0;
}

static int media_player_pause(MediaPlayerContext* ctx)
{
    int ret = AVERROR(EPERM);

    if (ctx->state == MEDIA_PLAYER_STATE_STARTED) {
        ctx->state = MEDIA_PLAYER_STATE_PAUSED;
        ret = 0;
    }
    pthread_mutex_lock(&ctx->mutex);
    if (ctx->audio_output)
        media_graph_stream_close(&ctx->audio_output);
    pthread_mutex_unlock(&ctx->mutex);

    media_player_event_cb(ctx, MEDIA_EVENT_PAUSED, ret, NULL);
    return 0;
}

static int media_player_stop(MediaPlayerContext* ctx)
{
    if (ctx->state == MEDIA_PLAYER_STATE_STOPPED)
        return 0;

    media_player_clear_queue(ctx, MEDIA_PLAYER_DATA_QUEUE_IDX);

    media_player_close_demuxer(ctx);

    pthread_mutex_lock(&ctx->mutex);
    if (ctx->audio_output)
        media_graph_stream_close(&ctx->audio_output);
    pthread_mutex_unlock(&ctx->mutex);

    ctx->pending_stop = 0;
    ctx->state = MEDIA_PLAYER_STATE_STOPPED;

    media_player_event_cb(ctx, MEDIA_EVENT_STOPPED, 0, NULL);
    return 0;
}

static int media_player_start(MediaPlayerContext* ctx)
{
    int ret = 0;

    if (ctx->state != MEDIA_PLAYER_STATE_PREPARED &&
        ctx->state != MEDIA_PLAYER_STATE_PAUSED &&
        ctx->state != MEDIA_PLAYER_STATE_COMPLETED) {
        ret = AVERROR(EPERM);
        goto error;
    }

    if (ctx->audio_idx >= 0 && !ctx->audio_output) {
        ret = media_graph_stream_open(&ctx->audio_output , ctx->name, -1, 0, 0,
                                          media_player_on_event_cb, ctx);
        if (ret < 0) {
            MEDIA_ERR("media_graph_stream_open failed.\n");
            ret = AVERROR(EINVAL);
            goto error;
        }
    }

    ctx->state = MEDIA_PLAYER_STATE_STARTED;
    ctx->ts_base = AV_NOPTS_VALUE;

error:
    media_player_event_cb(ctx, MEDIA_EVENT_STARTED, ret, NULL);
    return 0;
}

static int media_player_prepare(MediaPlayerContext* ctx, const char* filename)
{
    int ret = AVERROR(EPERM);

    if (ctx->state != MEDIA_PLAYER_STATE_STOPPED)
        goto out;

    ret = media_player_open_demuxer(ctx, filename);
    if (ret >= 0)
        ctx->state = MEDIA_PLAYER_STATE_PREPARED;

out:
    media_player_event_cb(ctx, MEDIA_EVENT_PREPARED, ret, NULL);
    return 0;
}

static int media_player_send_cmd(MediaPlayerContext* ctx, const int cmd, const void* data, size_t size)
{
    PlayerCmd *msg, *tmp;
    int cnt = 0;

    msg = av_malloc(sizeof(PlayerCmd) + size);
    if (!msg)
        return AVERROR(ENOMEM);

    msg->cmd = cmd;

    if (data && size)
        memcpy(msg->data, data, size);

    pthread_mutex_lock(&ctx->mutex);

    SIMPLEQ_FOREACH(tmp, &ctx->cmd_queue, entry) cnt++;
    if (cnt >= ctx->cmd_max && msg->cmd < MEDIA_PLAYER_CMD_STOP) {
        pthread_mutex_unlock(&ctx->mutex);
        av_freep(&msg);

        return AVERROR(ENOMEM);
    }

    SIMPLEQ_INSERT_TAIL(&ctx->cmd_queue, msg, entry);
    pthread_mutex_unlock(&ctx->mutex);

    return 0;
}

static int media_player_proc_cmd(MediaPlayerContext* ctx, PlayerCmd* msg)
{
    uint32_t time, pending_stop;
    int exit = false;

    switch (msg->cmd) {
    case MEDIA_PLAYER_CMD_SET_EVENT:
        break;

    case MEDIA_PLAYER_CMD_SET_OPTIONS:
        av_dict_parse_string(&ctx->format_opt, msg->data, "=", ":", 0);
        break;

    case MEDIA_PLAYER_CMD_SET_LOOP:
        ctx->loop_count = strtol(msg->data, NULL, 0);
        break;

    case MEDIA_PLAYER_CMD_PREPARE:
        media_player_prepare(ctx, msg->data);
        break;

    case MEDIA_PLAYER_CMD_START:
        media_player_start(ctx);
        break;

    case MEDIA_PLAYER_CMD_PAUSE:
        media_player_pause(ctx);
        break;

    case MEDIA_PLAYER_CMD_SEEK:
        time = strtoul(msg->data, NULL, 0);
        media_player_seek(ctx, time, true);
        break;

    case MEDIA_PLAYER_CMD_CLOSE:
        pending_stop = strtoul(msg->data, NULL, 0);
        if (pending_stop && ctx->state < MEDIA_PLAYER_STATE_STOPPED) {
            ctx->pending_stop = pending_stop;
            break;
        }
        media_player_close(ctx);
        exit = true;
        break;
    case MEDIA_PLAYER_CMD_STOP:
    case MEDIA_PLAYER_CMD_RESET:
        media_player_stop(ctx);
        break;
    default:
        break;
    }

    av_freep(&msg);
    return exit;
}

int media_player_process_cmd(MediaPlayerContext* ctx, const char* target, const char* cmd, const char* arg, char* res, int res_len)
{
    char url[PATH_MAX];
    int ret = 0;

    if (!ctx)
        return -EINVAL;

    MEDIA_INFO("cmd: %s, arg %s, target %s.\n", cmd, arg ? arg : "NULL",
               target ? target : "NULL");

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
        ret = media_player_send_cmd(ctx, MEDIA_PLAYER_CMD_PREPARE, arg, strlen(arg) + 1);
    } else if (!strcmp(cmd, "close") && arg) {
        media_player_clear_queue(ctx, MEDIA_PLAYER_CMD_QUEUE_IDX);
        ret = media_player_send_cmd(ctx, MEDIA_PLAYER_CMD_CLOSE, arg, strlen(arg) + 1);
    } else if (!strcmp(cmd, "start")) {
        ret = media_player_send_cmd(ctx, MEDIA_PLAYER_CMD_START, NULL, 0);
    } else if (!strcmp(cmd, "stop")) {
        ret = media_player_send_cmd(ctx, MEDIA_PLAYER_CMD_STOP, NULL, 0);
    } else if (!strcmp(cmd, "reset")) {
        media_player_clear_queue(ctx, MEDIA_PLAYER_CMD_QUEUE_IDX);
        ret = media_player_send_cmd(ctx, MEDIA_PLAYER_CMD_RESET, NULL, 0);
    } else if (!strcmp(cmd, "seek") && arg) {
        ret = media_player_send_cmd(ctx, MEDIA_PLAYER_CMD_SEEK, arg, strlen(arg) + 1);
    } else if (!strcmp(cmd, "pause")) {
        ret = media_player_send_cmd(ctx, MEDIA_PLAYER_CMD_PAUSE, NULL, 0);
    } else if (!strcmp(cmd, "set_loop") && arg) {
        ret = media_player_send_cmd(ctx, MEDIA_PLAYER_CMD_SET_LOOP, arg, strlen(arg) + 1);
    } else if (!strcmp(cmd, "set_options") && arg) {
        ret = media_player_send_cmd(ctx, MEDIA_PLAYER_CMD_SET_OPTIONS, arg, strlen(arg) + 1);
    } else {
        MEDIA_ERR("unknown cmd: %s.\n", cmd);
        return AVERROR(EINVAL);
    }

    return ret;
}

int media_player_onreceive(MediaPlayerContext* ctx, media_parcel* in, media_parcel* out)
{
    const char *target = NULL, *cmd = NULL, *arg = NULL;
    int32_t len = 0, flags = 0, id = 0, ret;
    char* response = NULL;

    media_parcel_read_int32(in, &id);

    switch (id) {
    case MEDIA_ID_PLAYER:
        media_parcel_read_scanf(in, "%s%s%s%i", &target, &cmd, &arg, &len);
        if (len > 0)
            response = zalloc(len);

        ret = media_player_process_cmd(ctx, target, cmd, arg, response, len);
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

static int media_player_create_notify(MediaPlayerContext* ctx, media_parcel* parcel)
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
        family = AF_RPMSG;
        rpmsg_addr.rp_family = AF_RPMSG;
        strlcpy(rpmsg_addr.rp_name, key, RPMSG_SOCKET_NAME_SIZE);
        strlcpy(rpmsg_addr.rp_cpu, cpu, RPMSG_SOCKET_CPU_SIZE);
        addr = (struct sockaddr*)&rpmsg_addr;
        len  = sizeof(struct sockaddr_rpmsg);
    } else {
        family = PF_LOCAL;
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

static void media_player_conn_close(MediaPlayerContext* ctx)
{
    close(ctx->tran_fd);
    ctx->tran_fd = -EPERM;
    ctx->offset = 0;
    media_parcel_deinit(&ctx->parcel);
}

static int media_player_poll_available(MediaPlayerContext* ctx)
{
    int ret = -EINVAL;
    uint32_t code;
    media_parcel ack;
    struct pollfd fds[1];
    struct pollfd* fd = &fds[0];
    fds[0].fd = ctx->tran_fd;
    fds[0].events = POLLIN;
    fds[0].revents = 0;

    ret = poll(fds, 1, 2);
    if (ret == -1) {
        return ret;
    } else if (ret == 0) {
        MEDIA_DEBUG("poll timeout\n");
        return ret;
    }

    if (fd->revents & POLLERR)
        goto out;

    while (1) {
        ret = media_parcel_recv(&ctx->parcel, ctx->tran_fd, &ctx->offset, MSG_DONTWAIT);
        if (ret < 0)
            break;

        code = media_parcel_get_code(&ctx->parcel);
        switch (code) {
        case MEDIA_PARCEL_SEND:
            media_player_onreceive(ctx, &ctx->parcel, NULL);
            break;

        case MEDIA_PARCEL_SEND_ACK:
            media_parcel_init(&ack);
            media_player_onreceive(ctx, &ctx->parcel, &ack);
            ret = media_parcel_send(&ack, ctx->tran_fd, MEDIA_PARCEL_REPLY, 0);
            media_parcel_deinit(&ack);
            break;

        case MEDIA_PARCEL_CREATE_NOTIFY:
            ret = media_player_create_notify(ctx, &ctx->parcel);
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

    return 0;

out:
    MEDIA_DEBUG("fd:%d revent:%d\n", fd->fd, (int)fd->revents);
    media_player_conn_close(ctx);
    return 0;
}

static MediaPlayerContext* media_player_get_available_session(MediaPlayerPriv* priv)
{
    MediaPlayerContext* ctx = NULL;
    int i;

    for (i = 0; i < MEDIA_PLAYER_MAX_CNT; i++) {
        ctx = &priv->ctxs[i];
        if (ctx->state == MEDIA_PLAYER_STATE_IDLE)
            break;
    }

    return ctx;
}

static void media_player_dump(MediaPlayerPriv* priv)
{
    MediaPlayerContext *ctx;
    AVBPrint buf;
    int i;

    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_UNLIMITED);
    av_bprintf(&buf, "\n--------------player dump start-------------\n");
    for (i = 0; i < MEDIA_PLAYER_MAX_CNT; i++) {
        ctx = &priv->ctxs[i];
        if (ctx->state == MEDIA_PLAYER_STATE_IDLE)
            continue;
        av_bprintf(&buf, "player[%d, %s] state:%d", i, ctx->name, ctx->state);
        if (ctx->audio_idx >=0)
            av_bprintf(&buf, ", a: %d %s %"PRId64" %d ch:%d %d", ctx->audio_idx,
            avcodec_get_name(ctx->streams[ctx->audio_idx].codec_ctx->codec_id),
            ctx->streams[ctx->audio_idx].codec_ctx->bit_rate,
            ctx->streams[ctx->audio_idx].codec_ctx->sample_rate,
            ctx->streams[ctx->audio_idx].codec_ctx->ch_layout.nb_channels,
            media_player_queue_cnt(ctx, ctx->audio_idx));
        if (ctx->video_idx >=0)
            av_bprintf(&buf, ", v: %d %s %dx%d %d", ctx->video_idx,
            avcodec_get_name(ctx->streams[ctx->video_idx].codec_ctx->codec_id),
            ctx->streams[ctx->video_idx].codec_ctx->width,
            ctx->streams[ctx->video_idx].codec_ctx->height,
            media_player_queue_cnt(ctx, ctx->video_idx));
    }
    av_bprintf(&buf, "\n--------------player dump end---------------\n");
    MEDIA_INFO("%s\n", buf.str);
    av_bprint_finalize(&buf, NULL);
}

static void media_player_get_timestamp(MediaPlayerContext* ctx, int64_t *ts, int64_t *lat)
{
    switch (ctx->sync_mode) {
    case MEDIA_PLAYER_SYNC_MODE_AUDIO:
        // TODO: get audio timestamp and latency
        break;
    case MEDIA_PLAYER_SYNC_MODE_SYSTEM:
        *lat = 0;
        *ts = av_gettime_relative();
        break;
    default:
        *lat = 0;
        *ts = AV_NOPTS_VALUE;
        break;
    }
}

static int media_player_sync_video(MediaPlayerContext *ctx, int64_t pts, int64_t ts, int64_t lat)
{
    int64_t now, diff;

    if (ctx->ts_base == AV_NOPTS_VALUE) {
        if (ctx->lat_base == AV_NOPTS_VALUE)
            ctx->ts_base = ts;
        else
            ctx->ts_base = ts - pts;
        ctx->lat_base = lat;
        MEDIA_INFO("sync pts:%"PRId64" ts:%"PRId64" base:%"PRId64" lat:%"PRId64"\n",
                   pts, ts, ctx->ts_base, lat);
    }

    now   = ts - ctx->ts_base;
    diff  = pts - now;
    diff += ctx->lat_base;

    MEDIA_DEBUG("sync pts:%"PRId64" ts:%"PRId64" now:%"PRId64" diff:%"PRId64" lat:%"PRId64"\n",
                pts, ts, now, diff, lat);

    if (diff > ctx->frame_duration)
        return ctx->frame_duration;
    else if (diff >= 0)
        return diff;
    else if (diff >= -ctx->max_latency)
        return 0;
    else
        return -1;

    return 0;
}

static void* media_player_thread(void* arg)
{
    MediaPlayerContext* ctx = (MediaPlayerContext*)arg;
    MEDIA_INFO("create player thread.\n");
    int64_t pts, ts, latency;
    int exit = false;
    int diff;
    PlayerCmd* msg;
    int ret = 0;

    while (1) {
        pthread_mutex_lock(&ctx->mutex);
        ret = media_player_poll_available(ctx);
        if (ret < 0)
            MEDIA_ERR("poll available failed %d\n", ret);
        if ((msg = SIMPLEQ_FIRST(&ctx->cmd_queue)) != NULL) {
            SIMPLEQ_REMOVE_HEAD(&ctx->cmd_queue, entry);
            pthread_mutex_unlock(&ctx->mutex);
            if (media_player_proc_cmd(ctx, msg))
                exit = true;
        } else if (ctx->state == MEDIA_PLAYER_STATE_STARTED) {
            pthread_mutex_unlock(&ctx->mutex);
            if (media_player_is_need_process(ctx))
                exit = media_player_proc_dat(ctx);

            if (media_player_queue_cnt(ctx, ctx->video_idx) > 0) {
                AVFrame* frame = media_player_queue_peek(ctx, ctx->video_idx);
                media_player_get_timestamp(ctx, &ts, &latency);
                pts = av_rescale_q(frame->pts, frame->time_base, AV_TIME_BASE_Q);
                diff = media_player_sync_video(ctx, pts, ts, latency);
                if (diff >= 0) {
                    if (diff > 0)
                        usleep(diff);
                    frame = media_player_queue_pop(ctx, ctx->video_idx);
                    ret = media_video_output_write_frame(ctx->video_output, frame);
                    if (ret < 0)
                        MEDIA_ERR("video_output write frame failed %d\n", ret);
                } else {
                    frame = media_player_queue_pop(ctx, ctx->video_idx);
                    av_frame_free(&frame);
                    MEDIA_ERR("drop frame pts:%"PRId64" ts:%"PRId64" diff:%d\n",
                              pts, ts, diff);
                }
            }
        } else if (exit) {
            ctx->state = MEDIA_PLAYER_STATE_IDLE;
            pthread_mutex_unlock(&ctx->mutex);
            break;
        } else {
            pthread_mutex_unlock(&ctx->mutex);
        }
    }

    media_player_ctx_release(ctx);

    MEDIA_INFO("exit player thread.\n");
    return NULL;
}

static int media_player_open(MediaPlayerContext* ctx, const char* name)
{
    struct sched_param param;
    pthread_attr_t attr;
    pthread_t thread;
    int ret;

    if (ctx->state != MEDIA_PLAYER_STATE_IDLE || name == NULL)
        return AVERROR(EINVAL);

    strlcpy(ctx->name, name, sizeof(ctx->name));

    media_player_ctx_init(ctx);

    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, CONFIG_MEDIA_PLAYER_STACKSIZE);
    param.sched_priority = CONFIG_MEDIA_PLAYER_PRIORITY;
    pthread_attr_setschedparam(&attr, &param);
    ret = pthread_create(&thread, &attr, media_player_thread, ctx);
    if (ret != 0) {
        ctx->state = MEDIA_PLAYER_STATE_IDLE;
        return AVERROR(ret);
    }

    pthread_setname_np(thread, ctx->name);
    pthread_detach(thread);

    return 0;
}

static int media_player_init(MediadPlugin* handle)
{
    MediaPlayerPriv* priv = handle->priv;

    pthread_mutex_init(&priv->mutex, NULL);
    /* TODO: add parse player config, like global options for different stream. */

    return 0;
}

static int media_player_uninit(MediadPlugin* handle)
{
    MediaPlayerPriv* priv = handle->priv;

    pthread_mutex_destroy(&priv->mutex);

    return 0;
}

static int media_player_handler(MediadPlugin* handle, struct media_server_conn* conn, const char* target, const char* cmd, const char* arg, int flags, char* res, int res_len)
{
    MediaPlayerPriv* priv = handle->priv;
    int ret = 0;

    MEDIA_INFO("cmd: %s, arg %s, target %s.\n", cmd, arg ? arg : "NULL", target ? target : "NULL");

    pthread_mutex_lock(&priv->mutex);

    if (!strcmp(cmd, "open")) {
        MediaPlayerContext* ctx = media_player_get_available_session(priv);
        if (!ctx) {
            MEDIA_ERR("player open failed...\n");
            ret = -ENOMEM;
            goto out;
        }

        ctx->tran_fd = media_server_get_tran_fd(conn);
        if (ctx->tran_fd < 0) {
            MEDIA_ERR("player get tran fd failed...\n");
            ret = -EINVAL;
            goto out;
        }

        media_server_clean_conn(conn);

        ret = media_player_open(ctx, arg);
        if (ret < 0)
            goto out;

        strncpy(ctx->name, arg, sizeof(ctx->name));

        MEDIA_INFO("open player success...\n");
    } else if (!strcmp(cmd, "dump")) {
        media_player_dump(priv);
    }


out:
    pthread_mutex_unlock(&priv->mutex);

    return ret;
}

MediadPlugin media_player_plugin = {
    .name            = "media_player",
    .priv_size       = sizeof(struct MediaPlayerPriv),
    .priv            = NULL,
    .init            = media_player_init,
    .get             = NULL,
    .available       = NULL,
    .run_once        = NULL,
    .uninit          = media_player_uninit,
    .process_command = media_player_handler,
};
