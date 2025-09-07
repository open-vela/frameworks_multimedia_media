/****************************************************************************
 * frameworks/media/audio_negotiation.h
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
#ifndef MEDIA_NEGOTIATION_H
#define MEDIA_NEGOTIATION_H

/**
 * @brief Initiates the audio format negotiation and propagation process in the filter graph.
 *
 * This function performs format negotiation starting from the given filter node,
 * queries supported formats of all connected filters, and then propagates format
 * constraints from source filters (e.g., abuffer) toward sinks to establish a
 * consistent data format across the entire audio processing graph.
 *
 * The process includes:
 *   1. Traversing the filter graph to collect format capabilities.
 *   2. Resolving compatible sample format, sample rate, and channel layout.
 *   3. Propagating negotiated formats from sources to sinks.
 *
 * @param filter A pointer to an AVFilterContext within the graph. The negotiation
 *               starts from this node and covers the entire connected subgraph.
 *
 * @return 0 on success, negative AVERROR code on failure.
 *         Common errors:
 *         - AVERROR(ENOMEM): Memory allocation failed.
 *         - AVERROR(EAGAIN): Format negotiation could not be completed (try again).
 *         - AVERROR(ENOSYS): A filter lacks format negotiation support.
 *
 * @note This function is typically called during graph configuration, before audio
 *       processing begins. It must be called after all filters are linked.
 *
 * @see audio_negotiate_formats_init
 * @see audio_negotiate_src
 */

int audio_negotiation_trigger(AVFilterContext* filter);

#endif // MEDIA_NEGOTIATION_H
