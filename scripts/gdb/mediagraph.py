############################################################################
# multimedia/media/scripts/gdb/utils.py
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

import avfilter_function as avf
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
        self.priv = graph.cast(gdb.lookup_type("MediaGraphPriv").pointer())
        self.split_pipeline = []
        self.worning = []
        self.splite_graph()

    def splite_graph(self):
        graph = self.priv["graph"]
        if not self.priv:
            raise gdb.GdbError("media_graph is not init")

        for filter in utils.ArrayIterator(graph["filters"], graph["nb_filters"]):
            if int(filter["nb_inputs"]) == 0:
                pipe_line = []
                filters = {}
                self.splite_graph_in_link(filter, pipe_line, filters)
                self.split_pipeline.append([pipe_line, filters])

    def splite_graph_in_link(self, filter: gdb.Value, pipeline: list, filters: dict):
        filters[filter["name"].string()] = filter

        for i in range(int(filter["nb_outputs"])):
            output_link = filter["outputs"][i]
            dst_filter = output_link["dst"]
            link_status = self.link_filter_dump(output_link)
            pipeline.append([output_link, link_status])

            if int(dst_filter["nb_outputs"]) != 0:
                self.splite_graph_in_link(dst_filter, pipeline, filters)
            else:
                filters[dst_filter["name"].string()] = dst_filter
                pipeline.append("none")

    def link_filter_dump(self, link: gdb.Value) -> dict:
        link_stats = {}
        link_stats["name"] = "linkfilter"
        link_stats["src"] = link["src"]
        link_stats["dst"] = link["dst"]
        if link["type"] == 0:
            link_stats["type"] = "video"
        elif link["type"] == 1:
            link_stats["type"] = "audio"
        else:
            link_stats["type"] = "unknown"
        link_stats["w"] = link["w"]
        link_stats["h"] = link["h"]
        link_stats["sample_aspect_ratio"] = link["sample_aspect_ratio"]
        link_stats["format"] = g_sample_fmt.get(int(link["format"]))
        link_stats["channels"] = link["ch_layout"]["nb_channels"]
        link_stats["sample_rate"] = link["sample_rate"]
        link_stats["codec"] = link["codec"]
        link_stats["incfg"] = link["incfg"]
        link_stats["outcfg"] = link["outcfg"]
        link_stats["incfg-outcfg-st"] = (
            ("1" if link["incfg"]["formats"] else "0")
            + "-"
            + ("1" if link["outcfg"]["formats"] else "0")
        )
        link_stats["frame_count_in"] = link["frame_count_in"]
        link_stats["frame_wanted_out"] = link["frame_wanted_out"]
        link_stats["time_base"] = link["time_base"]
        link_stats["current_pts"] = link["current_pts"]
        if link.type.has_key("status_out"):
            link_stats["status_out"] = link["status_out"]
            link_stats["status_in"] = link["status_in"]
            link_stats["fifo_queued_frame"] = link["fifo"]["queued"] - 1
            link_stats["fifo_queued_sample"] = int(
                link["fifo"]["total_samples_head"] - link["fifo"]["total_samples_tail"]
            )
            link_stats["fifo_size"] = link["fifo"]["allocated"]
        else:
            priv_data = link["reserved"]
            offset = (
                utils.lookup_type("FFFrameQueue").sizeof
                + utils.lookup_type("int").sizeof
            )
            link_stats["status_in"] = priv_data[offset].cast(gdb.lookup_type("int"))
            offset += (
                utils.lookup_type("int").sizeof + utils.lookup_type("int64_t").sizeof
            )
            link_stats["status_out"] = priv_data[offset].cast(gdb.lookup_type("int"))
            fifo = (
                gdb.Value(priv_data.address)
                .cast(utils.lookup_type("FFFrameQueue").pointer())
                .dereference()
            )
            link_stats["fifo_queued_frame"] = (
                fifo["queued"] if fifo["queued"] == 0 else fifo["queued"] - 1
            )
            link_stats["fifo_queued_sample"] = int(
                fifo["total_samples_head"] - fifo["total_samples_tail"]
            )
            link_stats["fifo_size"] = fifo["allocated"]
        return link_stats

    def dump_graph(self) -> list:
        if not self.split_pipeline:
            return None

        dump_result = []
        for pipeline in self.split_pipeline:
            sub_pipeline = pipeline[0]
            filters = pipeline[1]
            pipeline_name = None
            pre_link = None
            for filter in filters.keys():
                if "movie_async" in filter or "devsrc" in filter:
                    pipeline_name = filter
                    break

            if not pipeline_name:
                pipeline_name = "error pipeline"

            dump_result.append(f"Pipeline Name: {pipeline_name}")

            for link in sub_pipeline:
                if link == "none":
                    if pre_link is None:
                        gdb.write("WARNING: graph nodes with unknown structure\n")
                        continue

                    final_filter = pre_link[1]["dst"]
                    if filter_func := avf.get_filter_func(
                        filters.get(final_filter["name"].string())
                    ):
                        filter_status = []
                        filter_func(hander=final_filter, dump=filter_status)
                        dump_result.append(
                            f"{final_filter['name'].string():<{25}} ex: {filter_status}\n"
                        )
                    else:
                        dump_result.append(
                            f"{final_filter['name'].string():<{25}} ex: unknown filter\n"
                        )
                    continue

                src = link[1]["src"]["name"].string()
                dst = link[1]["dst"]["name"].string()
                f = link[1]["incfg-outcfg-st"]
                fmt = link[1]["format"]
                sr = int(link[1]["sample_rate"])
                cl = int(link[1]["channels"])
                st = (
                    ("1" if int(link[1]["status_in"]) == 0 else "0")
                    + "-"
                    + ("1" if int(link[1]["status_out"]) == 0 else "0")
                )
                fwn = int(link[1]["frame_wanted_out"])
                cnt = int(link[1]["frame_count_in"])
                cur = f"{link[1]['fifo_queued_frame']}-{link[1]['fifo_queued_sample']}"
                ex = ""

                if filter_func := avf.get_filter_func(filters.get(src)):
                    filter_status = []
                    filter_func(hander=link[1]["src"], dump=filter_status)
                    for i in range(len(filter_status)):
                        ex += filter_status[i] + " "
                else:
                    ex += "unknown filter"

                status = (
                    f"{src:<{25}}"
                    + "->"
                    + f"{dst:<{25}}"
                    + f" f:{f}"
                    + " fmt:"
                    + f"{fmt:<{5}}"
                    + " sr:"
                    + f"{sr:<{6}}"
                    + " cls:"
                    + f"{cl:<{2}}"
                    + f" st:{st}"
                    + f" wn:{fwn}"
                    + " cnt:"
                    + f"{cnt:<{7}}"
                    + f" cur:{cur}"
                    + f" ex:{ex}"
                )
                dump_result.append(status)

                pre_link = link

        return dump_result
