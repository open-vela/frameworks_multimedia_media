############################################################################
# multimedia/media/scripts/gdb/mediagraph.py
#
# SPDX-License-Identifier: Apache-2.0
#
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.  The
# ASF licenses this file to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance with the
# License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations
# under the License.
#
############################################################################

import gdb
from nxgdb import utils

g_sample_fmt = {
    -1: "none",
    0: "u8",
    1: "s16",
    2: "s32",
    3: "flt",
    4: "dbl",
    5: "u8p",
    6: "s16p",
    7: "s32p",
    8: "fltp",
    9: "dblp",
    10: "s64",
    11: "s64p",
    12: "nb",
    2147483647: "MAX",
}

"""graph class"""


class MediaGraphHander:

    def __init__(self, graph: gdb.Value):
        if not isinstance(graph, gdb.Value):
            raise ValueError("hander must be gdb.Value")
        self.hander = graph
        # Try to get AVFilterGraph from MediaGraphPriv
        try:
            self.priv = graph.cast(gdb.lookup_type("MediaGraphPriv").pointer())
            self.filter_graph = self.priv["graph"]
        except:
            # Direct AVFilterGraph
            self.filter_graph = graph.cast(gdb.lookup_type("AVFilterGraph").pointer())

    def link_filter_dump(self, link: gdb.Value) -> str:
        """Format link info like audio_graph_dump_link"""
        if not link:
            return 0, ""

        # Get FilterLinkInternal for status info
        try:
            link_internal = link.cast(gdb.lookup_type("FilterLinkInternal").pointer())
            status_out = int(link_internal["status_out"])
            status_in = int(link_internal["status_in"])

            fifo = link_internal["fifo"]
            fifo_queued = int(fifo["queued"])

            # Get frame_count_out from FilterLink inside FilterLinkInternal
            filter_link = link_internal["l"]
            frame_count_in = int(filter_link["frame_count_in"])
            frame_count_out = int(filter_link["frame_count_out"])
            frame_wanted_out = int(link_internal["frame_wanted_out"])
        except:
            # Fallback
            status_out = 0
            status_in = 0
            fifo_queued = 0
            frame_count_in = 0
            frame_count_out = 0
            frame_wanted_out = 0

        # Media type
        media_type = int(link["type"])
        if media_type == 1:  # Audio
            format_str = g_sample_fmt.get(int(link["format"]), "?")
            sample_rate = int(link["sample_rate"])
            nb_ch = int(link["ch_layout"]["nb_channels"])

            if nb_ch == 2:
                ch_str = "stereo"
            elif nb_ch == 1:
                ch_str = "mono"
            else:
                ch_str = f"{nb_ch} channels"

            # Format like audio_graph.c
            if sample_rate == 0 and status_in != 0:
                fmt = f"[0Hz ?: status_in:{status_in} status_out: {status_out} fifo:{fifo_queued} wt:{frame_wanted_out} icnt:{frame_count_in} ocnt:{frame_count_out} {ch_str}]"
            else:
                fmt = f"[{sample_rate}Hz {format_str}: status_in:{status_in} status_out: {status_out} fifo:{fifo_queued} wt:{frame_wanted_out} icnt:{frame_count_in} ocnt:{frame_count_out} {ch_str}]"

            return len(fmt), fmt
        else:
            return 0, "[?]"

    def dump_graph(self) -> list:
        if not self.filter_graph:
            return None

        dump_result = []
        filters = utils.ArrayIterator(self.filter_graph["filters"], self.filter_graph["nb_filters"])

        for filter in filters:
            filter_name = filter["name"].string()
            filter_type = filter["filter"]["name"].string()

            # Show filter's priv pointer inside the box (if present)
            filter_priv_str = ""
            try:
                filter_priv = filter["priv"]
                if filter_priv:
                    filter_priv_str = str(filter_priv)
            except:
                filter_priv_str = ""

            # Calculate max widths for this filter
            max_src_name = 0
            max_dst_name = 0
            max_in_name = 0
            max_out_name = 0
            max_in_fmt = 0
            max_out_fmt = 0

            # Process inputs
            for j in range(int(filter["nb_inputs"])):
                link = filter["inputs"][j]
                if not link:
                    continue

                src_name = link["src"]["name"].string()
                src_pad_name = link["srcpad"]["name"].string() if link["srcpad"] else "output0"
                dst_pad_name = link["dstpad"]["name"].string() if link["dstpad"] else "input0"

                src_name_len = len(src_name) + 1 + len(src_pad_name)
                max_src_name = max(max_src_name, src_name_len)
                max_in_name = max(max_in_name, len(dst_pad_name))

                fmt_len, _ = self.link_filter_dump(link)
                max_in_fmt = max(max_in_fmt, fmt_len)

            # Process outputs
            for j in range(int(filter["nb_outputs"])):
                link = filter["outputs"][j]
                if not link:
                    continue

                dst_name = link["dst"]["name"].string()
                src_pad_name = link["srcpad"]["name"].string() if link["srcpad"] else "output0"
                dst_pad_name = link["dstpad"]["name"].string() if link["dstpad"] else "input0"

                dst_name_len = len(dst_name) + 1 + len(dst_pad_name)
                max_dst_name = max(max_dst_name, dst_name_len)
                max_out_name = max(max_out_name, len(src_pad_name))

                fmt_len, _ = self.link_filter_dump(link)
                max_out_fmt = max(max_out_fmt, fmt_len)

            # Calculate indent and width
            in_indent = max_src_name + max_in_name + max_in_fmt
            if in_indent > 0:
                in_indent += 4

            filter_name_len = len(filter_name)
            filter_type_len = len(filter_type) + 2  # for parentheses
            filter_priv_len = len(filter_priv_str) if filter_priv_str else 0
            filter_width = max(filter_name_len + 2, filter_type_len + 4, filter_priv_len + 4)

            box_text_lines = 2 + (1 if filter_priv_str else 0)
            height = max(box_text_lines, int(filter["nb_inputs"]), int(filter["nb_outputs"]))

            # Check if this is a sink filter (no outputs)
            is_sink = int(filter["nb_outputs"]) == 0

            if is_sink:
                # Sink filter: left side has input info, right side has filter box
                # Calculate total width for alignment
                total_width = in_indent + filter_width

                # Top border
                dump_result.append(" " * in_indent + "+" + "-" * filter_width + "+")

                # Process each row
                for row in range(height):
                    # Calculate which input to show
                    in_no = row - (height - int(filter["nb_inputs"])) // 2

                    # Input side (left)
                    input_str = ""
                    if in_no >= 0 and in_no < int(filter["nb_inputs"]):
                        link = filter["inputs"][in_no]
                        if link:
                            src_name = link["src"]["name"].string()
                            src_pad_name = link["srcpad"]["name"].string() if link["srcpad"] else "output0"
                            dst_pad_name = link["dstpad"]["name"].string() if link["dstpad"] else "input0"

                            src_str = f"{src_name}:{src_pad_name}"
                            _, fmt_str = self.link_filter_dump(link)

                            # Calculate padding
                            e1 = max_src_name + 2
                            e2 = max_in_fmt + 2 + max_in_name - len(dst_pad_name)

                            input_str = src_str + "-" * (e1 - len(src_str)) + fmt_str + "-" * (e2 - len(fmt_str)) + dst_pad_name

                    # Filter part
                    filter_str = "|"

                    text_start = (height - box_text_lines) // 2
                    name_row = text_start
                    type_row = text_start + 1
                    priv_row = text_start + 2 if filter_priv_str else -1

                    if row == name_row:
                        x = (filter_width - filter_name_len) // 2
                        filter_str += " " * x + filter_name + " " * (filter_width - x - filter_name_len)
                    elif row == type_row:
                        type_str = f"({filter_type})"
                        x = (filter_width - len(type_str)) // 2
                        filter_str += " " * x + type_str + " " * (filter_width - x - len(type_str))
                    elif row == priv_row:
                        x = (filter_width - len(filter_priv_str)) // 2
                        filter_str += " " * x + filter_priv_str + " " * (filter_width - x - len(filter_priv_str))
                    else:
                        filter_str += " " * filter_width
                    filter_str += "|"

                    # Combine: input + filter
                    # Ensure the filter box keeps the same column even when this row has no input string.
                    full_line = input_str.ljust(in_indent) + filter_str
                    dump_result.append(full_line)

                # Bottom border
                dump_result.append(" " * in_indent + "+" + "-" * filter_width + "+")
                dump_result.append("")

            else:
                # Source filter: filter box on left, outputs on right
                # Top border
                dump_result.append("+" + "-" * filter_width + "+")

                # Process each row
                for row in range(height):
                    # Calculate which output to show
                    out_no = row - (height - int(filter["nb_outputs"])) // 2

                    # Filter part
                    filter_str = "|"

                    text_start = (height - box_text_lines) // 2
                    name_row = text_start
                    type_row = text_start + 1
                    priv_row = text_start + 2 if filter_priv_str else -1

                    if row == name_row:
                        x = (filter_width - filter_name_len) // 2
                        filter_str += " " * x + filter_name + " " * (filter_width - x - filter_name_len)
                    elif row == type_row:
                        type_str = f"({filter_type})"
                        x = (filter_width - len(type_str)) // 2
                        filter_str += " " * x + type_str + " " * (filter_width - x - len(type_str))
                    elif row == priv_row:
                        x = (filter_width - len(filter_priv_str)) // 2
                        filter_str += " " * x + filter_priv_str + " " * (filter_width - x - len(filter_priv_str))
                    else:
                        filter_str += " " * filter_width
                    filter_str += "|"

                    # Output side (right)
                    output_str = ""
                    if out_no >= 0 and out_no < int(filter["nb_outputs"]):
                        link = filter["outputs"][out_no]
                        if link:
                            dst_name = link["dst"]["name"].string()
                            src_pad_name = link["srcpad"]["name"].string() if link["srcpad"] else "output0"
                            dst_pad_name = link["dstpad"]["name"].string() if link["dstpad"] else "input0"

                            _, fmt_str = self.link_filter_dump(link)
                            dst_str = f"{dst_name}:{dst_pad_name}"

                            # Calculate padding
                            e1 = max_out_name + 2
                            e2 = max_out_fmt + 2 + max_dst_name - len(dst_str)

                            output_str = src_pad_name + "-" * (e1 - len(src_pad_name)) + fmt_str + "-" * (e2 - len(fmt_str)) + dst_str

                    # Combine
                    full_line = filter_str + output_str
                    dump_result.append(full_line)

                # Bottom border
                dump_result.append("+" + "-" * filter_width + "+")
                dump_result.append("")

        return dump_result