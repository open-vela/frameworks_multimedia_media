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

    return 0;
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
    AVChannelLayout layouts[64];
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

static void audio_get_min_format_from_incfg(const AVFilterLink* link, int* fmt)
{
    *fmt = -1;
    if (link->incfg.formats && link->incfg.formats->nb_formats > 0) {
        *fmt = link->incfg.formats->formats[0];
        for (int i = 1; i < link->incfg.formats->nb_formats; i++)
            *fmt = FFMIN(*fmt, link->incfg.formats->formats[i]);
    }
}

static void audio_get_min_samplerate_from_incfg(const AVFilterLink* link, int* rate)
{
    *rate = -1;
    if (link->incfg.samplerates && link->incfg.samplerates->nb_formats > 0) {
        *rate = link->incfg.samplerates->formats[0];
        for (int i = 1; i < link->incfg.samplerates->nb_formats; i++)
            *rate = FFMIN(*rate, link->incfg.samplerates->formats[i]);
    }
}

static void audio_get_min_channels_from_incfg(const AVFilterLink* link, int* ch)
{
    *ch = -1;
    if (link->incfg.channel_layouts && link->incfg.channel_layouts->nb_channel_layouts > 0) {
        *ch = link->incfg.channel_layouts->channel_layouts[0].nb_channels;
        for (int i = 1; i < link->incfg.channel_layouts->nb_channel_layouts; i++)
            *ch = FFMIN(*ch, link->incfg.channel_layouts->channel_layouts[i].nb_channels);
    }
}

