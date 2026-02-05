/****************************************************************************
 * frameworks/media/server/audio_negotiation.c
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

#include <libavfilter/avfilter.h>
#include <libavfilter/avfilter_internal.h>
#include <libavfilter/formats.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>

#include "media_common.h"

#define MAX_LINKS 10
#define ROUTE_OFF 0
#define ROUTE_ON 1

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static bool audio_filter_is_started(AVFilterContext* ctx)
{
    int64_t state = 0;

    if (av_opt_get_int(ctx, "state", AV_OPT_SEARCH_CHILDREN, &state) < 0)
        return true;

    /* started if state is RUNNING(1) or PAUSED(2) */
    return state != 0;
}

static int audio_query_formats(AVFilterContext* ctx)
{
    AVFilterFormatsConfig **cfg_in_dyn = NULL, **cfg_out_dyn = NULL;
    AVFilterFormatsConfig *cfg_in_stack[64], *cfg_out_stack[64];
    AVFilterFormatsConfig **cfg_in, **cfg_out;
    int ret;

    if (ctx->nb_inputs > FF_ARRAY_ELEMS(cfg_in_stack)) {
        cfg_in_dyn = av_malloc_array(ctx->nb_inputs, sizeof(*cfg_in_dyn));
        if (!cfg_in_dyn)
            return AVERROR(ENOMEM);
        cfg_in = cfg_in_dyn;
    } else
        cfg_in = ctx->nb_inputs ? cfg_in_stack : NULL;
    for (unsigned i = 0; i < ctx->nb_inputs; i++) {
        AVFilterLink* l = ctx->inputs[i];
        cfg_in[i] = &l->outcfg;
    }
    if (ctx->nb_outputs > FF_ARRAY_ELEMS(cfg_out_stack)) {
        cfg_out_dyn = av_malloc_array(ctx->nb_outputs, sizeof(*cfg_out_dyn));
        if (!cfg_out_dyn) {
            av_freep(&cfg_in_dyn);
            return AVERROR(ENOMEM);
        }
        cfg_out = cfg_out_dyn;
    } else
        cfg_out = ctx->nb_outputs ? cfg_out_stack : NULL;
    for (unsigned i = 0; i < ctx->nb_outputs; i++) {
        AVFilterLink* l = ctx->outputs[i];
        cfg_out[i] = &l->incfg;
    }

    ret = ctx->filter->formats.query_func2(ctx, cfg_in, cfg_out);
    if (ret < 0) {
        if (ret != AVERROR(EAGAIN))
            MEDIA_INFO("Query format failed for '%s': %s\n",
                ctx->name, av_err2str(ret));
        return ret;
    }

#if defined(CONFIG_MEDIA_LOG_DEBUG)
    MEDIA_INFO("Filter '%s' query format results:\n", ctx->name);

    for (unsigned i = 0; i < ctx->nb_inputs; i++) {
        AVFilterFormatsConfig* incfg = cfg_in[i];
        MEDIA_INFO("  Input[%u] formats (outcfg):\n", i);

        if (incfg->formats && incfg->formats->nb_formats > 0) {
            MEDIA_INFO("    Sample formats: ");
            for (int j = 0; j < incfg->formats->nb_formats; j++) {
                MEDIA_INFO("%s(%d) ",
                    av_get_sample_fmt_name(incfg->formats->formats[j]),
                    incfg->formats->formats[j]);
            }
            MEDIA_INFO("\n");
        }

        if (incfg->samplerates && incfg->samplerates->nb_formats > 0) {
            MEDIA_INFO("    Sample rates: ");
            for (int j = 0; j < incfg->samplerates->nb_formats; j++) {
                MEDIA_INFO("%d ", incfg->samplerates->formats[j]);
            }
            MEDIA_INFO("\n");
        }

        if (incfg->channel_layouts && incfg->channel_layouts->nb_channel_layouts > 0) {
            MEDIA_INFO("    Channel layouts: ");
            for (int j = 0; j < incfg->channel_layouts->nb_channel_layouts; j++) {
                char buf[256];
                av_channel_layout_describe(&incfg->channel_layouts->channel_layouts[j],
                    buf, sizeof(buf));
                MEDIA_INFO("%s ", buf);
            }
            MEDIA_INFO("\n");
        }
    }

    for (unsigned i = 0; i < ctx->nb_outputs; i++) {
        AVFilterFormatsConfig* outcfg = cfg_out[i];
        MEDIA_INFO("  Output[%u] formats (incfg):\n", i);

        if (outcfg->formats && outcfg->formats->nb_formats > 0) {
            MEDIA_INFO("    Sample formats: ");
            for (int j = 0; j < outcfg->formats->nb_formats; j++) {
                MEDIA_INFO("%s(%d) ",
                    av_get_sample_fmt_name(outcfg->formats->formats[j]),
                    outcfg->formats->formats[j]);
            }
            MEDIA_INFO("\n");
        }

        if (outcfg->samplerates && outcfg->samplerates->nb_formats > 0) {
            MEDIA_INFO("    Sample rates: ");
            for (int j = 0; j < outcfg->samplerates->nb_formats; j++) {
                MEDIA_INFO("%d ", outcfg->samplerates->formats[j]);
            }
            MEDIA_INFO("\n");
        }

        if (outcfg->channel_layouts && outcfg->channel_layouts->nb_channel_layouts > 0) {
            MEDIA_INFO("    Channel layouts: ");
            for (int j = 0; j < outcfg->channel_layouts->nb_channel_layouts; j++) {
                char buf[256];
                av_channel_layout_describe(&outcfg->channel_layouts->channel_layouts[j],
                    buf, sizeof(buf));
                MEDIA_INFO("%s ", buf);
            }
            MEDIA_INFO("\n");
        }
    }
#endif

    av_freep(&cfg_in_dyn);
    av_freep(&cfg_out_dyn);
    return 0;
}

