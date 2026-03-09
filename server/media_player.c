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
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/sysinfo.h>
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
#include "libavutil/time.h"

#include "audio_graph.h"
#include "media_common.h"
#include "media_plugin.h"
#include "media_server.h"
#include "media_video_output.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MEDIA_PLAYER_CMD_QUEUE_IDX (1 << 0)
#define MEDIA_PLAYER_DATA_QUEUE_IDX (1 << 1)

#define MEDIA_AUDIO_OUTPUT_STARTING (1 << 0)
#define MEDIA_AUDIO_OUTPUT_STARTED (1 << 1)
#define MEDIA_AUDIO_OUTPUT_XRUN (1 << 2)

#define STREAMS_MAX 2

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
    SIMPLEQ_ENTRY(PlayerCmd)
    entry;
    int cmd;
    char data[0];
} PlayerCmd;

SIMPLEQ_HEAD(PlayerCmdQueue, PlayerCmd);

typedef struct OutputStream {
    int index;
    int nb_queue_max;
    int64_t next_pts;
    enum AVMediaType type;
    AVCodecContext* codec_ctx;
    FFFrameQueue queue;
    AVRational time_base;
    AVRational frame_rate;
} OutputStream;

typedef struct MediaPlayerContext MediaPlayerContext;

typedef struct MediaPlayerPoll {
    const char* name;
    int (*get_pollfds)(MediaPlayerContext* ctx, struct pollfd* fds, int count);
    int (*poll_available)(MediaPlayerContext* ctx, struct pollfd* fds);
} MediaPlayerPoll;

typedef struct MediaPlayerContext {
    /* communication with media client */
    int tran_fd;
    int notify_fd;
    int event_fd;
    int poll_timeout; /** < timeout for poll, default is -1, set for fbdev in global_opt */
    uint32_t offset;
    media_parcel parcel;

    int event;
    int cmd_max;
    int state;
    int exit;
    int loop_count;
    int offload;
    int pending_stop;
    int live_stream; /** < default is false, when set to true, avsync is disabled */
    uint32_t aframe_cnt;
    uint32_t vframe_cnt;
    uint32_t current_ms; /** < current timestamp of the decoded frame */
    uint32_t duration_ms; /** < duration of whole stream */
    char name[64];
    pthread_mutex_t mutex;
    char* protocol_map;
    struct PlayerCmdQueue cmd_queue;
    float volume;
    int interrupt;

    AVDictionary* format_opt;
    AVDictionary* global_opts;
    AVFormatContext* format_ctx;
    OutputStream* streams[STREAMS_MAX]; /**< array of all streams, one per output */
    OutputStream* audio_stream;
    OutputStream* video_stream;

    /* poll event */
    int poll_cnt;
    int idx[CONFIG_MEDIA_PLAYER_MAX_POLLFDS];
    struct pollfd fds[CONFIG_MEDIA_PLAYER_MAX_POLLFDS];
    MediaPlayerPoll poll[CONFIG_MEDIA_PLAYER_MAX_POLLFDS];

    /* avsync parameters */
    int frame_duration; /** < frame duration in ms */
    int max_latency; /** < max latency in ms */
    int64_t ts_base; /** < pts base for avsync */
    int64_t lat_base; /** < audio latency base for avsync */
    enum MediaPlayerSyncMode sync_mode;

    /* audio or video output */
    int audio_output_state;
    AVFilterContext* audio_output;
    MediaVOutputContext* video_output;
} MediaPlayerContext;

typedef struct MediaPlayerPriv {
    MediaPlayerContext ctxs[CONFIG_MEDIA_PLAYER_MAX_CNT];
} MediaPlayerPriv;

/****************************************************************************
 * Function declaration
 ****************************************************************************/
static int media_player_seek(MediaPlayerContext* ctx, uint32_t ms, int flush);
static int media_player_start(MediaPlayerContext* ctx);
static int media_player_stop(MediaPlayerContext* ctx);
static void media_player_poll(MediaPlayerContext* ctx);
static AVFrame* media_player_queue_pop(MediaPlayerContext* ctx, int type);
static int media_player_queue_cnt(MediaPlayerContext* ctx, int type);
static int media_player_get_pollfd(MediaPlayerContext* ctx, struct pollfd* fds, int count);
static int media_player_poll_available(MediaPlayerContext* ctx, struct pollfd* fds);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/**
 *  Ensure all audio data is finished when the work thread exit,
 *  we need to check if the audio_output_state.
 */
static inline int media_player_is_exit(MediaPlayerContext* ctx)
{
    int is_exit;
    pthread_mutex_lock(&ctx->mutex);
    is_exit = ctx->exit && (!ctx->audio_output_state || !ctx->audio_stream);
    pthread_mutex_unlock(&ctx->mutex);
    return is_exit;
}

static int media_player_is_queue_empty(MediaPlayerContext* ctx)
{
    return media_player_queue_cnt(ctx, AVMEDIA_TYPE_AUDIO) == 0 && media_player_queue_cnt(ctx, AVMEDIA_TYPE_VIDEO) == 0;
}

static void media_player_set_avsync_mode(MediaPlayerContext* ctx)
{
    AVDictionaryEntry* tag;

    if (!ctx->video_stream)
        return;

    if (ctx->audio_stream)
        ctx->sync_mode = MEDIA_PLAYER_SYNC_MODE_AUDIO;
    else
        ctx->sync_mode = MEDIA_PLAYER_SYNC_MODE_SYSTEM;

    if ((tag = av_dict_get(ctx->format_opt, "live_stream", NULL, 0))) {
        ctx->live_stream = strtol(tag->value, NULL, 0);
        if (ctx->live_stream)
            ctx->sync_mode = MEDIA_PLAYER_SYNC_MODE_BYPASS;
    }
}

static void media_player_video_get_poll_timeout(MediaPlayerContext* ctx)
{
    struct pollfd fds[CONFIG_MEDIA_PLAYER_MAX_POLLFDS];
    int count = CONFIG_MEDIA_PLAYER_MAX_POLLFDS;
    int ret = media_video_output_get_pollfd(ctx->video_output, fds, count);
    int frame_ms = ctx->frame_duration / 1000;
    if (ret <= 0) {
        if (frame_ms <= 0 || frame_ms > 50) // assume 20 as min fps
            ctx->poll_timeout = 2;
        else
            ctx->poll_timeout = frame_ms;
    }
}

static int media_player_output_get_pollfds(MediaPlayerContext* ctx, struct pollfd* fds, int count)
{
    if (ctx->state >= MEDIA_PLAYER_STATE_STOPPED)
        return 0;

    return media_video_output_get_pollfd(ctx->video_output, fds, count);
}

static int media_player_output_poll_available(MediaPlayerContext* ctx, struct pollfd* fds)
{
    return media_video_output_poll_available(ctx->video_output, fds);
}