static void audio_select_min_values(
    AVFilterLink* link,
    AVFilterFormatsConfig* config,
    int* fmt, int* rate, int* ch)
{
    int min_fmt = -1;
    int min_rate = -1;
    int min_ch = -1;
    int i;

    if (config->formats && config->formats->nb_formats > 0) {
        min_fmt = config->formats->formats[0];
        for (i = 1; i < config->formats->nb_formats; i++) {
            min_fmt = FFMIN(min_fmt, config->formats->formats[i]);
        }
    } else if (link->incfg.formats && link->incfg.formats->nb_formats > 0)
        audio_get_min_format_from_incfg(link, &min_fmt);

    *fmt = min_fmt;

    if (config->samplerates && config->samplerates->nb_formats > 0) {
        min_rate = config->samplerates->formats[0];
        for (i = 1; i < config->samplerates->nb_formats; i++) {
            min_rate = FFMIN(min_rate, config->samplerates->formats[i]);
        }
    } else if (link->incfg.samplerates && link->incfg.samplerates->nb_formats > 0)
        audio_get_min_samplerate_from_incfg(link, &min_rate);

    *rate = min_rate;

    min_ch = -1;
    if (config->channel_layouts && config->channel_layouts->nb_channel_layouts > 0) {
        min_ch = config->channel_layouts->channel_layouts[0].nb_channels;
        for (i = 1; i < config->channel_layouts->nb_channel_layouts; i++) {
            min_ch = FFMIN(min_ch, config->channel_layouts->channel_layouts[i].nb_channels);
        }
    } else if (link->incfg.channel_layouts && link->incfg.channel_layouts->nb_channel_layouts > 0)
        audio_get_min_channels_from_incfg(link, &min_ch);

    *ch = min_ch;
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

static int audio_negotiate_three_stage_link(
    AVFilterLink* src_link,
    AVFilterLink* sink_link)
{
    int sink_fmt = -1, sink_rate = -1, sink_ch = -1;
    int src_fmt = -1, src_rate = -1, src_ch = -1;
    AVFilterFormatsConfig final_config = { 0 };
    AVFilterFormatsConfig sink_config = { 0 };
    AVFilterFormatsConfig src_config = { 0 };
    int i;

    // Stage 1: Calculate internal format intersections for both links
    audio_calculate_intersection(&src_link->outcfg, &src_link->incfg, &src_config);
    audio_calculate_intersection(&sink_link->outcfg, &sink_link->incfg, &sink_config);

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

        // Priority 1: Use minimum value from final_config intersection
        src_fmt = sink_fmt = final_config.formats->formats[0];
        for (i = 1; i < final_config.formats->nb_formats; i++)
            src_fmt = sink_fmt = FFMIN(src_fmt, final_config.formats->formats[i]);
    } else {

        // Priority 2: Fallback to individual configs if no final intersection
        if (src_config.formats && src_config.formats->nb_formats > 0) {
            src_fmt = src_config.formats->formats[0];
            for (i = 1; i < src_config.formats->nb_formats; i++)
                src_fmt = FFMIN(src_fmt, src_config.formats->formats[i]);
        } else
            // Priority 3: Ultimate fallback to source link's incfg
            audio_get_min_format_from_incfg(src_link, &src_fmt);

        if (sink_config.formats && sink_config.formats->nb_formats > 0) {
            sink_fmt = sink_config.formats->formats[0];
            for (i = 1; i < sink_config.formats->nb_formats; i++)
                sink_fmt = FFMIN(sink_fmt, sink_config.formats->formats[i]);
        } else
            // Priority 3: Ultimate fallback to sink link's incfg
            audio_get_min_format_from_incfg(sink_link, &sink_fmt);
    }

    // 2. Negotiate sample rate with three-level fallback strategy
    if (final_config.samplerates && final_config.samplerates->nb_formats > 0) {

        // Priority 1: Use minimum value from final_config intersection
        src_rate = sink_rate = final_config.samplerates->formats[0];
        for (i = 1; i < final_config.samplerates->nb_formats; i++)
            src_rate = sink_rate = FFMIN(src_rate, final_config.samplerates->formats[i]);
    } else {

        // Priority 2: Fallback to individual configs if no final intersection
        if (src_config.samplerates && src_config.samplerates->nb_formats > 0) {
            src_rate = src_config.samplerates->formats[0];
            for (i = 1; i < src_config.samplerates->nb_formats; i++)
                src_rate = FFMIN(src_rate, src_config.samplerates->formats[i]);
        } else
            // Priority 3: Ultimate fallback to source link's incfg
            audio_get_min_samplerate_from_incfg(src_link, &src_rate);

        if (sink_config.samplerates && sink_config.samplerates->nb_formats > 0) {
            sink_rate = sink_config.samplerates->formats[0];
            for (i = 1; i < sink_config.samplerates->nb_formats; i++)
                sink_rate = FFMIN(sink_rate, sink_config.samplerates->formats[i]);
        } else
            audio_get_min_samplerate_from_incfg(sink_link, &sink_rate);
    }

    // 3. Negotiate channel layout with three-level fallback strategy
    if (final_config.channel_layouts && final_config.channel_layouts->nb_channel_layouts > 0) {

        // Priority 1: Use minimum channel count from final_config intersection
        src_ch = sink_ch = final_config.channel_layouts->channel_layouts[0].nb_channels;
        for (i = 1; i < final_config.channel_layouts->nb_channel_layouts; i++)
            src_ch = sink_ch = FFMIN(src_ch, final_config.channel_layouts->channel_layouts[i].nb_channels);
    } else {

        // Priority 2: Fallback to individual configs if no final intersection
        if (src_config.channel_layouts && src_config.channel_layouts->nb_channel_layouts > 0) {
            src_ch = src_config.channel_layouts->channel_layouts[0].nb_channels;
            for (i = 1; i < src_config.channel_layouts->nb_channel_layouts; i++)
                src_ch = FFMIN(src_ch, src_config.channel_layouts->channel_layouts[i].nb_channels);
        } else
            // Priority 3: Ultimate fallback to source link's incfg
            audio_get_min_channels_from_incfg(src_link, &src_ch);

        if (sink_config.channel_layouts && sink_config.channel_layouts->nb_channel_layouts > 0) {
            sink_ch = sink_config.channel_layouts->channel_layouts[0].nb_channels;
            for (i = 1; i < sink_config.channel_layouts->nb_channel_layouts; i++)
                sink_ch = FFMIN(sink_ch, sink_config.channel_layouts->channel_layouts[i].nb_channels);
        } else
            audio_get_min_channels_from_incfg(sink_link, &sink_ch);
    }

    // Apply negotiated format configuration to source link
    if (src_fmt > 0 && src_rate > 0 && src_ch > 0)
        audio_set_format_config(src_link, src_fmt, src_rate, src_ch);

    // Apply negotiated format configuration to sink link
    if (sink_fmt > 0 && sink_rate > 0 && sink_ch > 0)
        audio_set_format_config(sink_link, sink_fmt, sink_rate, sink_ch);

    audio_free_formats_config(&src_config);
    audio_free_formats_config(&sink_config);
    audio_free_formats_config(&final_config);

    return 0;
}

