############################################################################
# multimedia/media/scripts/gdb/mediarecorder.py
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


class MediaRecorderHandler:
    def __init__(self, handle):
        self.priv = None
        self.priv_type = None
        self.utils = media_utils.MediaUtils()

        # Try to find MediaRecorderPriv type
        for tname in (
            "MediaRecorderPriv",
            "struct MediaRecorderPriv",
            "media_recorder_priv",
            "struct media_recorder_priv",
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

        # Try to find MediaRecorderState enum values
        try:
            self.idle_state = int(gdb.parse_and_eval("MEDIA_RECORDER_STATE_IDLE"))
        except gdb.error:
            self.idle_state = 0

    def _get_max_cnt(self):
        try:
            return int(gdb.parse_and_eval("CONFIG_MEDIA_RECORDER_MAX_CNT"))
        except gdb.error:
            pass

        try:
            ctxs = self.priv["ctxs"]
            return int(ctxs.type.sizeof // ctxs[0].type.sizeof)
        except Exception:
            return 16  # Default fallback

    def dump_recorder(self):
        if not self.priv:
            return ["recorder priv is null"]

        try:
            ctxs = self.priv["ctxs"]
        except gdb.error:
            return ["recorder ctxs not readable"]

        ctx_cnt = self._get_max_cnt()
        if ctx_cnt <= 0:
            return ["recorder ctxs count <= 0"]

        lines = []
        lines.append("\n--------------recorder dump start-------------")

        active_recorders = 0
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

            active_recorders += 1
            name = self.utils.safe_string(ctx["name"], "?")

            # Base info: recorder[i, name] state:state
            parts = [f"recorder[{i}, {name}] state:{state}"]

            # Audio info
            try:
                audio_idx = int(ctx["audio_idx"])
                if audio_idx >= 0:
                    streams = ctx["streams"]
                    audio_stream = streams[audio_idx]
                    if audio_stream and audio_stream["enc_ctx"]:
                        enc_ctx = audio_stream["enc_ctx"]
                        codec_id = int(enc_ctx["codec_id"])
                        bit_rate = int(enc_ctx["bit_rate"])
                        sample_rate = int(enc_ctx["sample_rate"])
                        channels = int(enc_ctx["ch_layout"]["nb_channels"])

                        queue_cnt = self.utils.get_queue_count(audio_stream)
                        try:
                            aframe_cnt = int(ctx["aframe_cnt"])
                        except gdb.error:
                            aframe_cnt = 0

                        # Format: a: idx codec_name bit_rate sample_rate channels queue_cnt aframe_cnt
                        # C code: a: %d %s %" PRId64 " %d %d %d %" PRIu32 ""
                        codec_name = self.utils.get_codec_name(codec_id)
                        parts.append(f"a: {audio_idx} {codec_name} {bit_rate} {sample_rate} {channels} {queue_cnt} {aframe_cnt}")
            except gdb.error:
                pass

            # Video info
            try:
                video_idx = int(ctx["video_idx"])
                if video_idx >= 0:
                    streams = ctx["streams"]
                    video_stream = streams[video_idx]
                    if video_stream and video_stream["enc_ctx"]:
                        enc_ctx = video_stream["enc_ctx"]
                        codec_id = int(enc_ctx["codec_id"])
                        width = int(enc_ctx["width"])
                        height = int(enc_ctx["height"])

                        queue_cnt = self.utils.get_queue_count(video_stream)

                        # Format: v: idx codec_name width height queue_cnt
                        # C code: v: %d %s %d %d %d
                        codec_name = self.utils.get_codec_name(codec_id)
                        parts.append(f"v: {video_idx} {codec_name} {width} {height} {queue_cnt}")
            except gdb.error:
                pass

            lines.append(", ".join(parts))

        lines.append("--------------recorder dump end---------------")
        return lines