static void media_player_poll_add(MediaPlayerContext* ctx, const char* name,
    int (*get_pollfds)(MediaPlayerContext* ctx, struct pollfd* fds, int count),
    int (*poll_available)(MediaPlayerContext* ctx, struct pollfd* fds))
{
    ctx->poll[ctx->poll_cnt].name = name;
    ctx->poll[ctx->poll_cnt].get_pollfds = get_pollfds;
    ctx->poll[ctx->poll_cnt].poll_available = poll_available;
    ctx->poll_cnt++;
}

static int media_player_on_event_cb(void* udata, int evt, int64_t args)
{
    MediaPlayerContext* ctx = (MediaPlayerContext*)udata;
    AVFrame* out_frame = (AVFrame*)(uintptr_t)args;
    AVFrame* frame;
    uint64_t cnt = 1;

    MEDIA_DEBUG("audio audio_output event: %d", evt);

    if (evt < 0) {
        MEDIA_INFO("ctx %p received unlink event form audio_output.", ctx);
        pthread_mutex_lock(&ctx->mutex);
        ctx->audio_output_state = 0;
        write(ctx->event_fd, &cnt, sizeof(cnt));
        pthread_mutex_unlock(&ctx->mutex);
        return 0;
    }

    frame = media_player_queue_pop(ctx, AVMEDIA_TYPE_AUDIO);
    if (!frame) {
        pthread_mutex_lock(&ctx->mutex);
        if (ctx->audio_output_state & MEDIA_AUDIO_OUTPUT_XRUN) {
            pthread_mutex_unlock(&ctx->mutex);
            return AVERROR(EAGAIN);
        }

        ctx->audio_output_state |= MEDIA_AUDIO_OUTPUT_XRUN;

        if (ctx->audio_output) {
            MEDIA_INFO("player %s xrun, pause audio output.", ctx->name);
            audio_graph_pause(ctx->audio_output);
        }

        pthread_mutex_unlock(&ctx->mutex);

        return AVERROR(EAGAIN);
    }

    av_frame_move_ref(out_frame, frame);
    av_frame_free(&frame);

    write(ctx->event_fd, &cnt, sizeof(cnt));

    return 0;
}

static inline int media_player_stream_inactive(MediaPlayerContext* ctx, int idx)
{
    if (idx == AVMEDIA_TYPE_AUDIO)
        return ctx->audio_stream == NULL || ctx->audio_stream->codec_ctx == NULL;
    else if (idx == AVMEDIA_TYPE_VIDEO)
        return ctx->video_stream == NULL || ctx->video_stream->codec_ctx == NULL;
    else
        return true; // other stream types are not supported
}

static int media_player_loop(MediaPlayerContext* ctx)
{
    int ret = AVERROR_EOF;

    if (ctx->loop_count) {
        ret = media_player_seek(ctx, 0, true);
        ctx->loop_count -= ctx->loop_count > 0;
    }

    return ret;
}

static int media_player_queue_cnt(MediaPlayerContext* ctx, int type)
{
    OutputStream* stream = NULL;
    int count = 0;

    stream = type == AVMEDIA_TYPE_AUDIO ? ctx->audio_stream : ctx->video_stream;

    if (!stream)
        return 0;

    pthread_mutex_lock(&ctx->mutex);
    count = ff_framequeue_queued_frames(&stream->queue);
    pthread_mutex_unlock(&ctx->mutex);

    return count;
}

static inline int media_player_dat_available(MediaPlayerContext* ctx)
{
    int ret;

    if (ctx->state >= MEDIA_PLAYER_STATE_STOPPED)
        return 0;

    /* As long as one data queue less than nb_queue_max, continue read */
    ret = ((ctx->audio_stream && (media_player_queue_cnt(ctx, AVMEDIA_TYPE_AUDIO) < ctx->audio_stream->nb_queue_max))
        || (ctx->video_stream && (media_player_queue_cnt(ctx, AVMEDIA_TYPE_VIDEO) < ctx->video_stream->nb_queue_max)));
    return ret;
}

static AVFrame* media_player_queue_pop(MediaPlayerContext* ctx, int type)
{
    AVFrame* frame = NULL;
    OutputStream* stream = NULL;

    stream = type == AVMEDIA_TYPE_AUDIO ? ctx->audio_stream : ctx->video_stream;

    if (!stream)
        return NULL;

    pthread_mutex_lock(&ctx->mutex);
    if (ff_framequeue_queued_frames(&stream->queue)) {
        frame = ff_framequeue_take(&stream->queue);
    }
    pthread_mutex_unlock(&ctx->mutex);
    return frame;
}

static AVFrame* media_player_queue_peek(MediaPlayerContext* ctx, int type)
{
    AVFrame* frame = NULL;
    OutputStream* stream = NULL;

    stream = type == AVMEDIA_TYPE_AUDIO ? ctx->audio_stream : ctx->video_stream;

    if (!stream)
        return NULL;

    pthread_mutex_lock(&ctx->mutex);
    if (ff_framequeue_queued_frames(&stream->queue)) {
        frame = ff_framequeue_peek(&stream->queue, 0);
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
        for (i = 0; i < STREAMS_MAX; i++) {
            if (media_player_stream_inactive(ctx, i))
                continue;

            if (!ctx->offload) {
                ret = avcodec_send_packet(ctx->streams[i]->codec_ctx, NULL);
                if (ret < 0 && ret != AVERROR_EOF)
                    return ret;
            }
        }
    }

    if (ret < 0)
        return ret;

    /* send the packet to its decoder, if any */
    for (i = 0; i < STREAMS_MAX; i++) {
        if (!media_player_stream_inactive(ctx, i) && pkt.stream_index == ctx->streams[i]->index) {
            if (!ctx->offload)
                ret = avcodec_send_packet(ctx->streams[i]->codec_ctx, &pkt);
            else // todo
                MEDIA_ERR("don't support offload play\n");
            break;
        }
    }

    av_packet_unref(&pkt);
    return ret == AVERROR_INVALIDDATA ? 0 : ret;
}

static void media_player_notify_event(MediaPlayerContext* ctx, int event, int result, const char* extra)
{
    media_parcel notify;

    media_parcel_init(&notify);
    media_parcel_append_printf(&notify, "%i%i%s", event, result, extra);
    if (ctx->notify_fd > 0)
        media_parcel_send(&notify, ctx->notify_fd, MEDIA_PARCEL_SEND, MSG_DONTWAIT);

    media_parcel_deinit(&notify);
    return;
}

static void media_player_event_cb(MediaPlayerContext* ctx, int event, int result, const char* extra)
{
    if (ctx->event)
        media_player_notify_event(ctx, event, result, extra);
}

