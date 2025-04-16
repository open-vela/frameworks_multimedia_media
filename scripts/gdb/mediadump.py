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

import mediagraph as MediaGraph  # noqa: E402


class MediaDump(gdb.Command):
    """This GDB command dumps MediaPoll and its handle to MediaGraphPriv
    when the provided argument matches the node's name."""

    ALL_MODULES = ["graph", "policy", "focus", "session", "server", "all"]

    def __init__(self):
        super(MediaDump, self).__init__("mediadump", gdb.COMMAND_USER)
        self.graph = None
        self.policy = None
        self.focus = None
        self.session = None
        self.server = None

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

        handle = None

        if args.name == "all":
            for i in range(array_size):
                if g_media[i].type.has_key("handle"):
                    self.media_dump_func(
                        g_media[i]["handle"], g_media[i]["name"].string()
                    )
                else:
                    self.media_dump_func(
                        g_media[i]["priv"], g_media[i]["name"].string()
                    )
            return
        else:
            name = f"media_{args.name}"
            for i in range(array_size):
                if g_media[i]["name"].string() == name:
                    handle = g_media[i]["handle"]
                    try:
                        self.media_dump_func(handle, name)
                    except gdb.error as e:
                        gdb.write(f"dump {name} failed: {e}\n")
                        pass
                    break

        if not handle:
            gdb.write(f"Error: No media node found with the name '{arg}'.\n")

    def media_dump_func(self, handle, name):
        dump_functions = {
            "media_graph": self.dump_media_graph,
            "media_policy": self.dump_media_policy,
            "media_focus": self.dump_media_focus,
            "media_session": self.dump_media_session,
            "media_server": self.dump_media_server,
        }
        dump_func = dump_functions.get(name)
        if dump_func:
            dump_func(handle)
        else:
            raise gdb.GdbError(f"Error: '{name}' does not matched any dump_fuction.")

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
            raise gdb.GdbError("dump graph failed.")
        gdb.write("Media Graph Dump:\n")
        for line in result:
            gdb.write(f"  {line}\n")

    def dump_media_policy(self, handle):
        gdb.write("\n")
        gdb.write(f"Node policy handle: {handle}\n")

    def dump_media_session(self, handle):
        gdb.write("\n")
        gdb.write(f"Node session handle: {handle}\n")

    def dump_media_server(self, handle):
        gdb.write("\n")
        gdb.write(f"Node server handle: {handle}\n")

    def dump_media_focus(self, handle):
        gdb.write("\n")
        gdb.write(f"Node focus handle: {handle}\n")


MediaDump()
