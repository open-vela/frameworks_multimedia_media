############################################################################
# tools/pynuttx/nxgdb/avfilter_function.py
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

from typing import Optional

import gdb


def get_filter_func(filter: gdb.Value):
    if filter is None:
        return None

    name = filter["name"].string()
    sub_name = None
    if name.find("@") != -1:
        sub_name = name.partition("@")[0]
    elif name.startswith("Parsed_"):
        # Analyze automatically named filters，for example: Parsed_volume_16
        str_head = "Parsed_"
        sub_name = name[len(str_head) :].partition("_")[0]
    elif name.startswith("auto_"):
        # Analyze automatically named filters，for example: auto_aresample
        str_head = "auto_"
        sub_name = name[len(str_head) :]
    else:
        raise gdb.GdbError(f"filter name analysis error: {name}")
    return g_all_filter.get(sub_name)


"""filter dump function"""


def movie_async(hander: gdb.Value, dump: Optional[list] = None):
    movie = hander["priv"].cast(gdb.lookup_type("MovieAsyncContext").pointer())
    stream = movie["streams"]

    if dump is not None:
        dump.append(f"st: {movie['state']}")
        for i in range(int(hander["nb_outputs"])):
            if not stream:
                break
            index = int(stream[i]["index"])
            if index < 0 or not stream[index]["codecpar"]:
                continue
            parms = stream[i]["codecpar"]
            if stream[i]["type"] == 1:
                dump.append(
                    f"A: {stream[i]['index']}, {parms['codec_id']}, {parms['bit_rate']}, "
                    f"{parms['sample_rate']}, {parms['ch_layout']['nb_channels']}, "
                    f"{stream[i]['dat_queue']['queued']}"
                )
            else:
                dump.append(
                    f"V: {stream[i]['index']}, {parms['codec_id']}, {parms['width']}, "
                    f"{parms['height']}, {stream[i]['dat_queue']['queued']}"
                )


def streamselect(hander: gdb.Value, dump: Optional[list] = None):
    slct = hander["priv"].cast(gdb.lookup_type("StreamSelectContext").pointer())
    if dump is not None:
        if int(slct["map"]) != 0:
            dump.append("map:")
            smap = slct["map"]
            smap_size = int(slct["nb_map"])
            for i in range(smap_size):
                map = int(smap[i])
                dump.append(f"{map}")
        else:
            dump.append("no map")


def afade(hander: gdb.Value, dump: Optional[list] = None):
    if dump is not None:
        dump.append("")


def devsrc(hander: gdb.Value, dump: Optional[list] = None):
    src = hander["priv"].cast(gdb.lookup_type("ADevSrcPriv").pointer())
    if src["format"] is not None:
        format = src["format"].string()
        if format == "nuttx":
            nuttx = src["fmt_ctx"]["priv_data"].cast(
                gdb.lookup_type("NuttxPriv").pointer()
            )
            if dump is not None:
                dump.append(
                    f"{nuttx['running']}, {nuttx['draining']}, {nuttx['period_bytes']},"
                    f"{nuttx['periods']}, de_cnt, {nuttx['mq']}"
                )
        elif format == "bluelet":
            bluelet = src["fmt_ctx"]["priv_data"].cast(
                gdb.lookup_type("BlueletPriv").pointer()
            )
            if dump is not None:
                dump.append(
                    f"{bluelet['server_name']}, {bluelet['mode']}, {bluelet['codec_id']}, "
                    f"{bluelet['state']}, {bluelet['ctrl_fd']}, {bluelet['data_fd']}"
                )
        elif format == "alsa":
            if dump is not None:
                dump.append("alsa")
        else:
            if dump is not None:
                dump.append("invild format")


def amix(hander: gdb.Value, dump: Optional[list] = None):
    amix = hander["priv"].cast(gdb.lookup_type("MixContext").pointer())

    if dump is not None:
        for i in range(int(amix["nb_inputs"])):
            state = int(amix["input_state"][i]) if int(amix["input_state"]) != 0 else 0
            fifo_size = (
                int(amix["fifos"][i]["nb_samples"]) if int(amix["fifos"][i]) != 0 else 0
            )
            dump.append(f"{i}:{state},{fifo_size}")