static int media_player_start_audio(MediaPlayerContext* ctx)
{
    AVCodecContext* codec_ctx;
    int ret = 0;

    if (!ctx->audio_output) {
        ret = AVERROR(EINVAL);
        goto out;
    }

    if (!ctx->audio_stream || !ctx->audio_stream->codec_ctx) {
        ret = AVERROR(EINVAL);
        goto out;
    }
    codec_ctx = ctx->audio_stream->codec_ctx;

    ret = audio_graph_start(ctx->audio_output,
        codec_ctx->sample_fmt,
        codec_ctx->sample_rate,
        codec_ctx->ch_layout.nb_channels,
        media_player_on_event_cb,
        ctx);
    if (ret < 0)
        MEDIA_ERR("Failed to start audio graph: %s\n", av_err2str(ret));

out:
    return ret;
}

static int media_player_resume_audio(MediaPlayerContext* ctx, bool xrun)
{
    int ret = 0;

    if (!ctx->audio_output)
        return AVERROR(EINVAL);

    ret = audio_graph_resume(ctx->audio_output);
    if (ret < 0)
        MEDIA_ERR("Failed to resume audio graph: %s\n", av_err2str(ret));

    if (!xrun)
        ctx->state = MEDIA_PLAYER_STATE_STARTED;

    return ret;
}

