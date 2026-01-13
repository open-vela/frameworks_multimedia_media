############################################################################
# multimedia/media/scripts/gdb/media_utils.py
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

class MediaUtils:
    def __init__(self):
        self.codec_name_map = {}
        self._init_codec_map()

    def _init_codec_map(self):
        # Try to build map from enum AVCodecID
        try:
            enum_type = gdb.lookup_type("enum AVCodecID")
            for field in enum_type.fields():
                # Convert AV_CODEC_ID_MP3 -> mp3
                name = field.name.replace("AV_CODEC_ID_", "").lower()
                self.codec_name_map[field.enumval] = name
        except Exception:
            pass

        # Add common fallbacks
        self._add_fallback_codecs()

    def _add_fallback_codecs(self):
        # Common Audio
        fallbacks = {
            86017: "mp3",
            86018: "aac",
            86076: "opus",
            65536: "pcm_s16le",
            # Common Video
            28: "h264",
            174: "hevc",
            2: "mpeg2video",
        }
        for cid, name in fallbacks.items():
            if cid not in self.codec_name_map:
                self.codec_name_map[cid] = name

    def get_codec_name(self, codec_id):
        return self.codec_name_map.get(codec_id, str(codec_id))

    @staticmethod
    def safe_string(ptr, default=""):
        if not ptr:
            return default
        try:
            return ptr.string()
        except gdb.error:
            return default

    @staticmethod
    def get_queue_count(stream):
        """
        Get queued frame count from a stream (OutputStream or similar).
        Assumes stream has a 'queue' member which is FFFrameQueue,
        and FFFrameQueue has a 'queued' member.
        """
        if not stream:
            return 0
        try:
            return int(stream['queue']['queued'])
        except gdb.error:
            return 0