def adevsink(hander: gdb.Value, dump: Optional[list] = None):
    sink = hander["priv"].cast(gdb.lookup_type("ADevSinkPriv").pointer())
    if sink["format"] is not None:
        format = sink["format"].string()
        if format == "nuttx":
            nuttx = sink["fmt_ctx"]["priv_data"].cast(
                gdb.lookup_type("NuttxPriv").pointer()
            )
            if dump is not None:
                dump.append(
                    f"{nuttx['running']}, {nuttx['draining']}, {nuttx['period_bytes']}, "
                    f"{nuttx['periods']}, cunt_none, {int(nuttx['mq'])}"
                )
        elif format == "bluelet":
            bluelet = sink["fmt_ctx"]["priv_data"].cast(
                gdb.lookup_type("BlueletPriv").pointer()
            )
            if dump is not None:
                dump.append(
                    f"{bluelet['server_name']}, {bluelet['mode']}, {bluelet['lastpkt']}, "
                    f"{bluelet['state']}, {bluelet['ctrl_fd']}, {bluelet['data_fd']}"
                )
        elif format == "alsa":
            if dump is not None:
                dump.append("alsa")
        else:
            if dump is not None:
                dump.append("invild format")


def devsink(hander: gdb.Value, dump: Optional[list] = None):
    sink = hander["priv"].cast(gdb.lookup_type("DevSinkPriv").pointer())
    if sink["format"] is not None:
        format = sink["format"].string()
        if format == "vtun":
            vtun = sink["fmt_ctx"]["priv_data"].cast(
                gdb.lookup_type("VtunCtx").pointer()
            )
            if dump is not None:
                dump.append(f"{vtun['frame_count']}, {vtun['drop_count']}")
        elif format == "fbdev":
            fbdev = sink["fmt_ctx"]["priv_data"].cast(
                gdb.lookup_type("FBDevContext").pointer()
            )
            if dump is not None:
                dump.append(f"{fbdev['frame_count']}, {fbdev['drop_count']}")
        else:
            if dump is not None:
                dump.append("invild format")


def moviesink_async(hander: gdb.Value, dump: Optional[list] = None):
    movie = hander["priv"].cast(gdb.lookup_type("MovieSinkPriv").pointer())
    if dump is not None:
        dump.append(f"{movie['state']}")


# todo
def scale(hander: gdb.Value, dump: Optional[list] = None):
    if dump is not None:
        dump.append("")


def volume(hander: gdb.Value, dump: Optional[list] = None):
    vol = hander["priv"].cast(gdb.lookup_type("VolumeContext").pointer())
    if dump is not None:
        dump.append(f"{vol['volume']}")


# todo
def rpmsgsink(hander: gdb.Value, dump: Optional[list] = None):
    if dump is not None:
        dump.append("")


def aresample(hander: gdb.Value, dump: Optional[list] = None):
    if dump is not None:
        dump.append("")


def encoder(hander: gdb.Value, dump: Optional[list] = None):
    if dump is not None:
        dump.append("")


def decoder(hander: gdb.Value, dump: Optional[list] = None):
    if dump is not None:
        dump.append("")


# todo
def achange(hander: gdb.Value, dump: Optional[list] = None):
    if dump is not None:
        dump.append("")


def nxsrc(hander: gdb.Value, dump: Optional[list] = None):
    if dump is not None:
        dump.append("")


def nxsink(hander: gdb.Value, dump: Optional[list] = None):
    if dump is not None:
        dump.append("")


g_all_filter = {
    "amovie_async": movie_async,
    "movie_async": movie_async,
    "astreamselect": streamselect,
    "streamselect": streamselect,
    "afadext": afade,
    "afade": afade,
    "adevsrc": devsrc,
    "devsrc": devsrc,
    "amix": amix,
    "devsink": devsink,
    "adevsink": adevsink,
    "amoviesink_async": moviesink_async,
    "moviesink_async": moviesink_async,
    "scale": scale,
    "volume": volume,
    "rpmsgsink": rpmsgsink,
    "vmoviesink_async": moviesink_async,
    "aresample": aresample,
    "aencoder": encoder,
    "encoder": encoder,
    "adecoder": decoder,
    "decoder": decoder,
    "achange": achange,
    "anxsrc": nxsink,
    "nxsrc": nxsink,
    "anxsink": nxsink,
    "nxsink": nxsink,
}
