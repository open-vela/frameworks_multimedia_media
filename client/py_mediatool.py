#! /usr/bin/env python3
'''
Copyright (C) 2020 Xiaomi Corporation

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
'''

import logging
import socket
import struct
import time

try:
    import tkinter as tk
except ImportError as e:
    print(f"ImportError: {e}")
    print("Pls install tkinter: sudo apt install python3-tk")
    exit(0)

MEDIA_ID_GRAPH = 1
MEDIA_ID_POLICY = 2
MEDIA_ID_PLAYER = 3
MEDIA_ID_RECORDER = 4
MEDIA_ID_SESSION = 5
MEDIA_ID_FOCUS = 6

MEDIA_PARCEL_SEND = 1
MEDIA_PARCEL_SEND_ACK = 2
MEDIA_PARCEL_REPLY = 3
MEDIA_PARCEL_CREATE_NOTIFY = 4
MEDIA_PARCEL_NOTIFY = 5


class Client:
    def __init__(self, addr_str="127.0.0.1:9000"):
        addr, port = addr_str.split(':')
        port = int(port)
        self.logger = logging.getLogger("client")
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        # Set the SO_REUSEADDR option
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        # Set the socket to non-blocking mode
        self.sock.setblocking(False)
        self.sock.bind(('0.0.0.0', port + 1))
        # Set the timeout, for example 0.5 second
        self.sock.settimeout(5)

        try:
            self.logger.info(f'connect to {addr}:{port} ...')
            self.sock.connect((addr, port))
        except BlockingIOError as e:
            self.logger.warning(f'{e}')
        except socket.error as msg:
            self.logger.error(f"Connect failed. msg:{msg}")

    def __del__(self):
        self.close()

    def send(self, data):
        try:
            self.sock.send(data)
        except socket.timeout:
            self.logger.error("*** Socket send timed out")

    def recv(self, max_len):
        retry = 0
        while True:
            try:
                data = self.sock.recv(max_len)
                if data:
                    return data
                else:
                    retry += 1
                    if retry > 10:
                        self.logger.error("*** Socket recv failed")
                        return ""
            except socket.timeout:
                self.logger.error("*** Socket recv timed out")
                data = struct.pack('iii', MEDIA_PARCEL_REPLY, 4, -5)
                return data
            except socket.error:
                pass

    def close(self):
        if self.sock:
            self.sock.close()
            self.sock = None


def convert_milliseconds_to_time_string(milliseconds):
    seconds, milliseconds = divmod(milliseconds, 1000)
    minutes, seconds = divmod(seconds, 60)
    hours, minutes = divmod(minutes, 60)
    return f"{hours:02d}:{minutes:02d}:{seconds:02d}"


