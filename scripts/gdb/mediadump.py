import gdb
import argparse
from nuttxgdb import utils


class MediaDump(gdb.Command):
    """This GDB command dumps MediaPoll and its handle to MediaGraphPriv when the provided argument matches the node's name."""

    def __init__(self):
        super(MediaDump, self).__init__("mediadump", gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        parser = argparse.ArgumentParser(description="MediaDump command options.")
        parser.add_argument(
            "name",
            nargs="?",
            choices=[
                "graph",
                "policy",
                "focus",
                "session",
                "server",
            ],
            help="Name of the media node to dump.",
        )

        args = parser.parse_args(arg.split())
        arg = f"media_{args.name}"

        g_media = gdb.parse_and_eval("g_media")
        array_size = utils.nitems(g_media)
        handle = None

        for i in range(array_size):
            if g_media[i]["name"].string() == arg:
                handle = g_media[i]["handle"]
                break

        if not handle:
            gdb.write(f"Error: No media node found with the name '{arg}'.\n")
            return

        graph_priv = handle.cast(gdb.lookup_type("MediaGraphPriv").pointer())

        gdb.write(f"Node handle as MediaGraphPriv: {graph_priv}\n")
        graph = graph_priv["graph"]

        topology = {}
        for filter in utils.ArrayIterator(graph["filters"], graph["nb_filters"]):
            filter_name = filter["name"].string()

            topology[filter_name] = []

            for output_link in utils.ArrayIterator(filter["outputs"], filter["nb_outputs"]):
                frame_count_in = output_link["frame_count_in"]
                sample_rate = output_link["sample_rate"]
                ch_layout = output_link["ch_layout"]
                nb_channels = ch_layout["nb_channels"]
                codec_value = output_link["codec"]
                frame_wanted_out = output_link["frame_wanted_out"]
                channel_type = {1: "mono", 2: "stereo"}.get(int(nb_channels), "unknown")

                next_filter_name = (
                    output_link["dst"]["name"].string() if output_link["dst"] else None
                )
                if next_filter_name and int(frame_count_in) > 0:
                    topology[filter_name].append(
                        (
                            next_filter_name,
                            int(frame_count_in),
                            sample_rate,
                            channel_type,
                            codec_value,
                            frame_wanted_out,
                        )
                    )

        gdb.write("\nMedia Graph Topology:\n")
        for filter_name, connections in topology.items():
            if connections:
                for next_filter, fc, sr, ch, c, fwo in connections:
                    gdb.write(
                        f"{filter_name} -> {next_filter} (FC: {fc}, SR: {sr}, Channels: {ch}, Codec: {c}, FWO: {fwo})\n"
                    )

        gdb.write("\n")