static int media_player_queue_push(MediaPlayerContext* ctx, int idx, AVFrame* frame)
{
    int queued_cnt;
    int ret = 0;

    if (ctx->streams[idx]->type == AVMEDIA_TYPE_AUDIO)
        ctx->aframe_cnt++;
    else
        ctx->vframe_cnt++;

    pthread_mutex_lock(&ctx->mutex);

    ret = ff_framequeue_add(&ctx->streams[idx]->queue, frame);
    if (ret < 0) {
        pthread_mutex_unlock(&ctx->mutex);
        MEDIA_ERR("Failed to add frame to queue: %s\n", av_err2str(ret));
        return ret;
    }

    queued_cnt = ff_framequeue_queued_frames(&ctx->streams[idx]->queue);

    if (queued_cnt == ctx->streams[idx]->nb_queue_max) {
        if (ctx->audio_output_state & MEDIA_AUDIO_OUTPUT_STARTING) {
            ctx->audio_output_state &= ~MEDIA_AUDIO_OUTPUT_STARTING;
            ret = media_player_start_audio(ctx);
        } else if (ctx->audio_output_state & MEDIA_AUDIO_OUTPUT_XRUN) {
            ctx->audio_output_state &= ~MEDIA_AUDIO_OUTPUT_XRUN;
            ret = media_player_resume_audio(ctx, true);
        }

        if (ret >= 0)
            ctx->audio_output_state |= MEDIA_AUDIO_OUTPUT_STARTED;
    }

    pthread_mutex_unlock(&ctx->mutex);
    if (ret < 0)
        MEDIA_ERR("Failed to start/resume audio output: %s\n", av_err2str(ret));

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
        for (i = 0; i < STREAMS_MAX; i++) {
            while (ctx->streams[i] && ff_framequeue_queued_frames(&ctx->streams[i]->queue)) {
                frame = ff_framequeue_take(&ctx->streams[i]->queue);
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

    ret = avcodec_receive_frame(ctx->streams[idx]->codec_ctx, frame);
    if (ret < 0) {
        av_frame_free(&frame);
        return ret;
    }

    if (ctx->streams[idx]->type == AVMEDIA_TYPE_AUDIO) {
        if (frame->pts == AV_NOPTS_VALUE && ctx->streams[idx]->next_pts != AV_NOPTS_VALUE)
            frame->pts = ctx->streams[idx]->next_pts;

        if (frame->pts != AV_NOPTS_VALUE)
            ctx->streams[idx]->next_pts = frame->pts + frame->nb_samples;
    }

    frame->time_base = ctx->streams[idx]->time_base;
    ctx->current_ms = frame->pts * av_q2d(ctx->streams[idx]->time_base) * 1000;

    *oframe = frame;
    return 0;
}

static int media_player_dec_frames(MediaPlayerContext* ctx)
{
    int got_frame = 0;
    AVFrame* frame;
    int ret = 0, i;

    for (i = 0; i < STREAMS_MAX; i++) {
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
    struct pollfd fds[1];
    struct pollfd* fd = &fds[0];
    uint64_t cnt = 1;
    int pending_stop = 0;
    fds[0].fd = ctx->tran_fd;
    fds[0].events = POLLIN;
    fds[0].revents = 0;

    media_player_poll_available(ctx, fd);

    SIMPLEQ_FOREACH(msg, &ctx->cmd_queue, entry)
    {
        if (msg->cmd == MEDIA_PLAYER_CMD_CLOSE)
            pending_stop = strtoul(msg->data, NULL, 0);
        if (msg->cmd >= MEDIA_PLAYER_CMD_STOP)
            interrupt = 1;
    }
    if (pending_stop)
        interrupt = 0;
    write(ctx->event_fd, &cnt, sizeof(cnt));
    if (interrupt)
        ctx->interrupt = interrupt;
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
        } else {
            MEDIA_WARN("Protocol '%s' not found in map.\n", proto);
        }

        av_dict_free(&opts);
    } else {
        MEDIA_WARN("No protocol_map set or invalid proto.\n");
    }

    av_strlcpy(dst, url, length);
}

static int media_player_open_decoder(MediaPlayerContext* ctx, OutputStream* stream, AVCodecParameters* codecpar)
{
    AVDictionaryEntry* tag = NULL;
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

    if ((tag = av_dict_get(ctx->format_opt, "request_sample_fmt", NULL, 0)))
        stream->codec_ctx->request_sample_fmt = av_get_sample_fmt(tag->value);

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

static void media_player_release_stream(MediaPlayerContext* ctx)
{
    if (ctx->audio_stream) {
        ff_framequeue_free(&ctx->audio_stream->queue);
        memset(ctx->audio_stream, 0, sizeof(OutputStream));
        av_freep(&ctx->audio_stream);
        ctx->streams[AVMEDIA_TYPE_AUDIO] = NULL;
    }

    if (ctx->video_stream) {
        ff_framequeue_free(&ctx->video_stream->queue);
        memset(ctx->video_stream, 0, sizeof(OutputStream));
        av_freep(&ctx->video_stream);
        ctx->streams[AVMEDIA_TYPE_VIDEO] = NULL;
    }
}

static int media_player_init_stream(MediaPlayerContext* ctx)
{
    int i, ret = 0;

    if (!ctx->format_ctx || ctx->format_ctx->nb_streams <= 0)
        return -EINVAL;

    media_player_release_stream(ctx);

    for (i = 0; i < ctx->format_ctx->nb_streams; i++) {
        OutputStream* stream_out = NULL;
        AVStream* stream = NULL;
        AVDictionaryEntry* tag;

        stream = ctx->format_ctx->streams[i];

        if (stream->disposition & AV_DISPOSITION_ATTACHED_PIC || (stream->codecpar->codec_type != AVMEDIA_TYPE_AUDIO && stream->codecpar->codec_type != AVMEDIA_TYPE_VIDEO)) {
            MEDIA_INFO("Skip stream %d, type %d, disposition %d.\n", i, stream->codecpar->codec_type, stream->disposition);
            continue;
        } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && !strncmp(ctx->name, "Video", 5)) {
            av_log(NULL, AV_LOG_ERROR, "%s:%d stream %d, type %d.\n", __func__, __LINE__, i, stream->codecpar->codec_type);
            ctx->streams[AVMEDIA_TYPE_VIDEO] = stream_out = ctx->video_stream = av_calloc(1, sizeof(OutputStream));
            stream_out->type = AVMEDIA_TYPE_VIDEO;
        } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            av_log(NULL, AV_LOG_ERROR, "%s:%d stream %d, type %d.\n", __func__, __LINE__, i, stream->codecpar->codec_type);
            ctx->streams[AVMEDIA_TYPE_AUDIO] = stream_out = ctx->audio_stream = av_calloc(1, sizeof(OutputStream));
            stream_out->type = AVMEDIA_TYPE_AUDIO;
        }

        if (!stream_out)
            continue;

        stream_out->type = stream->codecpar->codec_type;
        stream_out->index = i;

        if ((tag = av_dict_get(ctx->format_opt, "datqmax", NULL, 0))) {
            stream_out->nb_queue_max = strtol(tag->value, NULL, 0);
        } else {
            stream_out->nb_queue_max = CONFIG_MEDIA_PLAYER_DATA_QUEUE_SIZE;
        }

        ff_framequeue_init(&stream_out->queue, NULL);

        /* Use specify ch_layout if possible, follow guess_input_channel_layout() in ffmpeg.c */
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && stream->codecpar->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
            av_channel_layout_default(&stream->codecpar->ch_layout,
                stream->codecpar->ch_layout.nb_channels);

        ret = media_player_open_decoder(ctx, stream_out, stream->codecpar);
        if (ret < 0)
            goto out;

        stream->discard = AVDISCARD_DEFAULT;
        if (stream_out->codec_ctx->codec_type == AVMEDIA_TYPE_AUDIO && !av_channel_layout_check(&stream_out->codec_ctx->ch_layout)) {
            ret = av_channel_layout_copy(&stream_out->codec_ctx->ch_layout,
                &stream->codecpar->ch_layout);
            if (ret < 0)
                goto out;
        }

        stream_out->time_base = stream->time_base;
        stream_out->frame_rate = stream->r_frame_rate;
        stream_out->next_pts = AV_NOPTS_VALUE;
        stream_out->codec_ctx->pkt_timebase = stream->time_base;

        if (stream_out->type == AVMEDIA_TYPE_VIDEO) {
            ret = media_video_output_open(&ctx->video_output, ctx->format_opt);
            if (ret < 0) {
                MEDIA_ERR("Failed to open video_output\n");
                goto out;
            }

            ctx->frame_duration = av_rescale(AV_TIME_BASE, stream_out->frame_rate.den,
                stream_out->frame_rate.num);
            ctx->max_latency = ctx->frame_duration;
            media_player_video_get_poll_timeout(ctx);

            media_player_poll_add(ctx, "video_output",
                media_player_output_get_pollfds,
                media_player_output_poll_available);
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
        for (i = 0; i < STREAMS_MAX; i++) {
            if (media_player_stream_inactive(ctx, i))
                continue;

            stream = ctx->streams[i];
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
    uint32_t seek_point = 0;
    AVDictionaryEntry* tag;
    char* name;
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
    if (!ctx->format_ctx) {
        av_freep(&name);
        return AVERROR(ENOMEM);
    }

    ctx->format_ctx->interrupt_callback.callback = media_player_interrupt;
    ctx->format_ctx->interrupt_callback.opaque = ctx;
    ctx->format_ctx->flags |= AVFMT_FLAG_FAST_SEEK;

    if (ctx->global_opts)
        av_dict_copy(&ctx->format_opt, ctx->global_opts, 0);
    ctx->protocol_map = NULL;
    if ((tag = av_dict_get(ctx->format_opt, "protocol_map", NULL, 0))) {
        ctx->protocol_map = tag->value;
    } else {
        MEDIA_WARN("protocol_map NOT found in options!\n");
    }
    media_player_map_protocol(ctx, filename, name, MAX_URL_SIZE);

    MEDIA_INFO("ctx %p url %s start open input.\n", ctx, name);
    ret = avformat_open_input(&ctx->format_ctx, name, iformat, &ctx->format_opt);
    if (ret < 0) {
        MEDIA_ERR("Failed to avformat_open_input ret %d, %s.\n", ret, av_err2str(ret));
        goto out;
    }

    MEDIA_INFO("ctx %p open input done.\n", ctx);

    ret = avformat_find_stream_info(ctx->format_ctx, NULL);
    if (ret < 0) {
        MEDIA_ERR("Failed to find stream info ret %d, %s.\n", ret, av_err2str(ret));
        goto out;
    }

    MEDIA_INFO("ctx %p find stream info done.\n", ctx);

    ret = media_player_init_stream(ctx);
    if (ret < 0) {
        MEDIA_ERR("ctx %p failed to init movie stream, ret %d, %s.\n", ctx, ret, av_err2str(ret));
        goto out;
    }

    MEDIA_INFO("ctx %p open decoder done.\n", ctx);

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
        return ret;
    else if (ret == AVERROR_EOF) {
        if (!media_player_is_queue_empty(ctx)) {

            pthread_mutex_lock(&ctx->mutex);
            if (ctx->audio_output_state & MEDIA_AUDIO_OUTPUT_STARTING) {
                ctx->audio_output_state &= ~MEDIA_AUDIO_OUTPUT_STARTING;
                ret = media_player_start_audio(ctx);
            } else if (ctx->audio_output_state & MEDIA_AUDIO_OUTPUT_XRUN) {
                ctx->audio_output_state &= ~MEDIA_AUDIO_OUTPUT_XRUN;
                ret = media_player_resume_audio(ctx, true);
            }

            if (ret >= 0)
                ctx->audio_output_state |= MEDIA_AUDIO_OUTPUT_STARTED;

            pthread_mutex_unlock(&ctx->mutex);

            if (ret < 0) {
                MEDIA_ERR("audio play/resume failed: %s\n", av_err2str(ret));
                return ret;
            }
        }
    }

    if (media_player_is_queue_empty(ctx)) {
        if (ret == AVERROR_EOF)
            ret = 0;
        ctx->state = MEDIA_PLAYER_STATE_COMPLETED;
        media_player_event_cb(ctx, MEDIA_EVENT_COMPLETED, ret, NULL);
    }

    if (!ctx->pending_stop)
        return ret;

    return media_player_stop(ctx);
}

static int media_player_seek(MediaPlayerContext* ctx, uint32_t ms, int flush)
{
    int64_t timestamp = ms * 1000LL;
    int i, ret = AVERROR(EPERM);
    int64_t max_ts;

    if (!ctx->format_ctx)
        goto end;

    max_ts = ctx->format_ctx->duration;

    if (max_ts != AV_NOPTS_VALUE && max_ts <= 0) {
        MEDIA_ERR("Cannot seek in empty file (max_ts %" PRId64 ")\n", max_ts);
        ret = AVERROR(EINVAL);
        goto end;
    }

    if (max_ts != AV_NOPTS_VALUE && timestamp > max_ts) {
        MEDIA_WARN("Seek position %" PRId64 " exceeds max_ts %" PRId64 ", seeking to end\n",
            timestamp, max_ts);
        timestamp = max_ts;
    }

    if (ctx->format_ctx->start_time != AV_NOPTS_VALUE) {
        timestamp += ctx->format_ctx->start_time;
        max_ts += ctx->format_ctx->start_time;
    }

    if (flush)
        media_player_clear_queue(ctx, MEDIA_PLAYER_DATA_QUEUE_IDX);

    ret = avformat_seek_file(ctx->format_ctx, -1, INT64_MIN, timestamp, max_ts, AVSEEK_FLAG_ANY);
    if (ret < 0)
        goto end;

    for (i = 0; i < STREAMS_MAX; i++) {
        if (media_player_stream_inactive(ctx, i))
            continue;

        avcodec_flush_buffers(ctx->streams[i]->codec_ctx);
    }

    ctx->current_ms = ms;
    ctx->ts_base = AV_NOPTS_VALUE;

    /* Auto-start playback if we were in completed state and seek was successful */
    if ((ctx->state == MEDIA_PLAYER_STATE_COMPLETED) && ret >= 0)
        media_player_start(ctx);

end:

    media_player_event_cb(ctx, MEDIA_EVENT_SEEKED, ret, NULL);
    return ret;
}

static void media_player_ctx_init(MediaPlayerContext* ctx)
{
    AVDictionaryEntry* tag;

    if ((tag = av_dict_get(ctx->global_opts, "cmdqmax", NULL, 0))) {
        ctx->cmd_max = strtol(tag->value, NULL, 0);
    } else {
        ctx->cmd_max = CONFIG_MEDIA_RECORDER_DATA_QUEUE_SIZE;
    }

    ctx->state = MEDIA_PLAYER_STATE_STOPPED;
    ctx->aframe_cnt = 0;
    ctx->vframe_cnt = 0;
    ctx->audio_stream = NULL;
    ctx->video_stream = NULL;
    ctx->streams[AVMEDIA_TYPE_AUDIO] = NULL;
    ctx->streams[AVMEDIA_TYPE_VIDEO] = NULL;
    ctx->sync_mode = MEDIA_PLAYER_SYNC_MODE_SYSTEM;
    ctx->ts_base = AV_NOPTS_VALUE;
    ctx->lat_base = AV_NOPTS_VALUE;
    ctx->volume = 1.0;
    ctx->interrupt = 0;
    SIMPLEQ_INIT(&ctx->cmd_queue);
    media_parcel_init(&ctx->parcel);
    pthread_mutex_init(&ctx->mutex, NULL);

    ctx->event_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ctx->poll_timeout = -1;

    ctx->poll_cnt = 0;
    media_player_poll_add(ctx, "media_player",
        media_player_get_pollfd,
        media_player_poll_available);
}

static void media_player_ctx_release(MediaPlayerContext* ctx)
{
    ctx->state = MEDIA_PLAYER_STATE_IDLE;
    ctx->exit = 0;
    ctx->loop_count = 0;
    ctx->offload = 0;
    ctx->pending_stop = 0;
    ctx->event = 0;
    av_freep(&ctx->audio_stream);
    av_freep(&ctx->video_stream);
    close(ctx->event_fd);
    ctx->event_fd = -1;
    media_player_notify_finalize(ctx);
    media_parcel_deinit(&ctx->parcel);
    pthread_mutex_destroy(&ctx->mutex);
}

static void media_player_conn_close(MediaPlayerContext* ctx)
{
    close(ctx->tran_fd);
    ctx->tran_fd = -EPERM;
    ctx->offset = 0;
    media_parcel_deinit(&ctx->parcel);
}

static void media_player_close(MediaPlayerContext* ctx)
{
    media_player_release_stream(ctx);

    if (ctx->audio_output)
        audio_graph_close(&ctx->audio_output);

    if (ctx->global_opts)
        av_dict_free(&ctx->global_opts);

    if (ctx->tran_fd > 0)
        media_player_conn_close(ctx);
}

static int media_player_pause(MediaPlayerContext* ctx)
{
    int ret = AVERROR(EPERM);

    if (ctx->state == MEDIA_PLAYER_STATE_STARTED) {
        ctx->state = MEDIA_PLAYER_STATE_PAUSED;
        ret = 0;
    }

    pthread_mutex_lock(&ctx->mutex);
    if (ctx->audio_output_state & MEDIA_AUDIO_OUTPUT_STARTING) {
        ctx->audio_output_state &= ~MEDIA_AUDIO_OUTPUT_STARTING;
        ctx->audio_output_state |= MEDIA_AUDIO_OUTPUT_STARTED;
    }
    pthread_mutex_unlock(&ctx->mutex);

    if (ctx->audio_output)
        audio_graph_pause(ctx->audio_output);

    media_player_event_cb(ctx, MEDIA_EVENT_PAUSED, ret, NULL);
    return 0;
}

static int media_player_stop(MediaPlayerContext* ctx)
{
    if (ctx->state == MEDIA_PLAYER_STATE_STOPPED)
        return 0;

    media_player_clear_queue(ctx, MEDIA_PLAYER_DATA_QUEUE_IDX);

    if (ctx->audio_output && ctx->audio_stream)
        audio_graph_stop(ctx->audio_output);

    if (ctx->video_output && ctx->video_stream)
        media_video_output_close(&ctx->video_output);

    media_player_close_demuxer(ctx);

    pthread_mutex_lock(&ctx->mutex);
    ctx->audio_output_state = 0;
    pthread_mutex_unlock(&ctx->mutex);

    ctx->pending_stop = 0;
    ctx->state = MEDIA_PLAYER_STATE_STOPPED;

    media_player_event_cb(ctx, MEDIA_EVENT_STOPPED, 0, NULL);
    return 0;
}

static int media_player_start(MediaPlayerContext* ctx)
{
    char volume_str[16] = { 0 };
    int ret = 0;

    if (!ctx->audio_output) {
        ret = AVERROR(EINVAL);
        goto err;
    }

    if (ctx->state != MEDIA_PLAYER_STATE_PREPARED
        && ctx->state != MEDIA_PLAYER_STATE_PAUSED
        && ctx->state != MEDIA_PLAYER_STATE_COMPLETED) {
        ret = AVERROR(EPERM);
        goto err;
    }

    media_player_set_avsync_mode(ctx);

    pthread_mutex_lock(&ctx->mutex);
    if (ctx->audio_stream
        && ff_framequeue_queued_frames(&ctx->audio_stream->queue)
            >= ctx->audio_stream->nb_queue_max) {
        if (ctx->state == MEDIA_PLAYER_STATE_PAUSED)
            ret = media_player_resume_audio(ctx, false);
        else
            ret = media_player_start_audio(ctx);

        if (ret >= 0)
            ctx->audio_output_state |= MEDIA_AUDIO_OUTPUT_STARTED;
    } else
        ctx->audio_output_state |= MEDIA_AUDIO_OUTPUT_STARTING;

    pthread_mutex_unlock(&ctx->mutex);

    if (ret < 0) {
        MEDIA_ERR("audio play/resume failed: %s\n", av_err2str(ret));
        goto err;
    }

    snprintf(volume_str, sizeof(volume_str), "%f", ctx->volume);
    ret = audio_graph_set_parameter(ctx->audio_output, "player_volume", volume_str);
    if (ret < 0)
        MEDIA_ERR("audio_graph_set_parameter failed.\n");

    ctx->state = MEDIA_PLAYER_STATE_STARTED;
    ctx->ts_base = AV_NOPTS_VALUE;
err:
    media_player_event_cb(ctx, MEDIA_EVENT_STARTED, ret, NULL);
    return ret;
}

static int media_player_prepare(MediaPlayerContext* ctx, const char* filename)
{
    int ret = AVERROR(EPERM);

    if (ctx->state != MEDIA_PLAYER_STATE_STOPPED)
        goto out;

    ctx->interrupt = 0;

    ret = media_player_open_demuxer(ctx, filename);
    if (ret < 0) {
        MEDIA_ERR("media_player_open_demuxer failed %d.\n", ret);
        goto out;
    }
    ctx->state = MEDIA_PLAYER_STATE_PREPARED;

out:
    if (ret < 0 && ctx->interrupt)
        ret = AVERROR(ECANCELED);

    media_player_event_cb(ctx, MEDIA_EVENT_PREPARED, ret, NULL);
    return 0;
}

static int media_player_get_latency(MediaPlayerContext* ctx, char* res, int res_len)
{
    int64_t audio_latency = 0;
    int i, nb_frames, ret;
    int64_t latency = 0;

    if (!ctx->audio_stream)
        return AVERROR(EINVAL);

    pthread_mutex_lock(&ctx->mutex);
    nb_frames = ff_framequeue_queued_frames(&ctx->audio_stream->queue);
    for (i = 0; i < nb_frames; i++) {
        AVFrame* frame = ff_framequeue_peek(&ctx->audio_stream->queue, i);
        if (frame) {
            latency += av_rescale_q(frame->duration, ctx->audio_stream->time_base, AV_TIME_BASE_Q);
        }
    }
    pthread_mutex_unlock(&ctx->mutex);

    ret = audio_graph_get_parameter(ctx->audio_output, "latency", res, res_len);
    if (ret == 0) {
        sscanf(res, "latency:%" PRId64, &audio_latency);
        latency += audio_latency;
    }

    snprintf(res, res_len, "%" PRId64, latency);

    return ret;
}

static int media_player_volume(MediaPlayerContext* ctx, const char* args, char* res, int res_len)
{
    int ret = -EINVAL;

    if (ctx->audio_output) {
        if (args) {
            ret = audio_graph_set_parameter(ctx->audio_output, "player_volume", args);
            sscanf(args, "%f", &ctx->volume);
        } else if (res && res_len) {
            ret = audio_graph_get_parameter(ctx->audio_output, "player_volume", res, res_len);
            sscanf(res, "vol:%f", &ctx->volume);
        }
        if (ret < 0)
            MEDIA_ERR("ctx %p media_player_volume failed.\n", ctx);
    } else {
        MEDIA_INFO("ctx %p audio_output is NULL.\n", ctx);
        if (args) {
            sscanf(args, "%f", &ctx->volume);
            ret = 0;
        } else if (res && res_len) {
            snprintf(res, res_len, "vol:%f", ctx->volume);
            ret = 0;
        }
    }

    return ret;
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

    SIMPLEQ_FOREACH(tmp, &ctx->cmd_queue, entry)
    cnt++;
    if (cnt >= ctx->cmd_max && msg->cmd < MEDIA_PLAYER_CMD_STOP) {
        av_freep(&msg);

        return AVERROR(ENOMEM);
    }

    SIMPLEQ_INSERT_TAIL(&ctx->cmd_queue, msg, entry);

    return 0;
}

static const char* media_player_cmd_to_string(int cmd)
{
    switch (cmd) {
    case MEDIA_PLAYER_CMD_OPEN:
        return "OPEN";
    case MEDIA_PLAYER_CMD_SET_EVENT:
        return "SET_EVENT";
    case MEDIA_PLAYER_CMD_SET_OPTIONS:
        return "SET_OPTIONS";
    case MEDIA_PLAYER_CMD_SET_LOOP:
        return "SET_LOOP";
    case MEDIA_PLAYER_CMD_PREPARE:
        return "PREPARE";
    case MEDIA_PLAYER_CMD_START:
        return "START";
    case MEDIA_PLAYER_CMD_PAUSE:
        return "PAUSE";
    case MEDIA_PLAYER_CMD_SEEK:
        return "SEEK";
    case MEDIA_PLAYER_CMD_STOP:
        return "STOP";
    case MEDIA_PLAYER_CMD_RESET:
        return "RESET";
    case MEDIA_PLAYER_CMD_CLOSE:
        return "CLOSE";
    default:
        return "UNKNOWN";
    }
}

static void media_player_proc_cmd(MediaPlayerContext* ctx, PlayerCmd* msg)
{
    uint32_t time, pending_stop;

    MEDIA_INFO("ctx %p name %s proc cmd %s (ID:%d)\n",
        ctx, ctx->name, media_player_cmd_to_string(msg->cmd), msg->cmd);

    switch (msg->cmd) {
    case MEDIA_PLAYER_CMD_SET_EVENT:
        break;

    case MEDIA_PLAYER_CMD_SET_OPTIONS:
        if (av_dict_parse_string(&ctx->format_opt, msg->data, "=", ":", 0) < 0)
            MEDIA_ERR("ctx %p av_dict_parse_string (%s) failed.\n", ctx, msg->data);
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
            ctx->exit = 1;
            break;
        }

        if (!pending_stop)
            media_player_notify_finalize(ctx);

        ctx->exit = 1;
    case MEDIA_PLAYER_CMD_STOP:
    case MEDIA_PLAYER_CMD_RESET:
        media_player_stop(ctx);
        break;
    default:
        break;
    }

    av_freep(&msg);
}

int media_player_process_cmd(MediaPlayerContext* ctx, const char* target, const char* cmd, const char* arg, char* res, int res_len)
{
    char url[PATH_MAX];
    int ret = 0;

    if (!ctx)
        return -EINVAL;

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
    } else if (!strcmp(cmd, "get_duration")) {
        snprintf(res, res_len, "%" PRIu32, ctx->duration_ms);
    } else if (!strcmp(cmd, "get_position")) {
        snprintf(res, res_len, "%" PRIu32, ctx->current_ms);
    } else if (!strcmp(cmd, "get_playing")) {
        snprintf(res, res_len, "%d", ctx->state == MEDIA_PLAYER_STATE_STARTED);
    } else if (!strcmp(cmd, "get_volume")) {
        ret = media_player_volume(ctx, NULL, res, res_len);
    } else if (!strcmp(cmd, "volume")) {
        ret = media_player_volume(ctx, arg, res, res_len);
    } else if (!strcmp(cmd, "get_latency")) {
        return media_player_get_latency(ctx, res, res_len);
    } else if (!res && !res_len) {
        return media_stub_process_command(target, cmd, arg);
    } else {
        MEDIA_ERR("unknown cmd: %s.\n", cmd);
        return AVERROR(EINVAL);
    }

    MEDIA_INFO("ctx %p cmd: %s, arg %s, target %s. ret %d\n", ctx, cmd, arg ? arg : "NULL",
        target ? target : "NULL", ret);

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
        MEDIA_ERR("ctx %p unsupported id %d\n", ctx, (int)id);
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

    if (key == NULL || cpu == NULL)
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

static int media_player_get_pollfd(MediaPlayerContext* ctx, struct pollfd* fds, int count)
{
    int nfd = 0;

    if (!fds || count < 2)
        return -EINVAL;

    fds[nfd].fd = ctx->tran_fd;
    fds[nfd].events = POLLIN;
    fds[nfd].revents = 0;
    nfd++;

    fds[nfd].fd = ctx->event_fd;
    fds[nfd].events = POLLIN;
    fds[nfd].revents = 0;
    nfd++;

    return nfd;
}

static int media_player_poll_available(MediaPlayerContext* ctx, struct pollfd* fds)
{
    int ret = -EINVAL;
    uint32_t code;
    media_parcel ack;
    uint64_t cnt = 0;

    if (fds->revents & POLLERR)
        goto out;

    if (fds->fd == ctx->event_fd) {
        ret = read(ctx->event_fd, &cnt, sizeof(cnt));
        if (ret < 0) {
            MEDIA_ERR("read event_fd failed %d\n", ret);
            goto out;
        }
    } else if (fds->fd == ctx->tran_fd) {
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
    }

    if (((fds->revents & POLLIN) && ret == -EPIPE) || (fds->revents & POLLHUP))
        goto out;

    return ret;

out:
    MEDIA_DEBUG("fds:%d revent:%d\n", fds->fd, (int)fds->revents);
    media_player_conn_close(ctx);
    return 0;
}

static void media_player_poll(MediaPlayerContext* ctx)
{
    int ret, i, n;
    for (i = n = 0; i < ctx->poll_cnt; i++) {
        if (!ctx->poll[i].get_pollfds)
            continue;

        ret = ctx->poll[i].get_pollfds(ctx, &ctx->fds[n], CONFIG_MEDIA_PLAYER_MAX_POLLFDS - n);
        if (ret < 0) {
            MEDIA_ERR("get pollfd failed %d\n", ret);
            continue;
        }

        while (ret--)
            ctx->idx[n++] = i;
    }

    if (n < 1)
        return;

    ret = poll(ctx->fds, n, ctx->poll_timeout);
    if (ret < 0)
        MEDIA_ERR("poll failed %d\n", ret);
    else if (ret == 0)
        MEDIA_DEBUG("poll timeout\n");

    for (i = 0; i < n; i++) {
        if (!ctx->fds[i].revents)
            continue;

        ret = ctx->poll[ctx->idx[i]].poll_available(ctx, &ctx->fds[i]);
        if (ret < 0 && ret != -EAGAIN && ret != -EPIPE)
            MEDIA_ERR("%s poll_available failed %d\n",
                ctx->poll[ctx->idx[i]].name, ret);
    }
}

static MediaPlayerContext* media_player_get_available_session(MediaPlayerPriv* priv)
{
    MediaPlayerContext* ctx = NULL;
    int i;

    for (i = 0; i < CONFIG_MEDIA_PLAYER_MAX_CNT; i++) {
        ctx = &priv->ctxs[i];
        if (ctx->state == MEDIA_PLAYER_STATE_IDLE)
            break;
    }

    return ctx;
}

static void media_player_dump(MediaPlayerPriv* priv)
{
    MediaPlayerContext* ctx;
    AVBPrint buf;
    int i;

    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_UNLIMITED);
    av_bprintf(&buf, "\n--------------player dump start-------------\n");
    for (i = 0; i < CONFIG_MEDIA_PLAYER_MAX_CNT; i++) {
        ctx = &priv->ctxs[i];
        if (ctx->state == MEDIA_PLAYER_STATE_IDLE)
            continue;
        av_bprintf(&buf, "player[%d, %s] state:%d", i, ctx->name, ctx->state);
        if (ctx->audio_stream && ctx->audio_stream->codec_ctx)
            av_bprintf(&buf, ", a: %d %s %" PRId64 " %d ch:%d %d %" PRIu32 "",
                AVMEDIA_TYPE_AUDIO,
                avcodec_get_name(ctx->audio_stream->codec_ctx->codec_id),
                ctx->audio_stream->codec_ctx->bit_rate,
                ctx->audio_stream->codec_ctx->sample_rate,
                ctx->audio_stream->codec_ctx->ch_layout.nb_channels,
                media_player_queue_cnt(ctx, AVMEDIA_TYPE_AUDIO), ctx->aframe_cnt);
        if (ctx->video_stream && ctx->video_stream->codec_ctx)
            av_bprintf(&buf, ", v: %d %s %dx%d %d %" PRIu32 "", AVMEDIA_TYPE_VIDEO,
                avcodec_get_name(ctx->video_stream->codec_ctx->codec_id),
                ctx->video_stream->codec_ctx->width,
                ctx->video_stream->codec_ctx->height,
                media_player_queue_cnt(ctx, AVMEDIA_TYPE_VIDEO), ctx->vframe_cnt);
    }
    av_bprintf(&buf, "\n--------------player dump end---------------\n");
    MEDIA_INFO("%s\n", buf.str);
    av_bprint_finalize(&buf, NULL);
}

static void media_player_get_timestamp(MediaPlayerContext* ctx, int64_t* ts, int64_t* lat)
{
    switch (ctx->sync_mode) {
    case MEDIA_PLAYER_SYNC_MODE_AUDIO:
        // TODO: get audio timestamp and latency
        *lat = 0;
        *ts = av_gettime_relative();
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

static int media_player_sync_video(MediaPlayerContext* ctx, int64_t pts, int64_t ts, int64_t lat)
{
    int64_t now, diff;

    if (ctx->ts_base == AV_NOPTS_VALUE) {
        if (ctx->lat_base == AV_NOPTS_VALUE)
            ctx->ts_base = ts;
        else
            ctx->ts_base = ts - pts;
        ctx->lat_base = lat;
        MEDIA_INFO("ctx %p sync pts:%" PRId64 " ts:%" PRId64 " base:%" PRId64 " lat:%" PRId64 "\n",
            ctx, pts, ts, ctx->ts_base, lat);
    }

    now = ts - ctx->ts_base;
    diff = pts - now;
    diff += ctx->lat_base;

    MEDIA_DEBUG("ctx %p sync pts:%" PRId64 " ts:%" PRId64 " now:%" PRId64 " diff:%" PRId64 " lat:%" PRId64 "\n",
        ctx, pts, ts, now, diff, lat);

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

static void media_player_proc_avsync(MediaPlayerContext* ctx)
{
    int64_t pts, ts, latency;
    AVFrame* frame;
    int diff;

    if (ctx->state != MEDIA_PLAYER_STATE_STARTED || media_player_queue_cnt(ctx, AVMEDIA_TYPE_VIDEO) == 0)
        return;

    media_player_get_timestamp(ctx, &ts, &latency);
    if (ts != AV_NOPTS_VALUE) {
        frame = media_player_queue_peek(ctx, AVMEDIA_TYPE_VIDEO);
        pts = av_rescale_q(frame->pts, frame->time_base, AV_TIME_BASE_Q);
        diff = media_player_sync_video(ctx, pts, ts, latency);
        if (diff >= 0) {
            if (diff > 0)
                return;
            frame = media_player_queue_pop(ctx, AVMEDIA_TYPE_VIDEO);
            if (media_video_output_write_frame(ctx->video_output, frame) < 0)
                MEDIA_ERR("ctx %p video_output write frame failed.\n", ctx);
        } else {
            frame = media_player_queue_pop(ctx, AVMEDIA_TYPE_VIDEO);
            av_frame_free(&frame);
            MEDIA_ERR("ctx %p drop frame pts:%" PRId64 " ts:%" PRId64 " diff:%d\n",
                ctx, pts, ts, diff);
        }
    } else {
        frame = media_player_queue_pop(ctx, AVMEDIA_TYPE_VIDEO);
        if (media_video_output_write_frame(ctx->video_output, frame) < 0)
            MEDIA_ERR("ctx %p video_output write frame failed.\n", ctx);
    }
}

static void* media_player_thread(void* arg)
{
    MediaPlayerContext* ctx = (MediaPlayerContext*)arg;
    MEDIA_INFO("ctx %p create player thread.\n", ctx);
    PlayerCmd* msg;
    uint64_t cnt = 1;

    while (1) {
        if (media_player_is_exit(ctx))
            break;

        media_player_poll(ctx);

        while ((msg = SIMPLEQ_FIRST(&ctx->cmd_queue)) != NULL) {
            SIMPLEQ_REMOVE_HEAD(&ctx->cmd_queue, entry);
            media_player_proc_cmd(ctx, msg);
        }

        if (media_player_dat_available(ctx) && media_player_proc_dat(ctx) >= 0) {
            write(ctx->event_fd, &cnt, sizeof(cnt));
        }

        media_player_proc_avsync(ctx);
    }

    media_player_close(ctx);

    media_player_ctx_release(ctx);

    MEDIA_INFO("ctx %p %s player thread exit.\n", ctx, ctx->name);
    return NULL;
}

static int media_player_open(MediaPlayerContext* ctx, const char* name)
{
    struct sched_param param;
    pthread_attr_t attr;
    pthread_t thread;
    int ret = -EINVAL;

    if (ctx->state != MEDIA_PLAYER_STATE_IDLE || name == NULL)
        return ret;

    strlcpy(ctx->name, name, sizeof(ctx->name));

    media_player_ctx_init(ctx);

    audio_graph_open(&ctx->audio_output, ctx->name);
    if (ctx->audio_output == NULL) {
        MEDIA_ERR("ctx %p open audio output failed\n", ctx);
        goto out;
    }

    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, CONFIG_MEDIA_PLAYER_STACKSIZE);
    param.sched_priority = CONFIG_MEDIA_PLAYER_PRIORITY;
    pthread_attr_setschedparam(&attr, &param);
    ret = pthread_create(&thread, &attr, media_player_thread, ctx);
    pthread_attr_destroy(&attr);
    if (ret != 0) {
        MEDIA_ERR("create player thread failed, ret %d\n", ret);
        goto out;
    }

    pthread_setname_np(thread, ctx->name);
    pthread_detach(thread);

    return 0;

out:
    audio_graph_close(&ctx->audio_output);
    ctx->state = MEDIA_PLAYER_STATE_IDLE;
    return ret;
}

static int media_player_handler(MediadPlugin* handle, struct media_server_conn* conn, const char* target, const char* cmd, const char* arg, int flags, char* res, int res_len)
{
    MediaPlayerPriv* priv = handle->priv;
    char option_name[64] = { 0 };
    char options[256] = { 0 };
    int ret = 0;

    MEDIA_INFO("cmd: %s, arg %s, target %s.\n",
        cmd, arg ? arg : "NULL", target ? target : "NULL");

    if (!strcmp(cmd, "open")) {
        MediaPlayerContext* ctx = media_player_get_available_session(priv);
        if (!ctx) {
            MEDIA_ERR("player open failed...\n");
            return -ENOMEM;
        }

        ctx->tran_fd = media_server_get_tran_fd(conn);
        if (ctx->tran_fd < 0) {
            MEDIA_ERR("player get tran fd failed...\n");
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

        ret = media_player_open(ctx, arg);
        if (ret < 0)
            return ret;

        MEDIA_INFO("ctx %p open %s success...\n", ctx, ctx->name);
    } else if (!strcmp(cmd, "dump")) {
        media_player_dump(priv);
    }

    return 0;
}

MediadPlugin media_player_plugin = {
    .name = "media_player",
    .priv_size = sizeof(struct MediaPlayerPriv),
    .priv = NULL,
    .init = NULL,
    .get = NULL,
    .available = NULL,
    .run_once = NULL,
    .uninit = NULL,
    .process_command = media_player_handler,
};