class Application(tk.Frame):
    def __init__(self, master=None):
        self.logger = logging.getLogger("app")
        super().__init__(master)
        self.master = master
        self.create_widgets()

        self.client = Client()

        self.md_duration = 0
        self.md_position = 0

        w = 620
        h = 300
        screen_width = 1920  # self.winfo_screenwidth() / 2
        screen_height = 1080  # self.winfo_screenheight()

        self.logger.info(f'screen_width: {screen_width}, screen_height: {screen_height}')
        x = int(screen_width / 2 - w / 2)
        y = int(screen_height / 2 - h / 2)
        self.master.geometry(f'{w}x{h}+{x}+{y}')
        self.master.title("Media Client")

    def __del__(self):
        if self.client:
            self.client.close()

    def add_button(self, parent, text, command):
        button = tk.Button(parent, width=10)
        button["text"] = text
        button["command"] = command
        button.pack(side="left")
        return button

    def add_label(self, parent, text):
        label = tk.Label(parent, text=text)
        label.pack(side="right")
        return label

    def add_entry(self, parent, text, width=None):
        en = tk.Entry(parent, textvariable=tk.StringVar(value=text), width=width)
        en.pack(side="left")
        return en

    def add_listbox(self, parent, textlist):
        lb = tk.Listbox(parent)
        lb.configure(exportselection=False)
        if textlist:
            for item in textlist:
                lb.insert(tk.END, item)
        lb.place(x=450, y=1, width=160, height=260)
        return lb

    def add_optionmenu(self, parent, textlist):
        var = tk.StringVar()
        var.set(textlist[0])
        om = tk.OptionMenu(parent, var, *textlist)
        om.pack(side="left")
        return om

    def layout_add_row_frame(self):
        if self.__dict__.get('frame_rows', -1) == -1:
            self.frame_rows = 0
        else:
            self.frame_rows += 1
        frame = tk.Frame(self.master)
        frame.grid(row=self.frame_rows, column=0, padx=1, pady=1, sticky=tk.W)
        return frame

    def create_widgets(self):
        self.lb_info = self.add_listbox(self.master, None)
        frame = self.layout_add_row_frame()
        self.add_button(frame, "connect", self.on_connect)
        self.en_addr = self.add_entry(frame, "127.0.0.1:9000")
        self.lb_status = self.add_label(frame, "ret:")
        frame = self.layout_add_row_frame()
        self.add_button(frame, "open", self.on_open)
        self.om_media = self.add_optionmenu(frame, ["Video", "Music"])
        self.add_button(frame, "set_event", self.on_set_event)
        frame = self.layout_add_row_frame()
        self.add_button(frame, "set_options", self.on_set_options)
        self.en_option = self.add_entry(frame, "ext_buffer_num=2:apply_cropping=0", 38)
        frame = self.layout_add_row_frame()
        self.add_button(frame, "prepare", self.on_prepare)
        self.en_uri = self.add_entry(frame, '/mnt/1.mp4')
        frame = self.layout_add_row_frame()
        self.add_button(frame, "start", self.on_start)
        self.btn_pause = self.add_button(frame, "pause", self.on_pause)
        self.add_button(frame, "stop", self.on_stop)
        self.add_button(frame, "close", self.on_close)
        self.add_button(frame, "reset", self.on_reset)
        frame = self.layout_add_row_frame()
        self.add_button(frame, "seek", self.on_seek)
        self.en_seek = self.add_entry(frame, "0")
        frame = self.layout_add_row_frame()
        self.add_button(frame, "get_position", self.on_get_position)
        self.add_button(frame, "get_duration", self.on_get_duration)
        self.lb_progress = self.add_label(frame, "0/0 s")
        frame = self.layout_add_row_frame()
        self.add_button(frame, "test", self.on_test)

    def on_connect(self):
        if self.client:
            self.client.close()
            self.client = None
            time.sleep(0.2)
        self.client = Client(self.en_addr.get())
        self.lb_info.delete(0, tk.END)

    def media_parcel(self, handle, cmd, args, res_len):
        target = "\0".encode('utf-8')
        cmd = f"{cmd}\0".encode('utf-8')
        args = f"{args}\0".encode('utf-8')
        '''
        fmt = f'<iq{len(target)}s{len(cmd)}s{len(args)}si'
        data = struct.pack(fmt, MEDIA_ID_PLAYER, handle, target, cmd, args, res_len)
        '''
        fmt = f'<i{len(target)}s{len(cmd)}s{len(args)}si'
        data = struct.pack(fmt, MEDIA_ID_PLAYER, target, cmd, args, res_len)
        data = struct.pack(f'ii{len(data)}s', MEDIA_PARCEL_SEND_ACK, len(data), data)
        self.logger.info(f'send cmd:{cmd} len:{len(data)}')
        return data

    def media_recv(self):
        data = self.client.recv(32)
        if len(data) < 12:
            self.logger.error(f"*** failed need more data len:{len(data)} < 12")
            return -1, -1, -1, ""
        code, length, ret = struct.unpack('iii', data[:12])
        assert code == MEDIA_PARCEL_REPLY
        self.lb_status['text'] = f'ret:{ret}'

        return code, length, ret, data

    def handle(self):
        handle = int(self.lb_info.get(self.lb_info.curselection()[0]).split('|')[1], 16)
        return handle

    def on_open(self):
        if not self.client:
            self.client = Client()

        open_type = self.om_media.cget('text')
        data = self.media_parcel(0, "open", open_type, 32)
        self.client.send(data)

        code, length, ret, data = self.media_recv()
        self.logger.info(f'rcv:{code, length, ret}')
        if ret < 0:
            self.logger.error(f"*** open failed return:{code, length, ret}")
        else:
            # handle = str(data[12:12 + ret], encoding='utf-8')
            handle = 0
            self.logger.info(f"open return:{code, length, ret} {handle} 0x{int(handle):X}")
            self.lb_info.insert(tk.END, f"{open_type}|{int(handle):X}")
            self.lb_info.selection_clear(0, tk.END)
            self.lb_info.select_set(tk.END)

    def on_set_event(self):
        data = self.media_parcel(self.handle(), "set_event", "", 0)
        self.client.send(data)

        code, length, ret, data = self.media_recv()
        self.logger.info(f'rcv:{code, length, ret}')

    def on_set_options(self):
        data = self.media_parcel(self.handle(), "set_options", self.en_option.get(), 0)
        self.client.send(data)

        code, length, ret, data = self.media_recv()
        self.logger.info(f'rcv:{code, length, ret}')

    def on_prepare(self):
        data = self.media_parcel(self.handle(), "prepare", self.en_uri.get(), 0)
        self.client.send(data)

        code, length, ret, data = self.media_recv()
        self.logger.info(f'rcv:{code, length, ret}')

    def on_start(self):
        data = self.media_parcel(self.handle(), "start", "0", 0)
        self.client.send(data)

        code, length, ret, data = self.media_recv()
        self.logger.info(f'rcv:{code, length, ret}')

    def on_pause(self):
        data = self.media_parcel(self.handle(), "pause", "0", 0)
        self.client.send(data)

        code, length, ret, data = self.media_recv()
        self.logger.info(f'rcv:{code, length, ret}')

    def on_seek(self):
        data = self.media_parcel(self.handle(), "seek", self.en_seek.get(), 0)
        self.client.send(data)

        code, length, ret, data = self.media_recv()
        self.logger.info(f'rcv:{code, length, ret}')

    def on_get_duration(self):
        data = self.media_parcel(self.handle(), "get_duration", "", 32)
        self.client.send(data)

        code, length, ret, data = self.media_recv()
        self.logger.info(f'rcv:{code, length, ret}')
        if ret < 0:
            self.logger.error(f"*** get_duration failed return:{code, length, ret}")
        else:
            self.md_duration = int(data[12:].decode('utf-8').split('\0')[0])
            dur = convert_milliseconds_to_time_string(self.md_duration)
            pos = convert_milliseconds_to_time_string(self.md_position)
            self.lb_progress['text'] = f'{pos}/{dur} ms'

    def on_get_position(self):
        data = self.media_parcel(self.handle(), "get_position", "0", 32)
        self.client.send(data)

        code, length, ret, data = self.media_recv()
        self.logger.info(f'rcv:{code, length, ret}')
        if ret < 0:
            self.logger.error(f"*** get_position failed return:{code, length, ret}")
        else:
            self.md_position = int(data[12:].decode('utf-8').split('\0')[0])
            dur = convert_milliseconds_to_time_string(self.md_duration)
            pos = convert_milliseconds_to_time_string(self.md_position)
            self.lb_progress['text'] = f'{pos}/{dur} ms'

    def on_stop(self):
        data = self.media_parcel(self.handle(), "stop", "0", 0)
        self.client.send(data)

        code, length, ret, data = self.media_recv()
        self.logger.info(f'rcv:{code, length, ret}')

    def on_reset(self):
        data = self.media_parcel(self.handle(), "reset", "0", 0)
        self.client.send(data)

        code, length, ret, data = self.media_recv()
        self.logger.info(f'rcv:{code, length, ret}')

    def on_close(self):
        '''
        if not self.handle():
            self.logger.info("handle is null")
            return
        '''
        data = self.media_parcel(self.handle(), "close", "0", 0)
        self.client.send(data)

        code, length, ret, data = self.media_recv()
        self.logger.info(f'rcv:{code, length, ret}')
        if ret == 0:
            self.lb_info.delete(self.lb_info.curselection()[0])

    def on_test(self):
        self.on_open()
        self.on_prepare()
        self.on_start()


if __name__ == "__main__":
    logging.basicConfig(
        level=logging.INFO,
        format='%(asctime)s[%(levelname)s %(funcName)s:%(lineno)d]%(message)s',
        handlers=[
            logging.StreamHandler()
        ]
    )
    logger = logging.getLogger(__name__)
    logger.info('Start client')

    root = tk.Tk()
    app = Application(master=root)
    app.mainloop()