static int audio_query_all_filters_formats(AVFilterContext* filter, int nb_filters)
{
    AVFilterContext* stack[MAX_LINKS];
    bool traverse_downstream;
    AVFilterContext* current;
    int map[MAX_LINKS];
    AVFilterLink* link;
    int stack_size = 0;
    int ret;
    int i;

    // query the starting node first
    ret = audio_query_formats(filter);
    if (ret < 0)
        return ret;

    // decide traversal direction
    traverse_downstream = (filter->nb_outputs > 0)
        || (filter->nb_inputs == 0)
        || (filter->nb_inputs > 0 && filter->nb_outputs > 0);

    stack[stack_size++] = filter;

    while (stack_size > 0) {
        current = stack[--stack_size];

        if (traverse_downstream) {

            // traverse downstream: current -> output -> dst
            for (i = 0; i < current->nb_outputs; i++) {
                link = current->outputs[i];

                av_opt_get_array(current, "map_array", AV_OPT_SEARCH_CHILDREN,
                    0, current->nb_outputs, AV_OPT_TYPE_INT, map);
                if (map[i] != ROUTE_ON)
                    continue;

                ret = audio_query_formats(link->dst);
                if (ret < 0)
                    return ret;

                if (link->dst->nb_outputs > 0)
                    stack[stack_size++] = link->dst;
            }
        } else {

            // traverse upstream: current <- input <- src
            for (i = 0; i < current->nb_inputs; i++) {
                link = current->inputs[i];
                if (!link || !link->src)
                    continue;

                ret = audio_query_formats(link->src);
                if (ret < 0)
                    return ret;

                if (link->src->nb_inputs > 0)
                    stack[stack_size++] = link->src;
            }
        }
    }

    return 0;
}

static int audio_handle_multi_outputs(AVFilterContext* src, int enabled_count, AVFilterLink** enabled_outputs)
{
    int fmt = -1, rate = -1, ch = -1;
    bool has_complete_intersection = false;
    AVFilterFormatsConfig config = { 0 };
    AVFilterLink* dst_outlink;
    int dst_map[MAX_LINKS] = { 0 };
    int ret = 0, i = 0;

    // 1. try to find complete intersection from all enabled outputs
    for (i = 0; i < enabled_count; i++) {
        audio_calculate_intersection(&enabled_outputs[i]->outcfg,
            &enabled_outputs[i]->incfg,
            &config);

        has_complete_intersection = (config.formats && config.formats->nb_formats > 0)
            && (config.samplerates && config.samplerates->nb_formats > 0)
            && (config.channel_layouts && config.channel_layouts->nb_channel_layouts > 0);

        if (has_complete_intersection) {
            fmt = config.formats->formats[0];
            rate = config.samplerates->formats[0];
            ch = config.channel_layouts->channel_layouts[0].nb_channels;

            for (i = 1; i < config.formats->nb_formats; i++)
                fmt = FFMIN(fmt, config.formats->formats[i]);
            for (i = 1; i < config.samplerates->nb_formats; i++)
                rate = FFMIN(rate, config.samplerates->formats[i]);
            for (i = 1; i < config.channel_layouts->nb_channel_layouts; i++)
                ch = FFMIN(ch, config.channel_layouts->channel_layouts[i].nb_channels);

            break;
        }

        audio_free_formats_config(&config);
        memset(&config, 0, sizeof(config));
    }

    // 2. if no complete intersection, use first output's preferred format
    if (!has_complete_intersection && enabled_count > 0) {
        AVFilterFormatsConfig* first_config = &enabled_outputs[0]->incfg;
        if (first_config->formats
            && first_config->formats->nb_formats > 0)
            fmt = first_config->formats->formats[0];

        if (first_config->samplerates
            && first_config->samplerates->nb_formats > 0)
            rate = first_config->samplerates->formats[0];

        if (first_config->channel_layouts
            && first_config->channel_layouts->nb_channel_layouts > 0)
            ch = first_config->channel_layouts->channel_layouts[0].nb_channels;
    }

    // 3. apply the selected format to all enabled outputs
    for (i = 0; i < enabled_count; i++) {
        ret = audio_set_format_config(enabled_outputs[i], fmt, rate, ch);
        if (ret < 0) {
            MEDIA_WARN("Failed to set common format on link %d from '%s'", i, src->name);
            continue;
        }

        // handle downstream negotiation for third-stage links
        if (enabled_outputs[i]->dst && enabled_outputs[i]->dst->nb_outputs > 0) {
            av_opt_get_array(enabled_outputs[i]->dst, "map_array", AV_OPT_SEARCH_CHILDREN,
                0, enabled_outputs[i]->dst->nb_outputs, AV_OPT_TYPE_INT, dst_map);

            for (int j = 0; j < enabled_outputs[i]->dst->nb_outputs; j++) {
                if (dst_map[j] != ROUTE_ON)
                    continue;

                dst_outlink = enabled_outputs[i]->dst->outputs[j];
                ret = audio_negotiate_three_stage_link(enabled_outputs[i], dst_outlink);
                if (ret < 0) {
                    MEDIA_WARN("Negotiation failed between '%s' and '%s'",
                        enabled_outputs[i]->dst->name,
                        dst_outlink->dst->name);
                }
            }
        }
    }

    audio_free_formats_config(&config);
    return ret;
}