static int audio_set_format_config(AVFilterLink* link, int fmt, int rate, int ch)
{
    FilterLinkInternal* li = (FilterLinkInternal*)link;
    int (*config_link)(AVFilterLink*);
    int ret = 0;

    if (fmt < 0 || rate <= 0 || ch <= 0) {
        MEDIA_WARN("Invalid format: fmt=%d, rate=%d, ch=%d\n", fmt, rate, ch);
        return -EAGAIN;
    }

    MEDIA_INFO(
        "Setting format for link: "
        "src='%s' ---> dst='%s' | "
        "format='%s', sample_rate=%d, channels=%d\n",
        link->src ? link->src->name : "unknown",
        link->dst ? link->dst->name : "unknown",
        av_get_sample_fmt_name((enum AVSampleFormat)fmt), rate, ch);

    link->format = fmt;
    link->sample_rate = rate;
    av_channel_layout_default(&link->ch_layout, ch);
    link->time_base = (AVRational) { 1, rate };

    li->status_in = 0;
    li->status_out = 0;

    if (link->srcpad && (config_link = link->srcpad->config_props)) {
        ret = config_link(link);
        if (ret < 0) {
            MEDIA_ERR("Failed to configure output pad on %s\n", link->src->name);
            return ret;
        }
    }

    if (link->dstpad && (config_link = link->dstpad->config_props)) {
        ret = config_link(link);
        if (ret < 0) {
            MEDIA_ERR("Failed to configure input pad on %s\n", link->dst->name);
            return ret;
        }
    }

    return ret;
}

static AVFilterFormats* audio_create_merged_formats(AVFilterFormats* a, AVFilterFormats* b)
{
    AVFilterFormats* merged = NULL;
    int common_formats[65] = { 0 };
    int count = 0;
    int i, j;

    if (!a || !b)
        return NULL;

    for (i = 0; i < a->nb_formats && count < 64; i++) {
        for (j = 0; j < b->nb_formats; j++) {
            if (a->formats[i] == b->formats[j]) {
                common_formats[count++] = a->formats[i];
                break;
            }
        }
    }

    if (count == 0)
        return NULL;

    common_formats[count] = -1;
    merged = ff_make_format_list(common_formats);
    return merged;
}

