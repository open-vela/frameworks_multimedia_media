############################################################################
# multimedia/media/scripts/gdb/mediaplayer.py
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
import media_utils


class MediaPlayerHandler:
    def __init__(self, handle):
        self.priv = None
        self.priv_type = None
        self.utils = media_utils.MediaUtils()

        # Try to find MediaPlayerPriv type
        for tname in (
            "MediaPlayerPriv",
            "struct MediaPlayerPriv",
            "media_player_priv",
            "struct media_player_priv",
        ):
            try:
                self.priv_type = gdb.lookup_type(tname).pointer()
                break
            except gdb.error:
                continue

        try:
            self.priv = handle.cast(self.priv_type) if self.priv_type else handle
        except gdb.error:
            self.priv = handle

        # Try to find MediaPlayerState enum values
        try:
            self.idle_state = int(gdb.parse_and_eval("MEDIA_PLAYER_STATE_IDLE"))
        except gdb.error:
            self.idle_state = 0

    def _get_max_cnt(self):
        try:
            return int(gdb.parse_and_eval("CONFIG_MEDIA_PLAYER_MAX_CNT"))
        except gdb.error:
            pass

        try:
            ctxs = self.priv["ctxs"]
            return int(ctxs.type.sizeof // ctxs[0].type.sizeof)
        except Exception:
            return 16  # Default fallback

    def dump_player(self):
        if not self.priv:
            return ["player priv is null"]

        try:
            ctxs = self.priv["ctxs"]
        except gdb.error:
            return ["player ctxs not readable"]

        ctx_cnt = self._get_max_cnt()
        if ctx_cnt <= 0:
            return ["player ctxs count <= 0"]

        lines = []
        lines.append("\n--------------player dump start-------------")

        active_players = 0
        for i in range(ctx_cnt):
            try:
                ctx = ctxs[i]
            except gdb.error:
                continue

            try:
                state = int(ctx["state"])
            except gdb.error:
                state = -1

            if state == self.idle_state:
                continue

            active_players += 1
            name = self.utils.safe_string(ctx["name"], "?")

            # Base info: player[i, name] state:state
            parts = [f"player[{i}, {name}] state:{state}"]

            # Audio info
            try:
                audio_stream = ctx["audio_stream"]
                if audio_stream and audio_stream["codec_ctx"]:
                    codec_ctx = audio_stream["codec_ctx"]
                    codec_id = int(codec_ctx["codec_id"])
                    bit_rate = int(codec_ctx["bit_rate"])
                    sample_rate = int(codec_ctx["sample_rate"])
                    channels = int(codec_ctx["ch_layout"]["nb_channels"])

                    queue_cnt = self.utils.get_queue_count(audio_stream)
                    try:
                        aframe_cnt = int(ctx["aframe_cnt"])
                    except gdb.error:
                        aframe_cnt = 0

                    # Format: a: type codec_id bit_rate sample_rate ch:channels queue_cnt aframe_cnt
                    # C code: a: %d %s %" PRId64 " %d ch:%d %d %" PRIu32 ""
                    # We use codec_id instead of name
                    codec_name = self.utils.get_codec_name(codec_id)
                    parts.append(f"a: 1 {codec_name} {bit_rate} {sample_rate} ch:{channels} {queue_cnt} {aframe_cnt}")
            except gdb.error:
                pass

            # Video info
            try:
                video_stream = ctx["video_stream"]
                if video_stream and video_stream["codec_ctx"]:
                    codec_ctx = video_stream["codec_ctx"]
                    codec_id = int(codec_ctx["codec_id"])
                    width = int(codec_ctx["width"])
                    height = int(codec_ctx["height"])

                    queue_cnt = self.utils.get_queue_count(video_stream)
                    try:
                        vframe_cnt = int(ctx["vframe_cnt"])
                    except gdb.error:
                        vframe_cnt = 0

                    # Format: v: type codec_id widthxheight queue_cnt vframe_cnt
                    # C code: v: %d %s %dx%d %d %" PRIu32 ""
                    codec_name = self.utils.get_codec_name(codec_id)
                    parts.append(f"v: 0 {codec_name} {width}x{height} {queue_cnt} {vframe_cnt}")
            except gdb.error:
                pass

            lines.append(", ".join(parts))

        if active_players == 0:
            # If no active players, we might want to indicate that, or just print start/end like C code
            pass

        lines.append("--------------player dump end---------------")
        return lines
