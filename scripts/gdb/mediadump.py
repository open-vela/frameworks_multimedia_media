############################################################################
# multimedia/media/scripts/gdb/mediadump.py
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

import argparse
import os
import sys

import gdb
from nxgdb import utils

sys.path.append(os.path.dirname(os.path.abspath(__file__)))

import mediagraph as MediaGraph
import mediapolicy as MediaPolicy
import mediaplayer as MediaPlayer
import mediarecorder as MediaRecorder


class MediaDump(gdb.Command):
    """This GDB command dumps MediaPoll and its handle to MediaGraphPriv
    when the provided argument matches the node's name."""

    ALL_MODULES = ["policy", "graph", "player", "recorder", "all"]

    def __init__(self):
        super(MediaDump, self).__init__("mediadump", gdb.COMMAND_USER)
        self.graph = None
        self.policy = None
        self.player = None
        self.recorder = None

    def invoke(self, arg, from_tty):
        parser = argparse.ArgumentParser(description="MediaDump command options.")
        parser.add_argument(
            "name",
            nargs="?",
            default="all",
            choices=self.ALL_MODULES,
            help="Name of the media node to dump. if not provided, all modules will be dumped.",
        )

        try:
            args = parser.parse_args(arg.split())
        except SystemExit:
            return

        try:
            g_media = gdb.parse_and_eval("g_media")
            array_size = utils.nitems(g_media)
        except gdb.error as e:
            gdb.write(f"Error accessing g_media: {e}\n")
            return

        # Mapping from CLI argument to plugin name in g_media
        plugin_map = {
            "policy": "media_policy",
            "graph": "audio_graph",
            "player": "media_player",
            "recorder": "media_recorder",
        }

        target_plugins = []
        if args.name == "all":
            # Order matters here: policy -> graph -> player -> recorder
            target_plugins = ["media_policy", "audio_graph", "media_player", "media_recorder"]
        elif args.name in plugin_map:
            target_plugins = [plugin_map[args.name]]
        else:
            # Should be covered by argparse choices, but safe fallback
            gdb.write(f"Unknown module: {args.name}\n")
            return

        # Collect all matching nodes first
        found_nodes = {}
        for i in range(array_size):
            try:
                name_val = g_media[i]["name"]
                if not name_val:
                    continue
                plugin_name = name_val.string()

                if plugin_name in target_plugins:
                    found_nodes[plugin_name] = g_media[i]["priv"]
            except gdb.error:
                continue

        if not found_nodes:
            gdb.write(f"No matching media nodes found for '{args.name}'.\n")
            return

        # Dump in the order specified in target_plugins
        for plugin_name in target_plugins:
            if plugin_name in found_nodes:
                try:
                    self.media_dump_func(found_nodes[plugin_name], plugin_name)
                except gdb.error as e:
                    gdb.write(f"dump {plugin_name} failed: {e}\n")

    def media_dump_func(self, handle, name):
        dump_functions = {
            "media_policy": self.dump_media_policy,
            "audio_graph": self.dump_media_graph,
            "media_player": self.dump_media_player,
            "media_recorder": self.dump_media_recorder,
        }
        dump_func = dump_functions.get(name)
        if dump_func:
            dump_func(handle)

    def dump_media_graph(self, handle):
        gdb.write("\n")
        gdb.write(f"Node graph handle: {handle}\n")

        try:
            self.graph = MediaGraph.MediaGraphHander(handle)
        except (gdb.error, gdb.GdbError) as e:
            gdb.write(f"Error analyze media_graph: {e}\n")
            return

        result = self.graph.dump_graph()
        if not result:
            gdb.write("dump graph failed.\n")
            return

        gdb.write("Media Graph Dump:\n")
        for line in result:
            gdb.write(f"{line}\n")

    def dump_media_policy(self, handle):
        gdb.write("\n")
        gdb.write(f"Node policy handle: {handle}\n")

        try:
            self.policy = MediaPolicy.MediaPolicyHandler(handle)
        except (gdb.error, gdb.GdbError) as e:
            gdb.write(f"Error analyze media_policy: {e}\n")
            return

        result = self.policy.dump_policy()
        if not result:
            gdb.write("Media Policy Dump failed.\n")
            return

        gdb.write("Media Policy Dump:\n")
        for line in result:
            gdb.write(f"{line}\n")

    def dump_media_player(self, handle):
        gdb.write("\n")
        gdb.write(f"Node player handle: {handle}\n")

        try:
            self.player = MediaPlayer.MediaPlayerHandler(handle)
        except (gdb.error, gdb.GdbError) as e:
            gdb.write(f"Error analyze media_player: {e}\n")
            return

        result = self.player.dump_player()
        if not result:
            gdb.write("Media Player Dump failed.\n")
            return

        gdb.write("Media Player Dump:\n")
        for line in result:
            gdb.write(f"{line}\n")

    def dump_media_recorder(self, handle):
        gdb.write("\n")
        gdb.write(f"Node recorder handle: {handle}\n")

        try:
            self.recorder = MediaRecorder.MediaRecorderHandler(handle)
        except (gdb.error, gdb.GdbError) as e:
            gdb.write(f"Error analyze media_recorder: {e}\n")
            return

        result = self.recorder.dump_recorder()
        if not result:
            gdb.write("Media Recorder Dump failed.\n")
            return

        gdb.write("Media Recorder Dump:\n")
        for line in result:
            gdb.write(f"{line}\n")


MediaDump()