static AVFilterChannelLayouts* audio_create_merged_channel_layouts(
    AVFilterChannelLayouts* a, AVFilterChannelLayouts* b)
{
    AVFilterChannelLayouts* merged = NULL;
    AVChannelLayout layouts[64] = { 0 };
    int i, j, count = 0;
    int ret = 0;

    if (!a || !b)
        return NULL;

    for (i = 0; i < a->nb_channel_layouts; i++) {
        for (j = 0; j < b->nb_channel_layouts; j++) {
            if (av_channel_layout_compare(&a->channel_layouts[i],
                    &b->channel_layouts[j])
                == 0) {
                ret = av_channel_layout_copy(&layouts[count], &a->channel_layouts[i]);
                if (ret != 0)
                    goto fail;
                count++;
                break;
            }
        }
    }

    for (i = 0; i < count; i++) {
        ret = ff_add_channel_layout(&merged, &layouts[i]);
        if (ret < 0)
            goto fail;
    }

fail:
    for (i = 0; i < count; i++)
        av_channel_layout_uninit(&layouts[i]);
    return merged;
}

// Calculate intersection of two format configurations
static void audio_calculate_intersection(
    AVFilterFormatsConfig* a,
    AVFilterFormatsConfig* b,
    AVFilterFormatsConfig* result)
{
    if (!a || !b)
        return;

    memset(result, 0, sizeof(AVFilterFormatsConfig));

    if (a->formats && b->formats)
        result->formats = audio_create_merged_formats(a->formats, b->formats);

    if (a->samplerates && b->samplerates)
        result->samplerates = audio_create_merged_formats(a->samplerates, b->samplerates);

    if (a->channel_layouts && b->channel_layouts)
        result->channel_layouts = audio_create_merged_channel_layouts(
            a->channel_layouts, b->channel_layouts);
}

static void audio_get_max_format(AVFilterFormatsConfig* config, int* fmt)
{
    if (config->formats && config->formats->nb_formats > 0) {
        *fmt = config->formats->formats[0];
        for (int i = 1; i < config->formats->nb_formats; i++)
            *fmt = FFMAX(*fmt, config->formats->formats[i]);
    }
}

static void audio_get_max_samplerate(AVFilterFormatsConfig* config, int* rate)
{
    if (config->samplerates && config->samplerates->nb_formats > 0) {
        *rate = config->samplerates->formats[0];
        for (int i = 1; i < config->samplerates->nb_formats; i++)
            *rate = FFMAX(*rate, config->samplerates->formats[i]);
    }
}

static void audio_get_max_channels(AVFilterFormatsConfig* config, int* ch)
{
    if (config->channel_layouts && config->channel_layouts->nb_channel_layouts > 0) {
        *ch = config->channel_layouts->channel_layouts[0].nb_channels;
        for (int i = 1; i < config->channel_layouts->nb_channel_layouts; i++)
            *ch = FFMAX(*ch, config->channel_layouts->channel_layouts[i].nb_channels);
    }
}

static void audio_free_formats_config(AVFilterFormatsConfig* config)
{
    if (config->formats)
        ff_formats_unref(&config->formats);

    if (config->samplerates)
        ff_formats_unref(&config->samplerates);

    if (config->channel_layouts)
        ff_channel_layouts_unref(&config->channel_layouts);
}