static int audio_transfer_formats_from_source(AVFilterContext* filter)
{
    AVFilterLink* enabled_outputs[MAX_LINKS] = { 0 };
    AVFilterContext* srcs[MAX_LINKS] = { 0 };
    AVFilterContext* stack[MAX_LINKS] = { 0 };
    int map[MAX_LINKS] = { 0 };
    int dst_map[MAX_LINKS] = { 0 };
    int ret = 0, i, j, s;
    int enabled_count = 0;
    int map_on_count = 0;
    int src_count = 0;
    int stack_top = 0;
    bool all_eof;

    // 1. collect all source filters (filters with no inputs)
    if (filter->nb_inputs == 0 || (filter->nb_inputs && filter->nb_outputs))
        srcs[src_count++] = filter;
    else {
        stack[stack_top++] = filter;
        while (stack_top > 0) {
            AVFilterContext* cur = stack[--stack_top];
            if (cur->nb_inputs == 0) {
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

        // check if all outputs are at EOF
        for (i = 0; i < src->nb_outputs; i++) {
            if (src->outputs[i] && ((FilterLinkInternal*)src->outputs[i])->status_in != AVERROR_EOF
                && ((FilterLinkInternal*)src->outputs[i])->status_out != AVERROR_EOF) {
                all_eof = false;
                break;
            }
        }

        av_opt_get_array(src, "map_array", AV_OPT_SEARCH_CHILDREN,
            0, src->nb_outputs, AV_OPT_TYPE_INT, map);

        // count enabled outputs
        map_on_count = 0;
        for (i = 0; i < src->nb_outputs; i++) {
            if (map[i] == ROUTE_ON)
                map_on_count++;
        }

        // 3. handle multiple enabled outputs
        if (map_on_count > 1 && all_eof) {
            enabled_count = 0;
            for (i = 0; i < src->nb_outputs && enabled_count < MAX_LINKS; i++) {
                if (map[i] == ROUTE_ON && src->outputs[i]) {
                    enabled_outputs[enabled_count++] = src->outputs[i];
                }
            }

            ret = audio_handle_multi_outputs(src, enabled_count, enabled_outputs);
            if (ret < 0)
                MEDIA_WARN("Failed to handle multiple outputs for '%s'", src->name);
        }
        // 4. handle single or no enabled outputs
        else {
            for (i = 0; i < src->nb_outputs; i++) {
                if (map[i] != ROUTE_ON || !src->outputs[i])
                    continue;

                AVFilterLink* link = src->outputs[i];

                // handle third-stage links (links with destination that has outputs)
                if (link->dst && link->dst->nb_outputs > 0) {
                    av_opt_get_array(link->dst, "map_array", AV_OPT_SEARCH_CHILDREN,
                        0, link->dst->nb_outputs, AV_OPT_TYPE_INT, dst_map);

                    for (j = 0; j < link->dst->nb_outputs; j++) {
                        if (dst_map[j] != ROUTE_ON || !link->dst->outputs[j])
                            continue;

                        ret = audio_negotiate_three_stage_link(link, link->dst->outputs[j]);
                        if (ret < 0)
                            MEDIA_WARN("Three-stage negotiation failed for '%s'", link->dst->name);
                    }
                }

                // handle direct links
                else {
                    AVFilterFormatsConfig config = { 0 };
                    int fmt = -1, rate = -1, ch = -1;

                    audio_calculate_intersection(&link->outcfg, &link->incfg, &config);
                    audio_select_min_values(link, &config, &fmt, &rate, &ch);

                    if (fmt > 0 && rate > 0 && ch > 0) {
                        ret = audio_set_format_config(link, fmt, rate, ch);
                        if (ret < 0) {
                            MEDIA_WARN("Failed to set format config for direct link");
                        }
                    }

                    audio_free_formats_config(&config);
                }
            }
        }
    }

    return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int audio_formats_transfer(AVFilterContext* filter)
{
    int ret;

    ret = audio_query_all_filters_formats(filter, filter->graph->nb_filters);
    if (ret < 0)
        return ret;

    return audio_transfer_formats_from_source(filter);
}