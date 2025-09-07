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

static void audio_get_min_format(AVFilterFormatsConfig* config, int* fmt)
{
    if (config->formats && config->formats->nb_formats > 0) {
        *fmt = config->formats->formats[0];
        for (int i = 1; i < config->formats->nb_formats; i++)
            *fmt = FFMIN(*fmt, config->formats->formats[i]);
    }
}

static void audio_get_min_samplerate(AVFilterFormatsConfig* config, int* rate)
{
    if (config->samplerates && config->samplerates->nb_formats > 0) {
        *rate = config->samplerates->formats[0];
        for (int i = 1; i < config->samplerates->nb_formats; i++)
            *rate = FFMIN(*rate, config->samplerates->formats[i]);
    }
}

static void audio_get_min_channels(AVFilterFormatsConfig* config, int* ch)
{
    if (config->channel_layouts && config->channel_layouts->nb_channel_layouts > 0) {
        *ch = config->channel_layouts->channel_layouts[0].nb_channels;
        for (int i = 1; i < config->channel_layouts->nb_channel_layouts; i++)
            *ch = FFMIN(*ch, config->channel_layouts->channel_layouts[i].nb_channels);
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
    int map[MAX_LINKS];
    AVFilterLink* link;
    int stack_size = 0;
    int i, j, ret;

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

                for (j = 0; j < link->src->nb_outputs; j++) {
                    if (link->src->outputs[j] == link)
                        break;
                }

                av_opt_get_array(link->src, "map_array", AV_OPT_SEARCH_CHILDREN,
                    0, link->src->nb_outputs, AV_OPT_TYPE_INT, map);

                if (map[j] != ROUTE_ON) {
                    MEDIA_INFO("Skipping query for input[%d] of '%s' (src output[%d] map=OFF)\n",
                        i, current->name, j);
                    continue;
                }

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
    AVFilterFormatsConfig fcfg_src = { 0 }, fcfg_sink = { 0 }, fcfg_final = { 0 };
    int cand_fmt = -1, cand_rate = -1, cand_ch = -1;
    int temp_fmt = -1, temp_rate = -1, temp_ch = -1;
    int ds_fmt = -1, ds_rate = -1, ds_ch = -1;
    int fmt = -1, rate = -1, ch = -1;
    int dst_map[MAX_LINKS] = { 0 };
    AVFilterLink* link = NULL;
    int i = 0, j = 0, ret = 0;
    bool has_common = false;

    /* Strategy 1: Find the minimal common format intersection across all enabled outputs */
    for (i = 0; i < enabled_count; i++) {
        audio_calculate_intersection(&enabled_outputs[i]->outcfg, &enabled_outputs[i]->incfg, &fcfg_src);
        MEDIA_DEBUG("[Intersection Debug] After src_link (outcfg & incfg) for output %d:\n", i);
#if defined(CONFIG_MEDIA_LOG_DEBUG)
        audio_debug_print_formats_config(&fcfg_src, "    ");
#endif

        has_common = (fcfg_src.formats && fcfg_src.formats->nb_formats > 0)
            || (fcfg_src.samplerates && fcfg_src.samplerates->nb_formats > 0)
            || (fcfg_src.channel_layouts && fcfg_src.channel_layouts->nb_channel_layouts > 0);

        if (has_common) {
            if (fcfg_src.formats && fcfg_src.formats->nb_formats > 0) {
                audio_get_min_format(&fcfg_src, &temp_fmt);
                if (cand_fmt == -1 || temp_fmt < cand_fmt)
                    cand_fmt = temp_fmt;
            }

            if (fcfg_src.samplerates && fcfg_src.samplerates->nb_formats > 0) {
                audio_get_min_samplerate(&fcfg_src, &temp_rate);
                if (cand_rate == -1 || temp_rate < cand_rate)
                    cand_rate = temp_rate;
            }

            if (fcfg_src.channel_layouts && fcfg_src.channel_layouts->nb_channel_layouts > 0) {
                audio_get_min_channels(&fcfg_src, &temp_ch);
                if (cand_ch == -1 || temp_ch < cand_ch)
                    cand_ch = temp_ch;
            }

            MEDIA_DEBUG("Found common intersection for output %d: fmt=%s, rate=%d, ch=%d\n",
                i,
                cand_fmt != -1 ? av_get_sample_fmt_name(cand_fmt) : "N/A",
                cand_rate != -1 ? cand_rate : -1,
                cand_ch != -1 ? cand_ch : -1);

            // Update each parameter independently, without relying on a global found_common_min flag
            if (cand_fmt != -1 && (fmt == -1 || cand_fmt < fmt))
                fmt = cand_fmt;

            if (cand_rate != -1 && (rate == -1 || cand_rate < rate))
                rate = cand_rate;

            if (cand_ch != -1 && (ch == -1 || cand_ch < ch))
                ch = cand_ch;
        }
        audio_free_formats_config(&fcfg_src);
        memset(&fcfg_src, 0, sizeof(fcfg_src));

        cand_fmt = -1;
        cand_rate = -1;
        cand_ch = -1;
    }

    /* Strategy 2: For parameters without a common intersection,
       fall back to the minimal format from the first output's incfg */
    AVFilterFormatsConfig* first_cfg = &enabled_outputs[0]->incfg;

    if (fmt == -1) {
        MEDIA_INFO("No common sample format intersection found, using first output's min format.\n");
        if (first_cfg->formats && first_cfg->formats->nb_formats > 0)
            audio_get_min_format(first_cfg, &fmt);
    }

    if (rate == -1) {
        MEDIA_INFO("No common sample rate intersection found, using first output's min rate.\n");
        if (first_cfg->samplerates && first_cfg->samplerates->nb_formats > 0)
            audio_get_min_samplerate(first_cfg, &rate);
    }

    if (ch == -1) {
        MEDIA_INFO("No common channel layout intersection found, using first output's min channels.\n");
        if (first_cfg->channel_layouts && first_cfg->channel_layouts->nb_channel_layouts > 0)
            audio_get_min_channels(first_cfg, &ch);
    }

    /* Strategy 3: Apply the negotiated format to all enabled outputs */
    for (i = 0; i < enabled_count; i++) {
        ret = audio_set_format_config(enabled_outputs[i], fmt, rate, ch);
        if (ret < 0) {
            MEDIA_WARN("Failed to set common format on link %d from '%s'", i, src->name);
            continue;
        }

        /* Handle downstream link negotiation - using independent parameter negotiation strategy */
        if (enabled_outputs[i]->dst && enabled_outputs[i]->dst->nb_outputs > 0) {
            av_opt_get_array(enabled_outputs[i]->dst, "map_array", AV_OPT_SEARCH_CHILDREN,
                0, enabled_outputs[i]->dst->nb_outputs, AV_OPT_TYPE_INT, dst_map);

            cand_fmt = -1;
            cand_rate = -1;
            cand_ch = -1;

            for (j = 0; j < enabled_outputs[i]->dst->nb_outputs; j++) {
                if (dst_map[j] != ROUTE_ON)
                    continue;

                link = enabled_outputs[i]->dst->outputs[j];

                audio_calculate_intersection(&enabled_outputs[i]->outcfg, &enabled_outputs[i]->incfg, &fcfg_src);
                audio_calculate_intersection(&link->outcfg, &link->incfg, &fcfg_sink);

#if defined(CONFIG_MEDIA_LOG_DEBUG)
                MEDIA_DEBUG("[Intersection Debug] For downstream link %d:\n", j);
                MEDIA_DEBUG("[Intersection Debug] After src_link (outcfg & incfg):\n");
                audio_debug_print_formats_config(&fcfg_src, "    ");
                MEDIA_DEBUG("[Intersection Debug] After sink_link (outcfg & incfg):\n");
                audio_debug_print_formats_config(&fcfg_sink, "    ");
#endif

                audio_calculate_intersection(&fcfg_src, &fcfg_sink, &fcfg_final);

#if defined(CONFIG_MEDIA_LOG_DEBUG)
                MEDIA_DEBUG("[Intersection Debug] After final intersection (src_config & sink_config):\n");
                audio_debug_print_formats_config(&fcfg_final, "    ");
#endif

                /* Negotiate each format parameter independently */
                // 1. Sample format negotiation
                if (fcfg_final.formats && fcfg_final.formats->nb_formats > 0) {
                    temp_fmt = fcfg_final.formats->formats[0];
                    for (int k = 1; k < fcfg_final.formats->nb_formats; k++)
                        temp_fmt = FFMIN(temp_fmt, fcfg_final.formats->formats[k]);

                    if (cand_fmt == -1 || temp_fmt < cand_fmt)
                        cand_fmt = temp_fmt;
                } else if (fcfg_sink.formats && fcfg_sink.formats->nb_formats > 0) {
                    temp_fmt = fcfg_sink.formats->formats[0];
                    for (int k = 1; k < fcfg_sink.formats->nb_formats; k++)
                        temp_fmt = FFMIN(temp_fmt, fcfg_sink.formats->formats[k]);

                    if (cand_fmt == -1 || temp_fmt < cand_fmt)
                        cand_fmt = temp_fmt;
                } else {
                    audio_get_min_format(&link->incfg, &temp_fmt);
                    if (cand_fmt == -1 || temp_fmt < cand_fmt)
                        cand_fmt = temp_fmt;
                }

                // 2. Sample rate negotiation
                if (fcfg_final.samplerates && fcfg_final.samplerates->nb_formats > 0) {
                    temp_rate = fcfg_final.samplerates->formats[0];
                    for (int k = 1; k < fcfg_final.samplerates->nb_formats; k++)
                        temp_rate = FFMIN(temp_rate, fcfg_final.samplerates->formats[k]);

                    if (cand_rate == -1 || temp_rate < cand_rate)
                        cand_rate = temp_rate;

                } else if (fcfg_sink.samplerates && fcfg_sink.samplerates->nb_formats > 0) {
                    temp_rate = fcfg_sink.samplerates->formats[0];
                    for (int k = 1; k < fcfg_sink.samplerates->nb_formats; k++)
                        temp_rate = FFMIN(temp_rate, fcfg_sink.samplerates->formats[k]);

                    if (cand_rate == -1 || temp_rate < cand_rate)
                        cand_rate = temp_rate;
                } else {
                    audio_get_min_samplerate(&link->incfg, &temp_rate);
                    if (cand_rate == -1 || temp_rate < cand_rate)
                        cand_rate = temp_rate;
                }

                // 3. Channel count negotiation
                if (fcfg_final.channel_layouts && fcfg_final.channel_layouts->nb_channel_layouts > 0) {
                    temp_ch = fcfg_final.channel_layouts->channel_layouts[0].nb_channels;
                    for (int k = 1; k < fcfg_final.channel_layouts->nb_channel_layouts; k++)
                        temp_ch = FFMIN(temp_ch, fcfg_final.channel_layouts->channel_layouts[k].nb_channels);

                    if (cand_ch == -1 || temp_ch < cand_ch)
                        cand_ch = temp_ch;
                } else if (fcfg_sink.channel_layouts && fcfg_sink.channel_layouts->nb_channel_layouts > 0) {
                    temp_ch = fcfg_sink.channel_layouts->channel_layouts[0].nb_channels;
                    for (int k = 1; k < fcfg_sink.channel_layouts->nb_channel_layouts; k++)
                        temp_ch = FFMIN(temp_ch, fcfg_sink.channel_layouts->channel_layouts[k].nb_channels);

                    if (cand_ch == -1 || temp_ch < cand_ch)
                        cand_ch = temp_ch;
                } else {
                    audio_get_min_channels(&link->incfg, &temp_ch);
                    if (cand_ch == -1 || temp_ch < cand_ch)
                        cand_ch = temp_ch;
                }

                MEDIA_DEBUG("Downstream intersection found for output %d, link %d: fmt=%s, rate=%d, ch=%d\n",
                    i, j,
                    cand_fmt != -1 ? av_get_sample_fmt_name(cand_fmt) : "N/A",
                    cand_rate != -1 ? cand_rate : -1,
                    cand_ch != -1 ? cand_ch : -1);

                audio_free_formats_config(&fcfg_src);
                audio_free_formats_config(&fcfg_sink);
                audio_free_formats_config(&fcfg_final);
                memset(&fcfg_src, 0, sizeof(fcfg_src));
                memset(&fcfg_sink, 0, sizeof(fcfg_sink));
                memset(&fcfg_final, 0, sizeof(fcfg_final));
            }

            ds_fmt = cand_fmt != -1 ? cand_fmt : ds_fmt;
            ds_rate = cand_rate != -1 ? cand_rate : ds_rate;
            ds_ch = cand_ch != -1 ? cand_ch : ds_ch;

            MEDIA_DEBUG("Downstream negotiated format for output %d: fmt=%s, rate=%d, ch=%d\n",
                i,
                ds_fmt != -1 ? av_get_sample_fmt_name(ds_fmt) : "N/A",
                ds_rate != -1 ? ds_rate : -1,
                ds_ch != -1 ? ds_ch : -1);

            for (j = 0; j < enabled_outputs[i]->dst->nb_outputs; j++) {
                if (dst_map[j] != ROUTE_ON)
                    continue;

                ret = audio_set_format_config(enabled_outputs[i]->dst->outputs[j], ds_fmt, ds_rate, ds_ch);
                if (ret < 0)
                    MEDIA_ERR("Failed to set downstream format on link %d from '%s'", j, enabled_outputs[i]->dst->name);
            }
        }
    }

    audio_free_formats_config(&fcfg_src);
    audio_free_formats_config(&fcfg_sink);
    audio_free_formats_config(&fcfg_final);

    return 0;
}

static int audio_negotiate_src(AVFilterContext* filter)
{
    int src_count = 0, stack_top = 0, enabled_count = 0;
    AVFilterLink* enabled_outputs[MAX_LINKS] = { 0 };
    AVFilterContext* srcs[MAX_LINKS] = { 0 };
    AVFilterContext* stack[MAX_LINKS] = { 0 };
    int map[MAX_LINKS] = { 0 };
    int i, s;

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

        av_opt_get_array(src, "map_array", AV_OPT_SEARCH_CHILDREN,
            0, src->nb_outputs, AV_OPT_TYPE_INT, map);

        enabled_count = 0;
        for (i = 0; i < src->nb_outputs; i++) {
            if (map[i] != ROUTE_ON)
                continue;

            enabled_outputs[enabled_count++] = src->outputs[i];
        }

        if (enabled_count)
            audio_handle_multi_outputs(src, enabled_count, enabled_outputs);
    }

    return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int audio_negotiation_trigger(AVFilterContext* filter)
{
    int ret;

    ret = audio_negotiate_formats_init(filter);
    if (ret < 0)
        return ret;

    return audio_negotiate_src(filter);
}