#if defined(CONFIG_MEDIA_LOG_DEBUG)
static void audio_debug_print_formats_config(AVFilterFormatsConfig* config, const char* indent)
{
    char buf[256];
    int i;

    if (!config) {
        MEDIA_INFO("%sConfig is NULL\n", indent);
        return;
    }

    if (config->formats && config->formats->nb_formats > 0) {
        MEDIA_INFO("%sSample formats: ", indent);
        for (i = 0; i < config->formats->nb_formats; i++) {
            MEDIA_INFO("%s(%d) ", av_get_sample_fmt_name(config->formats->formats[i]),
                config->formats->formats[i]);
        }
        MEDIA_INFO("\n");
    } else {
        MEDIA_INFO("%sSample formats: (none)\n", indent);
    }

    if (config->samplerates && config->samplerates->nb_formats > 0) {
        MEDIA_INFO("%sSample rates: ", indent);
        for (i = 0; i < config->samplerates->nb_formats; i++) {
            MEDIA_INFO("%d ", config->samplerates->formats[i]);
        }
        MEDIA_INFO("\n");
    } else {
        MEDIA_INFO("%sSample rates: (none)\n", indent);
    }

    if (config->channel_layouts && config->channel_layouts->nb_channel_layouts > 0) {
        MEDIA_INFO("%sChannel layouts: ", indent);
        for (i = 0; i < config->channel_layouts->nb_channel_layouts; i++) {
            av_channel_layout_describe(&config->channel_layouts->channel_layouts[i],
                buf, sizeof(buf));
            MEDIA_INFO("%s (%d ch) ", buf,
                config->channel_layouts->channel_layouts[i].nb_channels);
        }
        MEDIA_INFO("\n");
    } else {
        MEDIA_INFO("%sChannel layouts: (none)\n", indent);
    }
}
#endif

static int audio_negotiate_formats_init(AVFilterContext* filter)
{
    AVFilterContext* stack[MAX_LINKS];
    bool traverse_downstream;
    AVFilterContext* current;
    int map[MAX_LINKS] = { 0 };
    AVFilterLink* link;
    int stack_size = 0;
    int i, j, ret;

    if (audio_filter_is_started(filter)) {
        // query the starting node first
        ret = audio_query_formats(filter);
        if (ret < 0)
            return ret;
    }

    // decide traversal direction
    traverse_downstream = (filter->nb_outputs > 0)
        || (filter->nb_inputs == 0)
        || (filter->nb_inputs > 0 && filter->nb_outputs > 0);

    if (stack_size >= MAX_LINKS) {
        MEDIA_ERR("Stack overflow in formats init\n");
        return AVERROR(ENOMEM);
    }
    stack[stack_size++] = filter;

    while (stack_size > 0) {
        current = stack[--stack_size];

        if (traverse_downstream) {

            // traverse downstream: current -> output -> dst
            for (i = 0; i < current->nb_outputs; i++) {
                link = current->outputs[i];

                memset(map, 0, sizeof(map));
                if (av_opt_get_array(current, "map_array", AV_OPT_SEARCH_CHILDREN,
                        0, current->nb_outputs, AV_OPT_TYPE_INT, map)
                    < 0)
                    MEDIA_WARN("Failed to get map array for %s\n", current->name);

                if (map[i] != ROUTE_ON)
                    continue;

                if (!audio_filter_is_started(link->dst)) {
                    MEDIA_INFO("Skipping format negotiation for sink '%s' (started=false)\n",
                        link->dst->name);
                    continue;
                }

                ret = audio_query_formats(link->dst);
                if (ret < 0)
                    return ret;

                if (link->dst->nb_outputs > 0) {
                    if (stack_size >= MAX_LINKS) {
                        MEDIA_ERR("Stack overflow traversing downstream\n");
                        return AVERROR(ENOMEM);
                    }
                    stack[stack_size++] = link->dst;
                }
            }
        } else {

            // traverse upstream: current <- input <- src
            for (i = 0; i < current->nb_inputs; i++) {
                link = current->inputs[i];
                if (!link || !link->src)
                    continue;

                for (j = 0; j < link->src->nb_outputs; j++) {
                    if (link->src->outputs[j] == link)
                        break;
                }

                memset(map, 0, sizeof(map));
                if (av_opt_get_array(link->src, "map_array", AV_OPT_SEARCH_CHILDREN,
                        0, link->src->nb_outputs, AV_OPT_TYPE_INT, map)
                    < 0)
                    MEDIA_WARN("Failed to get map array for %s\n", link->src->name);

                if (map[j] != ROUTE_ON) {
                    MEDIA_INFO("Skipping query for input[%d] of '%s' (src output[%d] map=OFF)\n",
                        i, current->name, j);
                    continue;
                }

                if (!audio_filter_is_started(link->src)) {
                    MEDIA_INFO("Skipping format negotiation for sink '%s' (started=false)\n",
                        link->dst->name);
                    continue;
                }

                ret = audio_query_formats(link->src);
                if (ret < 0)
                    return ret;

                if (link->src->nb_inputs > 0) {
                    if (stack_size >= MAX_LINKS) {
                        MEDIA_ERR("Stack overflow traversing upstream\n");
                        return AVERROR(ENOMEM);
                    }
                    stack[stack_size++] = link->src;
                }
            }
        }
    }

    return 0;
}

