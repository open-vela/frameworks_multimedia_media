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
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/sysinfo.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include "libavcodec/avcodec.h"
#include "libavfilter/filters.h"
#include "libavfilter/framequeue.h"
#include "libavformat/avformat.h"
#include "libavformat/internal.h"
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/bprint.h"
#include "libavutil/opt.h"

#include "audio_graph.h"
#include "media_common.h"
#include "media_plugin.h"
#include "media_server.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MEDIA_RECORDER_CMD_QUEUE_IDX (1 << 0)
#define MEDIA_RECORDER_DATA_QUEUE_IDX (1 << 1)

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
    /* communication with media client */
    int tran_fd;
    int notify_fd;
    int event_fd;
    uint32_t offset;
    media_parcel parcel;

    int event;
    int cmd_max;
    int state;
    int exit;
    int audio_idx;
    int video_idx;
    char name[64];
    uint32_t aframe_cnt;
    uint32_t nb_streams; /* total stream count */
    uint32_t current_ms;
    pthread_mutex_t mutex;
    OutputStream* streams; /* output stream */
    AVDictionary* format_opt; /* format options */
    AVDictionary* global_opts;
    AVFormatContext* format_ctx; /* output format context */
    const AVOutputFormat* format; /* output format */
    struct RecorderCmdQueue cmd_queue;

    int audio_input_state; /** < 1: audio input is started, 0: not started */
    AVFilterContext* audio_input;
} MediaRecorderContext;

typedef struct MediaRecorderPriv {
    MediaRecorderContext ctxs[CONFIG_MEDIA_RECORDER_MAX_CNT];
} MediaRecorderPriv;

typedef struct {
    const char* dict_key;
    const char* codec_option;
} CodecOption;

typedef struct {
    enum AVCodecID codec_id;
    const CodecOption* options;
} CodecSpecificOptions;

static const CodecOption opus_codec_options[] = {
    { "bitrate", "b" },
    { "vbr", "vbr" },
    { "level", "compression_level" },
    { "application", "application" },
    { "frame_duration", "frame_duration" },
    { NULL, NULL }
};

static const CodecSpecificOptions codec_specific_map[] = {
    { AV_CODEC_ID_OPUS, opus_codec_options },
    { AV_CODEC_ID_NONE, NULL }
};

/****************************************************************************
 * Function declaration
 ****************************************************************************/
static int media_recorder_poll_available(MediaRecorderContext* ctx, struct pollfd* fd);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static inline int media_recorder_is_exit(MediaRecorderContext* ctx)
{
    return ctx->exit && !ctx->audio_input_state;
}