static void audio_reset_link_format(AVFilterLink* link)
{
    if (!link)
        return;

    link->format = -1;
    link->sample_rate = -1;
    link->ch_layout.nb_channels = -1;
}

static int audio_negotiate_link(
    AVFilterLink* isrc_link,
    AVFilterLink* isink_link,
    AVFilterLink* osrc_link,
    AVFilterLink* osink_link)
{
    AVFilterFormatsConfig final_config = { 0 };
    AVFilterFormatsConfig sink_config = { 0 };
    AVFilterFormatsConfig src_config = { 0 };
    int i;

    // Stage 1: Calculate internal format intersections for both links
    if (isrc_link)
        audio_calculate_intersection(&isrc_link->outcfg, &isrc_link->incfg, &src_config);
    if (isink_link)
        audio_calculate_intersection(&isink_link->outcfg, &isink_link->incfg, &sink_config);

#if defined(CONFIG_MEDIA_LOG_DEBUG)
    MEDIA_INFO("[Intersection Debug] After src_link (outcfg & incfg):\n");
    audio_debug_print_formats_config(&src_config, "    ");
    MEDIA_INFO("[Intersection Debug] After sink_link (outcfg & incfg):\n");
    audio_debug_print_formats_config(&sink_config, "    ");
#endif

    // Stage 2: Calculate final intersection between source and sink configurations
    audio_calculate_intersection(&src_config, &sink_config, &final_config);

#if defined(CONFIG_MEDIA_LOG_DEBUG)
    MEDIA_INFO("[Intersection Debug] After final intersection (src_config & sink_config):\n");
    audio_debug_print_formats_config(&final_config, "    ");
#endif

    // 1. Negotiate sample format with three-level fallback strategy
    if (final_config.formats && final_config.formats->nb_formats > 0) {

        // Priority 1: Use largest value from final_config intersection
        osrc_link->format = osink_link->format = final_config.formats->formats[0];
        for (i = 1; i < final_config.formats->nb_formats; i++)
            osrc_link->format = osink_link->format
                = FFMAX(osrc_link->format, final_config.formats->formats[i]);
    } else {

        // Priority 2: Fallback to individual configs if no final intersection
        if (src_config.formats && src_config.formats->nb_formats > 0) {
            osrc_link->format = src_config.formats->formats[0];
            for (i = 1; i < src_config.formats->nb_formats; i++)
                osrc_link->format = FFMAX(osrc_link->format, src_config.formats->formats[i]);
        } else {

            // Priority 3: Ultimate fallback to source link's incfg
            if (isrc_link)
                audio_get_max_format(&isrc_link->incfg, &osrc_link->format);
        }

        if (sink_config.formats && sink_config.formats->nb_formats > 0) {
            osink_link->format = sink_config.formats->formats[0];
            for (i = 1; i < sink_config.formats->nb_formats; i++)
                osink_link->format = FFMAX(osink_link->format, sink_config.formats->formats[i]);
        } else {

            // Priority 3: Ultimate fallback to sink link's incfg
            if (isink_link)
                audio_get_max_format(&isink_link->incfg, &osink_link->format);
        }
    }

    // 2. Negotiate sample rate with three-level fallback strategy
    if (final_config.samplerates && final_config.samplerates->nb_formats > 0) {

        // Priority 1: Use largest value from final_config intersection
        osrc_link->sample_rate = osink_link->sample_rate = final_config.samplerates->formats[0];
        for (i = 1; i < final_config.samplerates->nb_formats; i++)
            osrc_link->sample_rate = osink_link->sample_rate
                = FFMAX(osrc_link->sample_rate, final_config.samplerates->formats[i]);
    } else {

        // Priority 2: Fallback to individual configs if no final intersection
        if (src_config.samplerates && src_config.samplerates->nb_formats > 0) {
            osrc_link->sample_rate = src_config.samplerates->formats[0];
            for (i = 1; i < src_config.samplerates->nb_formats; i++)
                osrc_link->sample_rate = FFMAX(osrc_link->sample_rate,
                    src_config.samplerates->formats[i]);
        } else {

            // Priority 3: Ultimate fallback to source link's incfg
            if (isrc_link)
                audio_get_max_samplerate(&isrc_link->incfg, &osrc_link->sample_rate);
        }

        if (sink_config.samplerates && sink_config.samplerates->nb_formats > 0) {
            osink_link->sample_rate = sink_config.samplerates->formats[0];
            for (i = 1; i < sink_config.samplerates->nb_formats; i++)
                osink_link->sample_rate = FFMAX(osink_link->sample_rate,
                    sink_config.samplerates->formats[i]);
        } else {
            if (isink_link)
                audio_get_max_samplerate(&isink_link->incfg, &osink_link->sample_rate);
        }
    }

    // 3. Negotiate channel layout with three-level fallback strategy
    if (final_config.channel_layouts && final_config.channel_layouts->nb_channel_layouts > 0) {

        // Priority 1: Use largest channel count from final_config intersection
        osrc_link->ch_layout.nb_channels = osink_link->ch_layout.nb_channels
            = final_config.channel_layouts->channel_layouts[0].nb_channels;
        for (i = 1; i < final_config.channel_layouts->nb_channel_layouts; i++)
            osrc_link->ch_layout.nb_channels
                = osink_link->ch_layout.nb_channels
                = FFMAX(osrc_link->ch_layout.nb_channels,
                    final_config.channel_layouts->channel_layouts[i].nb_channels);
    } else {

        // Priority 2: Fallback to individual configs if no final intersection
        if (src_config.channel_layouts && src_config.channel_layouts->nb_channel_layouts > 0) {
            osrc_link->ch_layout.nb_channels
                = src_config.channel_layouts->channel_layouts[0].nb_channels;
            for (i = 1; i < src_config.channel_layouts->nb_channel_layouts; i++)
                osrc_link->ch_layout.nb_channels = FFMAX(osrc_link->ch_layout.nb_channels,
                    src_config.channel_layouts->channel_layouts[i].nb_channels);
        } else {
            // Priority 3: Ultimate fallback to source link's incfg
            if (isrc_link)
                audio_get_max_channels(&isrc_link->incfg, &osrc_link->ch_layout.nb_channels);
        }

        if (sink_config.channel_layouts && sink_config.channel_layouts->nb_channel_layouts > 0) {
            osink_link->ch_layout.nb_channels
                = sink_config.channel_layouts->channel_layouts[0].nb_channels;
            for (i = 1; i < sink_config.channel_layouts->nb_channel_layouts; i++)
                osink_link->ch_layout.nb_channels = FFMAX(osink_link->ch_layout.nb_channels,
                    sink_config.channel_layouts->channel_layouts[i].nb_channels);
        } else {
            if (isink_link)
                audio_get_max_channels(&isink_link->incfg, &osink_link->ch_layout.nb_channels);
        }
    }

    audio_free_formats_config(&src_config);
    audio_free_formats_config(&sink_config);
    audio_free_formats_config(&final_config);

    return 0;
}

static int audio_negotiation_enabled_outputs(int enabled_count, AVFilterLink** enabled_outputs)
{
    AVFilterLink osink_link = { 0 }, osrc_link = { 0 };
    int ds_fmt = -1, ds_rate = -1, ds_ch = -1;
    int sr_fmt = -1, sr_rate = -1, sr_ch = -1;
    int dst_map[MAX_LINKS] = { 0 };
    AVFilterLink* sin_link = NULL;
    int i = 0, j = 0, ret = 0;

    // Initialize formats to -1 to differentiate from AV_SAMPLE_FMT_U8 (0)
    audio_reset_link_format(&osink_link);
    audio_reset_link_format(&osrc_link);

    for (i = 0; i < enabled_count; i++) {
        if (enabled_outputs[i]->dst && enabled_outputs[i]->dst->nb_outputs > 0) {
            memset(dst_map, 0, sizeof(dst_map));
            if (av_opt_get_array(enabled_outputs[i]->dst, "map_array", AV_OPT_SEARCH_CHILDREN,
                    0, enabled_outputs[i]->dst->nb_outputs, AV_OPT_TYPE_INT, dst_map)
                < 0)
                MEDIA_WARN("Failed to get map array for %s.\n", enabled_outputs[i]->dst->name);

            for (j = 0; j < enabled_outputs[i]->dst->nb_outputs; j++) {
                if (dst_map[j] != ROUTE_ON)
                    continue;

                sin_link = enabled_outputs[i]->dst->outputs[j];

                if (!audio_filter_is_started(sin_link->dst)) {
                    MEDIA_INFO("Skipping format setting for sink '%s' (started=false)\n",
                        sin_link->dst->name);
                    continue;
                }

                ret = audio_negotiate_link(enabled_outputs[i], sin_link, &osrc_link, &osink_link);
                if (ret < 0) {
                    MEDIA_WARN("Downstream negotiation failed for output %d, sin_link %d", i, j);
                    continue;
                }

                if (osrc_link.format != -1
                    && (sr_fmt == -1 || osrc_link.format > sr_fmt))
                    sr_fmt = osrc_link.format;
                if (osrc_link.sample_rate != -1
                    && (sr_rate == -1 || osrc_link.sample_rate > sr_rate))
                    sr_rate = osrc_link.sample_rate;
                if (osrc_link.ch_layout.nb_channels != -1
                    && (sr_ch == -1 || osrc_link.ch_layout.nb_channels > sr_ch))
                    sr_ch = osrc_link.ch_layout.nb_channels;

                if (osink_link.format != -1
                    && (ds_fmt == -1 || osink_link.format > ds_fmt))
                    ds_fmt = osink_link.format;
                if (osink_link.sample_rate != -1
                    && (ds_rate == -1 || osink_link.sample_rate > ds_rate))
                    ds_rate = osink_link.sample_rate;
                if (osink_link.ch_layout.nb_channels != -1
                    && (ds_ch == -1 || osink_link.ch_layout.nb_channels > ds_ch))
                    ds_ch = osink_link.ch_layout.nb_channels;

                memset(&osrc_link, 0, sizeof(osrc_link));
                audio_reset_link_format(&osrc_link);

                memset(&osink_link, 0, sizeof(osink_link));
                audio_reset_link_format(&osink_link);
            }
        } else {

            if (!audio_filter_is_started(enabled_outputs[i]->dst)) {
                MEDIA_INFO("Skipping format setting for sink '%s' (started=false)\n",
                    enabled_outputs[i]->dst->name);
                continue;
            }

            ret = audio_negotiate_link(enabled_outputs[i], NULL, &osrc_link, NULL);
            if (ret < 0) {
                MEDIA_WARN("Downstream negotiation failed for output %d, link %d", i, j);
                continue;
            }

            if (osrc_link.format != -1
                && (sr_fmt == -1 || osrc_link.format > sr_fmt))
                sr_fmt = osrc_link.format;
            if (osrc_link.sample_rate != -1
                && (sr_rate == -1 || osrc_link.sample_rate > sr_rate))
                sr_rate = osrc_link.sample_rate;
            if (osrc_link.ch_layout.nb_channels != -1
                && (sr_ch == -1 || osrc_link.ch_layout.nb_channels > sr_ch))
                sr_ch = osrc_link.ch_layout.nb_channels;

            memset(&osrc_link, 0, sizeof(osrc_link));
            audio_reset_link_format(&osrc_link);
        }
    }

    MEDIA_INFO("Negotiated source: fmt=%s, rate=%d, ch=%d\n",
        sr_fmt != -1 ? av_get_sample_fmt_name(sr_fmt) : "any",
        sr_rate, sr_ch);
    MEDIA_INFO("Negotiated sink:   fmt=%s, rate=%d, ch=%d\n",
        ds_fmt != -1 ? av_get_sample_fmt_name(ds_fmt) : "any",
        ds_rate, ds_ch);

    for (i = 0; i < enabled_count; i++) {
        memset(dst_map, 0, sizeof(dst_map));
        av_opt_get_array(enabled_outputs[i]->dst, "map_array", AV_OPT_SEARCH_CHILDREN,
            0, enabled_outputs[i]->dst->nb_outputs, AV_OPT_TYPE_INT, dst_map);

        for (j = 0; j < enabled_outputs[i]->dst->nb_outputs; j++) {
            if (dst_map[j] != ROUTE_ON)
                continue;

            sin_link = enabled_outputs[i]->dst->outputs[j];

            if (!audio_filter_is_started(sin_link->dst)) {
                MEDIA_INFO("Skipping format config for sink '%s' (started=false)\n",
                    sin_link->dst->name);
                continue;
            }

            ret = audio_set_format_config(sin_link, ds_fmt, ds_rate, ds_ch);
            if (ret < 0)
                MEDIA_WARN("Failed to set format for sin_link\n");
        }

        if (!audio_filter_is_started(enabled_outputs[i]->dst)) {
            MEDIA_INFO("Skipping format config for sink '%s' (started=false)\n",
                enabled_outputs[i]->dst->name);
            continue;
        }

        ret = audio_set_format_config(enabled_outputs[i], sr_fmt, sr_rate, sr_ch);
        if (ret < 0)
            MEDIA_WARN("Failed to set format for enabled_output\n");
    }

    return 0;
}