static void media_recorder_notify_finalize(MediaRecorderContext* ctx)
{
    if (ctx->notify_fd > 0) {
        close(ctx->notify_fd);
        ctx->notify_fd = 0;
        ctx->offset = 0;
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
        ret = media_parcel_send(&notify, ctx->notify_fd, MEDIA_PARCEL_SEND, MSG_DONTWAIT);

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

    if (idx < 0)
        return 0;

    pthread_mutex_lock(&ctx->mutex);
    count = ff_framequeue_queued_frames(&ctx->streams[idx].queue);
    pthread_mutex_unlock(&ctx->mutex);

    return count;
}

static bool media_recorder_dat_valid(MediaRecorderContext* ctx)
{
    int i;

    if (ctx->state != MEDIA_RECORDER_STATE_STARTED && ctx->state != MEDIA_RECORDER_STATE_PAUSED)
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

    if (idx < 0)
        return NULL;

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

    if (idx < 0)
        return AVERROR(EINVAL);

    pthread_mutex_lock(&ctx->mutex);
    ctx->aframe_cnt++;
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

        if (frame)
            ctx->current_ms = frame->pts / 1000;

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

static enum AVCodecID media_recorder_find_encoder_id(const char* name, enum AVMediaType type)
{
    const AVCodecDescriptor* desc;
    const AVCodec* codec;

    codec = avcodec_find_encoder_by_name(name);

    if (!codec && (desc = avcodec_descriptor_get_by_name(name)))
        codec = avcodec_find_encoder(desc->id);

    if (!codec || codec->type != type) {
        MEDIA_ERR("cannot find proper codec for %s.", name);
        return AV_CODEC_ID_NONE;
    }

    return codec->id;
}

static int media_recorder_apply_options(AVCodecContext* avctx, AVDictionary* format_opt)
{
    const CodecSpecificOptions* spec_opts;
    const CodecOption* map;
    AVDictionaryEntry* tag;

    for (spec_opts = codec_specific_map; spec_opts->codec_id != AV_CODEC_ID_NONE; spec_opts++) {
        if (spec_opts->codec_id == avctx->codec_id) {
            for (map = spec_opts->options; map->dict_key != NULL; map++) {
                tag = av_dict_get(format_opt, map->dict_key, NULL, 0);
                if (tag && map->codec_option) {
                    if (strcmp(tag->value, "") != 0) {
                        av_opt_set_int(avctx, map->codec_option, strtol(tag->value, NULL, 0), 0);
                    }
                }
            }
            break;
        }
    }

    return 0;
}

static int media_recorder_open_encoder(MediaRecorderContext* ctx, int idx)
{
    int ret, i = 0, num_sample_fmts, num_samplerates, num_ch_layouts;
    int width = 0, height = 0;
    const enum AVSampleFormat* sample_fmts = NULL;
    const AVChannelLayout* ch_layouts = NULL;
    const int* supported_samplerates = NULL;
    int sample_rate, sample_fmt;
    AVChannelLayout ch_layout;
    AVDictionary* dict = NULL;
    AVDictionaryEntry* tag;
    const AVCodec* enc;
    AVStream* stream;

    if (ctx->streams[idx].type == AVMEDIA_TYPE_AUDIO) {
        tag = av_dict_get(ctx->format_opt, "audio_codec", NULL, 0);
        if (tag)
            enc = avcodec_find_encoder(media_recorder_find_encoder_id(tag->value, AVMEDIA_TYPE_AUDIO));
        else
            enc = avcodec_find_encoder(ctx->format_ctx->oformat->audio_codec);
    } else {
        tag = av_dict_get(ctx->format_opt, "video_codec", NULL, 0);
        if (tag)
            enc = avcodec_find_encoder(media_recorder_find_encoder_id(tag->value, AVMEDIA_TYPE_VIDEO));
        else
            enc = avcodec_find_encoder(ctx->format_ctx->oformat->video_codec);
    }

    if (!enc) {
        MEDIA_ERR("don't find match encoder\n");
        return AVERROR(EINVAL);
    }

    // get audio sample_format
    ret = avcodec_get_supported_config(NULL, enc, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0,
        (const void**)&sample_fmts, &num_sample_fmts);
    if (ret < 0) {
        MEDIA_ERR("get supported sample_format config failed\n");
        return ret;
    }

    if ((tag = av_dict_get(ctx->format_opt, "sample_fmt", NULL, 0))) {
        sample_fmt = strtol(tag->value, NULL, 0);
        for (i = 0; i < num_sample_fmts; i++) {
            if (sample_fmt == sample_fmts[i])
                break;
        }

        if (i == num_sample_fmts && num_sample_fmts != 0) {
            MEDIA_ERR("sample format %d is not supported by the encoder (%s) \n",
                sample_fmt, enc->name);
            return AVERROR(EINVAL);
        }
    } else {
        if (num_sample_fmts)
            sample_fmt = sample_fmts[0];
        else {
            sample_fmt = AV_SAMPLE_FMT_S16;
            MEDIA_WARN("no specify the sample_fmt, use default value %s\n",
                av_get_sample_fmt_name(sample_fmt));
        }
    }

    // get audio sample_rate
    ret = avcodec_get_supported_config(NULL, enc, AV_CODEC_CONFIG_SAMPLE_RATE, 0,
        (const void**)&supported_samplerates, &num_samplerates);
    if (ret < 0) {
        MEDIA_ERR("get supported sample_rate config failed\n");
        return ret;
    }

    if ((tag = av_dict_get(ctx->format_opt, "sample_rate", NULL, 0))) {
        sample_rate = strtol(tag->value, NULL, 0);
        for (i = 0; i < num_samplerates; i++) {
            if (sample_rate == supported_samplerates[i])
                break;
        }

        if (i == num_samplerates && num_samplerates != 0) {
            MEDIA_ERR("sample rate %d is not supported by the encoder (%s) \n",
                sample_rate, enc->name);
            return AVERROR(EINVAL);
        }
    } else {
        if (num_samplerates)
            sample_rate = supported_samplerates[0];
        else {
            sample_rate = 16000;
            MEDIA_WARN("no specify the sample_rate, use default value %d\n", sample_rate);
        }
    }

    // get audio channel_layout
    ret = avcodec_get_supported_config(NULL, enc, AV_CODEC_CONFIG_CHANNEL_LAYOUT, 0,
        (const void**)&ch_layouts, &num_ch_layouts);
    if (ret < 0) {
        MEDIA_ERR("get supported channel layout config failed\n");
        return ret;
    }

    if ((tag = av_dict_get(ctx->format_opt, "ch_layout", NULL, 0))) {
        if (av_channel_layout_from_string(&ch_layout, tag->value) < 0) {
            MEDIA_ERR("invalid channel layout %s\n", tag->value);
            return AVERROR(EINVAL);
        }

        for (i = 0; i < num_ch_layouts; i++) {
            if (av_channel_layout_compare(&ch_layouts[i], &ch_layout) == 0)
                break;
        }

        if (i == num_ch_layouts && num_ch_layouts != 0) {
            MEDIA_ERR("channel %d is not supported by the encoder (%s) \n",
                ch_layout.nb_channels, enc->name);
            return AVERROR(EINVAL);
        }
    } else {
        if (num_ch_layouts)
            ch_layout = ch_layouts[0];
        else {
            av_channel_layout_default(&ch_layout, 1);
            MEDIA_WARN("no specify the channel layout, use default value %d\n",
                ch_layout.nb_channels);
        }
    }

    if ((tag = av_dict_get(ctx->format_opt, "width", NULL, 0)))
        width = strtol(tag->value, NULL, 0);

    if ((tag = av_dict_get(ctx->format_opt, "height", NULL, 0)))
        height = strtol(tag->value, NULL, 0);

    ctx->streams[idx].enc_ctx = avcodec_alloc_context3(enc);
    if (!ctx->streams[idx].enc_ctx) {
        return AVERROR(ENOMEM);
    }

    if (ctx->streams[idx].type == AVMEDIA_TYPE_AUDIO) {
        ctx->streams[idx].enc_ctx->sample_fmt = sample_fmt;
        ctx->streams[idx].enc_ctx->sample_rate = sample_rate;
        ctx->streams[idx].enc_ctx->ch_layout = ch_layout;
        ctx->streams[idx].enc_ctx->time_base = (AVRational) { 1, 1000000 };
    } else {
        ctx->streams[idx].enc_ctx->width = width;
        ctx->streams[idx].enc_ctx->height = height;
    }

    if (ctx->format_ctx->oformat->flags & AVFMT_GLOBALHEADER)
        ctx->streams[idx].enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    ctx->streams[idx].enc_ctx->strict_std_compliance = FF_COMPLIANCE_UNOFFICIAL;

    media_recorder_apply_options(ctx->streams[idx].enc_ctx, ctx->format_opt);

    if (ctx->format_opt)
        av_dict_copy(&dict, ctx->format_opt, 0);

    ret = avcodec_open2(ctx->streams[idx].enc_ctx, enc, &dict);
    av_dict_free(&dict);
    if (ret < 0)
        goto out;

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

static void media_recorder_release_stream(MediaRecorderContext* ctx)
{
    int i;

    for (i = 0; i < ctx->nb_streams; i++) {
        ff_framequeue_free(&ctx->streams[i].queue);
    }

    av_freep(&ctx->streams);
}

static int media_recorder_init_stream(MediaRecorderContext* ctx)
{
    AVDictionaryEntry* tag;
    int stream_cnt;
    int types[2];
    int i;

    // init output stream by stream type.
    // a: only audio, v: only video, others: audio and video
    if (ctx->name[0] == 'a') {
        stream_cnt = 1;
        types[0] = AVMEDIA_TYPE_AUDIO;
        ctx->audio_idx = 0;
    } else if (ctx->name[0] == 'v') {
        stream_cnt = 1;
        types[0] = AVMEDIA_TYPE_VIDEO;
        ctx->video_idx = 0;
    } else {
        stream_cnt = 2;
        types[0] = AVMEDIA_TYPE_AUDIO;
        types[1] = AVMEDIA_TYPE_VIDEO;
        ctx->audio_idx = 0;
        ctx->video_idx = 1;
    }

    media_recorder_release_stream(ctx);

    ctx->streams = av_calloc(stream_cnt, sizeof(OutputStream));
    if (!ctx->streams)
        return AVERROR(ENOMEM);

    for (i = 0; i < stream_cnt; i++) {
        ctx->streams[i].index = i;
        ctx->streams[i].type = types[i];
        ff_framequeue_init(&ctx->streams[i].queue, NULL);

        if ((tag = av_dict_get(ctx->format_opt, "datqmax", NULL, 0))) {
            ctx->streams[i].nb_queue_max = strtol(tag->value, NULL, 0);
        } else {
            ctx->streams[i].nb_queue_max = CONFIG_MEDIA_RECORDER_DATA_QUEUE_SIZE;
        }
    }

    ctx->nb_streams = stream_cnt;

    return 0;
}

static int media_recorder_on_event_cb(void* udata, int evt, int64_t args)
{
    MediaRecorderContext* ctx = (MediaRecorderContext*)udata;
    AVFrame* in_frame = (AVFrame*)(uintptr_t)args;
    AVFrame* frame;
    uint64_t cnt = 1;

    if (evt < 0) {
        MEDIA_INFO("ctx %p received unlink event form audio_input.\n", ctx);
        ctx->audio_input_state = 0;
        write(ctx->event_fd, &cnt, sizeof(cnt));
        return 0;
    }

    frame = av_frame_clone(in_frame);
    if (!frame)
        return AVERROR(ENOMEM);

    media_recorder_queue_push(ctx, ctx->audio_idx, frame);
    write(ctx->event_fd, &cnt, sizeof(cnt));
    return 0;
}

static void media_recorder_close_muxer(MediaRecorderContext* ctx)
{
    int i;

    for (i = 0; i < ctx->nb_streams; i++) {
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

    MEDIA_INFO("ctx %p recorder last position %" PRIu32 ".\n", ctx, ctx->current_ms);
    ctx->current_ms = 0;
}

static void media_recorder_conn_close(MediaRecorderContext* ctx)
{
    close(ctx->tran_fd);
    ctx->tran_fd = -EPERM;
    ctx->offset = 0;
    media_parcel_deinit(&ctx->parcel);
}

static void media_recorder_clean(MediaRecorderContext* ctx)
{
    if (ctx->state != MEDIA_RECORDER_STATE_STOPPED) {
        media_recorder_close_muxer(ctx);
        ctx->state = MEDIA_RECORDER_STATE_STOPPED;
        media_recorder_event_cb(ctx, MEDIA_EVENT_STOPPED, 0, NULL);
    }
}

static int media_recorder_interrupt(void* opaque)
{
    MediaRecorderContext* ctx = opaque;
    RecorderCmd* msg;
    int interrupt = 0;
    struct pollfd fds[1];
    struct pollfd* fd = &fds[0];
    uint64_t cnt = 1;
    fds[0].fd = ctx->tran_fd;
    fds[0].events = POLLIN;
    fds[0].revents = 0;

    media_recorder_poll_available(ctx, fd);

    SIMPLEQ_FOREACH(msg, &ctx->cmd_queue, entry)
    {
        if (msg->cmd >= MEDIA_RECORDER_CMD_STOP) {
            interrupt = 1;
            break;
        }
    }
    write(ctx->event_fd, &cnt, sizeof(cnt));
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

    if (ctx->format_opt) {
        av_dict_copy(&dict, ctx->format_opt, 0);
        ret = av_opt_set_dict2(ctx->format_ctx, &dict, AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            av_dict_free(&dict);
            goto out;
        }
    }

    cb.callback = media_recorder_interrupt;
    cb.opaque = ctx;

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
    uint64_t cnt = 1;
    int ret;
    int i;
    for (i = 0; i < ctx->nb_streams; i++) {
        frame = media_recorder_queue_pop(ctx, i);
        if (!frame)
            continue;

        if (frame->pts == AV_NOPTS_VALUE || frame->pts == INT64_MIN || frame->pts < 0) {
            MEDIA_WARN("Invalid PTS detected: %" PRId64 ", resetting to sync_pts: %" PRId64 "\n",
                frame->pts, ctx->streams[i].sync_pts);
            frame->pts = ctx->streams[i].sync_pts;
        }

        if (frame->pts >= ctx->streams[i].sync_pts) {
            frame->pts -= ctx->streams[i].sync_pts;
        } else {
            MEDIA_WARN("PTS (%" PRId64 ") < sync_pts (%" PRId64 "), clamping to 0\n",
                frame->pts, ctx->streams[i].sync_pts);
            frame->pts = 0;
        }

        frame->pict_type = AV_PICTURE_TYPE_NONE;

        /* user request stop, flush code which data = 0 */
        if (!frame->data[0]) {
            MEDIA_INFO("ctx %p received empty frame\n", ctx);
            av_frame_free(&frame);
        } else if (ctx->state == MEDIA_RECORDER_STATE_PAUSED) {
            if (ctx->streams[i].type == AVMEDIA_TYPE_AUDIO)
                ctx->streams[i].sync_pts += frame->nb_samples;
            else
                ctx->streams[i].sync_pts++;
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
    write(ctx->event_fd, &cnt, sizeof(cnt));
    return 0;

out:

    media_recorder_clear_queue(ctx, MEDIA_RECORDER_DATA_QUEUE_IDX);
    ctx->state = MEDIA_RECORDER_STATE_COMPLETED;
    media_recorder_event_cb(ctx, MEDIA_EVENT_COMPLETED,
        ret == AVERROR_EOF ? 0 : ret, NULL);
    return ret;
}

static void media_recorder_ctx_init(MediaRecorderContext* ctx)
{
    AVDictionaryEntry* tag;

    if ((tag = av_dict_get(ctx->global_opts, "cmdqmax", NULL, 0))) {
        ctx->cmd_max = strtol(tag->value, NULL, 0);
    } else {
        ctx->cmd_max = CONFIG_MEDIA_RECORDER_DATA_QUEUE_SIZE;
    }

    ctx->state = MEDIA_RECORDER_STATE_STOPPED;
    ctx->audio_idx = -1;
    ctx->video_idx = -1;
    ctx->exit = 0;
    ctx->aframe_cnt = 0;
    ctx->audio_input_state = 0;
    SIMPLEQ_INIT(&ctx->cmd_queue);
    media_parcel_init(&ctx->parcel);
    pthread_mutex_init(&ctx->mutex, NULL);
    ctx->event_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
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
    close(ctx->event_fd);
    ctx->event_fd = -1;
}

static int media_recorder_pause(MediaRecorderContext* ctx)
{
    int ret = 0;

    if (ctx->state != MEDIA_RECORDER_STATE_STARTED) {
        ret = AVERROR(EPERM);
        goto out;
    }

    if (ctx->audio_idx >= 0) {
        ret = audio_graph_pause(ctx->audio_input);
        if (ret < 0)
            goto out;
    }

    ctx->state = MEDIA_RECORDER_STATE_PAUSED;

out:
    media_recorder_event_cb(ctx, MEDIA_EVENT_PAUSED, ret, NULL);
    return ret;
}

static int media_recorder_stop(MediaRecorderContext* ctx)
{
    int ret;
    if (ctx->state == MEDIA_RECORDER_STATE_STOPPED)
        return 0;

    if (ctx->audio_idx >= 0)
        audio_graph_stop(ctx->audio_input);

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

static int media_recorder_start(MediaRecorderContext* ctx)
{
    int ret = AVERROR(EPERM);
    int i;

    if (ctx->state != MEDIA_RECORDER_STATE_PREPARED && ctx->state != MEDIA_RECORDER_STATE_PAUSED)
        goto out;

    if (ctx->state == MEDIA_RECORDER_STATE_PREPARED) {
        for (i = 0; i < ctx->nb_streams; i++) {
            ret = media_recorder_open_encoder(ctx, i);
            if (ret < 0)
                goto out;
        }

        ret = avformat_write_header(ctx->format_ctx, NULL);
        if (ret < 0)
            goto out;
    }

    if (ctx->audio_idx >= 0) {
        if (ctx->streams[ctx->audio_idx].enc_ctx->frame_size) {
            char buf[16] = { 0 };
            snprintf(buf, sizeof(buf), "%d", ctx->streams[ctx->audio_idx].enc_ctx->frame_size);
            ret = audio_graph_set_parameter(ctx->audio_input, "frame_size", buf);
            if (ret < 0) {
                MEDIA_ERR("ctx %p audio_graph_set_parameter failed.\n", ctx);
                goto out;
            }
        }
        if (ctx->state == MEDIA_RECORDER_STATE_PAUSED) {
            ret = audio_graph_resume(ctx->audio_input);
            if (ret < 0) {
                MEDIA_ERR("ctx %p audio_graph_resume failed, ret %d.\n", ctx, ret);
                goto out;
            } else
                MEDIA_INFO("ctx %p audio_graph_resume success.\n", ctx);
        } else {
            ret = audio_graph_start(ctx->audio_input,
                ctx->streams[ctx->audio_idx].enc_ctx->sample_fmt,
                ctx->streams[ctx->audio_idx].enc_ctx->sample_rate,
                ctx->streams[ctx->audio_idx].enc_ctx->ch_layout.nb_channels,
                media_recorder_on_event_cb, ctx);
            if (ret < 0) {
                MEDIA_ERR("ctx %p audio_graph_start failed, ret %d.\n", ctx, ret);
                goto out;
            } else
                MEDIA_INFO("ctx %p audio_graph_start success.\n", ctx);
        }
        ctx->audio_input_state = 1;
    }

    ctx->state = MEDIA_RECORDER_STATE_STARTED;

out:
    media_recorder_event_cb(ctx, MEDIA_EVENT_STARTED, ret, NULL);
    return ret;
}

static void media_recorder_close(MediaRecorderContext* ctx)
{
    media_recorder_release_stream(ctx);

    if (ctx->audio_input)
        audio_graph_close(&ctx->audio_input);

    if (ctx->tran_fd > 0)
        media_recorder_conn_close(ctx);
}

static int media_recorder_prepare(MediaRecorderContext* ctx, const char* filename)
{
    int ret = AVERROR(EPERM);
    AVDictionaryEntry* tag;
    char* format = NULL;

    if (ctx->state != MEDIA_RECORDER_STATE_STOPPED)
        goto out;

    if ((tag = av_dict_get(ctx->format_opt, "format", NULL, 0)))
        format = tag->value;

    ctx->format = av_guess_format(format, filename, NULL);
    if (!ctx->format) {
        MEDIA_ERR("ctx %p unknown format.\n", ctx);
        ret = AVERROR(EINVAL);
        goto out;
    }

    ret = media_recorder_open_muxer(ctx, filename);
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

    SIMPLEQ_FOREACH(tmp, &ctx->cmd_queue, entry)
    cnt++;
    if (cnt >= ctx->cmd_max && msg->cmd < MEDIA_RECORDER_CMD_STOP) {
        av_freep(&msg);
        return AVERROR(ENOMEM);
    }

    SIMPLEQ_INSERT_TAIL(&ctx->cmd_queue, msg, entry);

    return 0;
}

static const char* media_recorder_cmd_to_string(int cmd)
{
    switch (cmd) {
    case MEDIA_RECORDER_CMD_OPEN:
        return "OPEN";
    case MEDIA_RECORDER_CMD_SET_EVENT:
        return "SET_EVENT";
    case MEDIA_RECORDER_CMD_SET_OPTIONS:
        return "SET_OPTIONS";
    case MEDIA_RECORDER_CMD_PREPARE:
        return "PREPARE";
    case MEDIA_RECORDER_CMD_START:
        return "START";
    case MEDIA_RECORDER_CMD_PAUSE:
        return "PAUSE";
    case MEDIA_RECORDER_CMD_STOP:
        return "STOP";
    case MEDIA_RECORDER_CMD_RESET:
        return "RESET";
    case MEDIA_RECORDER_CMD_CLOSE:
        return "CLOSE";
    default:
        return "UNKNOWN";
    }
}

static void media_recorder_proc_cmd(MediaRecorderContext* ctx, RecorderCmd* msg)
{
    MEDIA_INFO("ctx %p name %s proc cmd %s (ID:%d)\n",
        ctx, ctx->name, media_recorder_cmd_to_string(msg->cmd), msg->cmd);

    switch (msg->cmd) {
    case MEDIA_RECORDER_CMD_SET_EVENT:
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
        media_recorder_stop(ctx);
        ctx->exit = 1;
        break;
    case MEDIA_RECORDER_CMD_STOP:
    case MEDIA_RECORDER_CMD_RESET:
        media_recorder_stop(ctx);
        break;
    default:
        break;
    }

    av_freep(&msg);
}

int media_recorder_process_cmd(MediaRecorderContext* ctx, const char* target,
    const char* cmd, const char* arg, char* res, int res_len)
{
    char url[PATH_MAX];
    int ret = 0;

    if (!ctx)
        return -EINVAL;

    MEDIA_INFO("ctx %p cmd: %s, arg %s, target %s.\n", ctx, cmd, arg ? arg : "NULL", target ? target : "NULL");

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
            MEDIA_INFO("ctx %p url: %s.\n", ctx, url);
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
        ret = av_dict_parse_string(&ctx->format_opt, arg, "=", ":", 0);
    } else if (!strcmp(cmd, "get_position")) {
        snprintf(res, res_len, "%" PRIu32, ctx->current_ms);
    } else {
        MEDIA_ERR("ctx %p unknown cmd: %s.\n", ctx, cmd);
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
        MEDIA_ERR("ctx %p unsupported id %d\n", ctx, (int)id);
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
        family = AF_RPMSG;
        rpmsg_addr.rp_family = AF_RPMSG;
        strlcpy(rpmsg_addr.rp_name, key, RPMSG_SOCKET_NAME_SIZE);
        strlcpy(rpmsg_addr.rp_cpu, cpu, RPMSG_SOCKET_CPU_SIZE);
        addr = (struct sockaddr*)&rpmsg_addr;
        len = sizeof(struct sockaddr_rpmsg);
    } else {
        family = PF_LOCAL;
        local_addr.sun_family = AF_LOCAL;
        strlcpy(local_addr.sun_path, key, UNIX_PATH_MAX);
        addr = (struct sockaddr*)&local_addr;
        len = sizeof(struct sockaddr_un);
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

static int media_recorder_poll_available(MediaRecorderContext* ctx, struct pollfd* fd)
{
    int ret = -EINVAL;
    uint32_t code;
    media_parcel ack;

    if (fd->revents & POLLERR)
        goto out;

    if (fd->fd == ctx->event_fd) {
        uint64_t cnt;
        ret = read(ctx->event_fd, &cnt, sizeof(cnt));
        if (ret < 0 && errno != EINTR && errno != EAGAIN) {
            MEDIA_ERR("ctx %p read event fd failed %d, exit recorder\n", ctx, -errno);
            ctx->exit = 1;
            return ret;
        }
    } else if (fd->fd == ctx->tran_fd) {
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
                    MEDIA_ERR("ctx %p create notify failed %d\n", ctx, ret);
                break;
            default:
                break;
            }

            media_parcel_reinit(&ctx->parcel);
            ctx->offset = 0;
        }
    }

    if (((fd->revents & POLLIN) && ret == -EPIPE) || (fd->revents & POLLHUP))
        goto out;

    return ret;

out:
    MEDIA_DEBUG("ctx %p fd:%d revent:%d\n", ctx, fd->fd, (int)fd->revents);
    media_recorder_conn_close(ctx);
    return 0;
}

static MediaRecorderContext* media_recorder_get_available_session(MediaRecorderPriv* priv)
{
    MediaRecorderContext* ctx = NULL;
    int i;

    for (i = 0; i < CONFIG_MEDIA_RECORDER_MAX_CNT; i++) {
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
    for (i = 0; i < CONFIG_MEDIA_RECORDER_MAX_CNT; i++) {
        ctx = &priv->ctxs[i];
        if (ctx->state == MEDIA_RECORDER_STATE_IDLE)
            continue;
        av_bprintf(&buf, "recorder[%d, %s] state:%d", i, ctx->name, ctx->state);
        if (ctx->audio_idx >= 0 && ctx->streams[ctx->audio_idx].enc_ctx)
            av_bprintf(&buf, ", a: %d %s %" PRId64 " %d %d %d %" PRIu32 "",
                ctx->audio_idx,
                avcodec_get_name(ctx->streams[ctx->audio_idx].enc_ctx->codec_id),
                ctx->streams[ctx->audio_idx].enc_ctx->bit_rate,
                ctx->streams[ctx->audio_idx].enc_ctx->sample_rate,
                ctx->streams[ctx->audio_idx].enc_ctx->ch_layout.nb_channels,
                media_recorder_queue_cnt(ctx, ctx->audio_idx), ctx->aframe_cnt);
        if (ctx->video_idx >= 0 && ctx->streams[ctx->video_idx].enc_ctx)
            av_bprintf(&buf, ", v: %d %s %d %d %d",
                ctx->video_idx,
                avcodec_get_name(ctx->streams[ctx->video_idx].enc_ctx->codec_id),
                ctx->streams[ctx->video_idx].enc_ctx->width,
                ctx->streams[ctx->video_idx].enc_ctx->height,
                media_recorder_queue_cnt(ctx, ctx->video_idx));
    }
    av_bprintf(&buf, "\n--------------recorder dump end---------------\n");
    MEDIA_INFO("%s\n", buf.str);
    av_bprint_finalize(&buf, NULL);
}

static void media_recorder_poll(MediaRecorderContext* ctx)
{
    struct pollfd fds[2];
    fds[0].fd = ctx->tran_fd;
    fds[0].events = POLLIN;
    fds[0].revents = 0;
    fds[1].fd = ctx->event_fd;
    fds[1].events = POLLIN;
    fds[1].revents = 0;
    int ret;

    ret = poll(fds, 2, -1);
    if (ret == -1) {
        MEDIA_ERR("ctx %p poll failed err=%d\n", ctx, -errno);
    } else if (ret == 0)
        MEDIA_DEBUG("ctx %p poll timeout\n", ctx);

    for (int i = 0; i < 2; i++) {
        if (!fds[i].revents)
            continue;

        ret = media_recorder_poll_available(ctx, &fds[i]);
        if (ret < 0 && ret != -EAGAIN && ret != -EPIPE)
            MEDIA_ERR("ctx %p poll_available failed %d\n", ctx, ret);
    }
}

static void* media_recorder_thread(void* arg)
{
    MediaRecorderContext* ctx = (MediaRecorderContext*)arg;
    RecorderCmd* msg;
    MEDIA_INFO("ctx %p create recorder thread.\n", ctx);

    while (1) {
        if (media_recorder_is_exit(ctx))
            break;

        media_recorder_poll(ctx);

        while ((msg = SIMPLEQ_FIRST(&ctx->cmd_queue)) != NULL) {
            SIMPLEQ_REMOVE_HEAD(&ctx->cmd_queue, entry);
            media_recorder_proc_cmd(ctx, msg);
        }

        if (media_recorder_dat_valid(ctx) && media_recorder_proc_dat(ctx) == AVERROR_EOF) {
            ctx->exit = 1;
        }
    }

    media_recorder_close(ctx);

    media_recorder_ctx_release(ctx);

    MEDIA_INFO("ctx %p %s recorder thread exit.\n", ctx, ctx->name);
    return NULL;
}

static int media_recorder_open(MediaRecorderContext* ctx, const char* name)
{
    struct sched_param param;
    pthread_attr_t attr;
    pthread_t thread;
    int ret = -EINVAL;

    if (ctx->state != MEDIA_RECORDER_STATE_IDLE || name == NULL)
        return ret;

    media_recorder_ctx_init(ctx);

    audio_graph_open(&ctx->audio_input, name);
    if (ctx->audio_input == NULL) {
        MEDIA_ERR("ctx %p open audio input failed\n", ctx);
        goto out;
    }

    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, CONFIG_MEDIA_RECORDER_STACKSIZE);
    param.sched_priority = CONFIG_MEDIA_RECORDER_PRIORITY;
    pthread_attr_setschedparam(&attr, &param);
    ret = pthread_create(&thread, &attr, media_recorder_thread, ctx);
    pthread_attr_destroy(&attr);
    if (ret != 0) {
        MEDIA_ERR("ctx %p create recorder thread failed %d\n", ctx, ret);
        goto out;
    }

    pthread_setname_np(thread, ctx->name);
    pthread_detach(thread);

    return 0;

out:
    audio_graph_close(&ctx->audio_input);
    ctx->state = MEDIA_RECORDER_STATE_IDLE;
    return ret;
}

static int media_recorder_handler(MediadPlugin* handle, struct media_server_conn* conn,
    const char* target, const char* cmd, const char* arg,
    int flags, char* res, int res_len)
{
    MediaRecorderPriv* priv = handle->priv;
    char option_name[64] = { 0 };
    char options[256] = { 0 };
    int ret;

    MEDIA_INFO("cmd: %s, arg %s, target %s.\n",
        cmd, arg ? arg : "NULL", target ? target : "NULL");

    if (!strcmp(cmd, "open")) {
        MediaRecorderContext* ctx = media_recorder_get_available_session(priv);
        if (!ctx) {
            MEDIA_ERR("recorder open failed...\n");
            return -ENOMEM;
        }

        ret = media_stub_get_stream_name(arg, ctx->name, sizeof(ctx->name));
        if (ret < 0) {
            MEDIA_ERR("get stream name failed %d\n", ret);
            return ret;
        }

        ctx->tran_fd = media_server_get_tran_fd(conn);
        if (ctx->tran_fd < 0) {
            MEDIA_ERR("recorder get tran fd failed...\n");
            return -EINVAL;
        }

        media_server_clean_conn(conn);

        // get global options in criteria.txt
        snprintf(option_name, sizeof(option_name), "%sParams", arg);
        ret = media_stub_get_stream_name(option_name, options, sizeof(options));
        if (ret == 0 && strlen(options) != 0) {
            ret = av_dict_parse_string(&ctx->global_opts, options, "=", ":", 0);
            if (ret < 0) {
                MEDIA_ERR("parse global options failed %d\n", ret);
                return ret;
            }
        }

        ret = media_recorder_open(ctx, arg);
        if (ret < 0)
            return ret;

        MEDIA_INFO("ctx %p open recorder success...\n", ctx);
    } else if (!strcmp(cmd, "dump")) {
        media_recorder_dump(priv);
    }

    return 0;
}

MediadPlugin media_recorder_plugin = {
    .name = "media_recorder",
    .priv_size = sizeof(struct MediaRecorderPriv),
    .priv = NULL,
    .init = NULL,
    .get = NULL,
    .available = NULL,
    .run_once = NULL,
    .uninit = NULL,
    .process_command = media_recorder_handler,
};