static void audio_negotiate_src(AVFilterContext* filter)
{
    int src_count = 0, stack_top = 0, enabled_count = 0;
    AVFilterLink* enabled_outputs[MAX_LINKS] = { 0 };
    AVFilterContext* srcs[MAX_LINKS] = { 0 };
    AVFilterContext* stack[MAX_LINKS] = { 0 };
    int map[MAX_LINKS] = { 0 };
    int i, s;

    // 1. collect all source filters (filters with no inputs)
    if (filter->nb_inputs == 0 || (filter->nb_inputs && filter->nb_outputs)) {
        if (src_count < MAX_LINKS)
            srcs[src_count++] = filter;
        else
            MEDIA_WARN("Max source filters reached\n");
    } else {
        if (stack_top < MAX_LINKS)
            stack[stack_top++] = filter;
        else
            MEDIA_WARN("Stack overflow in src collection\n");

        while (stack_top > 0) {
            AVFilterContext* cur = stack[--stack_top];

            if (cur->nb_inputs == 0) {
                if (src_count < MAX_LINKS)
                    srcs[src_count++] = cur;
                continue;
            }

            for (i = 0; i < cur->nb_inputs && stack_top < MAX_LINKS; i++) {
                if (cur->inputs[i] && cur->inputs[i]->src)
                    stack[stack_top++] = cur->inputs[i]->src;
            }
        }
    }

    // 2. process each source filter
    for (s = 0; s < src_count; s++) {
        AVFilterContext* src = srcs[s];
        if (!src->nb_outputs)
            continue;

        memset(map, 0, sizeof(map));
        if (av_opt_get_array(src, "map_array", AV_OPT_SEARCH_CHILDREN,
                0, src->nb_outputs, AV_OPT_TYPE_INT, map)
            < 0)
            MEDIA_WARN("Failed to get map array for src %s\n", src->name);

        enabled_count = 0;
        for (i = 0; i < src->nb_outputs; i++) {
            if (map[i] != ROUTE_ON)
                continue;

            if (enabled_count < MAX_LINKS)
                enabled_outputs[enabled_count++] = src->outputs[i];
            else
                MEDIA_WARN("Max enabled outputs reached\n");
        }

        if (enabled_count)
            audio_negotiation_enabled_outputs(enabled_count, enabled_outputs);
    }

    return;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int audio_negotiation_trigger(AVFilterContext* filter)
{
    int ret;

    ret = audio_negotiate_formats_init(filter);
    if (ret < 0)
        MEDIA_ERR("%s negotiate formats init failed %d.", filter->name, ret);

    audio_negotiate_src(filter);

    return ret;
}