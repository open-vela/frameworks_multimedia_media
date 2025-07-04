# **Media Server**

[[English](./README.md)|简体中文]

## **概述**

 Media Server 端提供了一套全面的功能，基于事件循环模型，处理各种媒体事件以及客户端交互，为用户提供音频和视频播放、录制音视频、焦点管理、策略执行以及会话控制等功能。

## **项目目录**

```tree
.
├── focus_stack.c
├── focus_stack.h
├── media_daemon.c
├── media_focus.c
├── media_graph.c
├── media_policy.c
├── media_server.c
├── media_server.h
├── media_session.c
├── media_stub.c
└── README.md
```
## **模块介绍**

### **Media Daemon**

 Media Daemon 是 Media Server的核心，负责创建和管理Media的各个模块，如 Media Focus、Media Graph、Media Session、Media Policy等。Medid Daemon 的核心原理是使用**poll** 函数, 监听RPC socket fd,和音视频设备驱动注册的 message queue fd,处理 RPC 命令并触发 FFmpeg 工作。

 ![Media Daemon架构图](../images/server/Media_Daemon_zh-cn.jpg)

 Media Daemon 的主要工作在一个循环中进行，大体步骤如下：
- 初始化创各个模块实例，Media Focus 等。
- 获取事件 fd,通过 **media_get_pollfds** 接口获取各个模块的事件 fd。
- 使用poll阻塞等待事件，当监听到有事件发生，则 poll返回。
- 处理事件，当 poll 返回，调用 media_poll_available 处理事件。
- 执行 run once（主要是ffmpeg），确保资源的有效管理和调度。

### **Media Focus**

 Media Focus 模块是 Media Server 的一个重要组成部分，目的是给多个音频流混合一起播放场景提供播放策略，协助实现同一时间内只有一个音频作为主音频内容被放送，其他音频变为次要音频或暂停输出的使用场景。Media Focus 的机制为合作抢占型，不使用 Media Focus 应用依旧可以播放音乐，但无法接入到音频焦点管理体系，此时出现的非策略性声音混合可能会对用户使用体验造成影响。
- 默认声音事件类型交互的配置文件位于 **/etc/media**。
- 声音事件类型的输入以 media wrapper 中的不同 **MEDIA_SCENARIO_XXX** 宏为准。目前包含11种类型的声音事件。
- 支持应用发起**焦点请求**、**放弃焦点请求**、**焦点改变通知**等功能。

### **Media Garph**

 Media Graph 的原理是将音视频相关的 **filter** 的 **inputs** , **outputs** 链接在一起，构成播放和录制的链路。主要策略如下：
 - 加载 graph 配置文件创建和配置 Media Graph 及相应的 filter。
 - 提供一系列函数处理 filter 的命令和事件，包括**打开**、**关闭**、**播放**、**暂停**、**停止**、**设置事件回调**、**处理命令队列**等操作。
 - 封装 Media Player 和 Media Recoder 的操作接口，调用 ffempeg 库，实现播放和录制功能。

### **Media Policy**

 Server 端的 Media Policy 模块，提供了一系列函数来处理媒体策略的设置、获取和通知等操作，在不同的项目中向APP提供统一的接口，把用户的**路由和音量的控制信息转化成对设备驱动的控制命令**；在不同的项目中，Policy 会使用不同的配置文件来处理 Policy 接口的控制命令的映射关系。 Media Policy 的策略通过配置文件实现：
 - 修改 ffmpeg filter graph 配置文件，通过 Policy控制 graph 的 filter，进行音量和链路控制。
 - 编写 pfw 配置文件实现插件扩展等。

### **Media Server**

 Media Server 模块通过监听不同类型的 socket，接收来自 Client 的连接请求，并使用回调函数处理接收的数据。支持下述功能：
- 创建服务器实例、销毁服务器实例。
- media_server_get_pollfds 接口获取用于轮询的文件描述符列表。
- media_server_poll_available 处理文件描述符事件。
- media_server_notify 向特定连接发送通知和管理连接数据等。

### **Media Session**

 Server 端的 media session 模块在媒体框架中扮演着关键的角色，通过设计的控制者和受控者的架构，实现了对媒体播放的高效控制和准确的状态通知。
 - **控制者**：只想控制其他流、接受状态变化通知、或者查询信息，不会对流的创建和销毁负责。
 - **受控者**: 掌握着某些流的播放状态，需要负责对这些流的创建，销毁，以及播放功能，同时需要及时地更新自己的状态信息。
 - 举例，如音响播放来自手机的音乐：
   - 控制者：UI界面是控制者。
   - 受控者：与手机建立音频通道的蓝牙模块是受控者。

### **FFmpeg**
 Media Graph 模块通过调用 FFmpeg 库，实现了音视频编解码、封装与解封装以及音视频流处理等功能。借助 FFmpeg，系统能够满足多样化的多媒体需求，包括但不限于视频播放、视频录制、拍照以及音视频剪辑等。

 在 FFmpeg 内部，通过启用解复用器（demuxer）、解码器（decoder）、复用器（muxer）、编码器（encoder）和过滤器（filter）等模块对音视频数据进行处理。FFmpeg 将解复用器、解码器、过滤器等模块组合成完整的处理流程（pipeline），以实现诸如视频播放、视频录制等完整功能。

 具体而言，通过一个配置文件（conf 文件）预先连接各个功能所需的过滤器。然后，利用 FFmpeg API 解析并初始化该配置文件，从而构建出处理流程。向此处理流程输入数据，即可实现数据的传输。

 通过配置参数 CONFIG_LIB_FFMPEG=y 使能 FFmpeg 功能，并通过 CONFIG_LIB_FFMPEG_CONFIGURATION 对上述模块进行配置。
- **demuxer**：解复用模块，用于将各种音视频的 containter 中 extract 出音视频 stream 送给 decoder 解码。例如支持 mp3,mp4 格式，通过配置 CONFIG_LIB_FFMPEG_CONFIGURATION="--enable-demuxer='mp3,mp4' 等参数配置使能。
- **decoder**：用于接收 demuxer 处理后音视频数据解码，例如支持 h264，aac 格式，通过配置 CONFIG_LIB_FFMPEG_CONFIGURATION="--enable-decoder='h264,aac' 等参数配置使能。
- **muxer**：复用模块，用于将各种音视频 stream 按照特定的多媒体容器格式的规则，封装成一个完整的 containter。同 demuxer，通过配置 CONFIG_LIB_FFMPEG_CONFIGURATION="--enable-muxer='mp3,mp4' 等参数配置使能。
- **encoder**：用于接收 muxer 处理后音视频数据编码，同 decoder，通过配置 CONFIG_LIB_FFMPEG_CONFIGURATION="--enable-encoder='h264,aac' 等参数配置使能。
- **filter**：用于对音视频数据进行处理，例如音视频的裁剪，缩放，旋转，音视频的混合，音视频的编解码，音视频的封装和解封装等。例如 graph 内配置如下通路，具体路径：/vendor/openvela/boards/vela/src/etc/media/graph.conf：
  ```
  adevsrc@pcm0c=format=nuttx:devname=/dev/audio/pcm0c[a],
  devsrc@InVsrc=d=/dev/video:s=640x480:r=30[v],
  [v]streamselect@SelVideo=inputs=1:map=0 -1[vout0][vout1],
  [vout0]devsink@fb=format=fbdev:devname=/dev/fb0,
  [a][vout1]moviesink_async@cap
  ```
  - adevsrc filter 从驱动设备文件 /dev/audio/pcm0c 中获取 audio 数据，devsrc filter 从驱动设备文件中获取 video 数据，然后通过 streamselect filter 将 audio/video 数据送到 moviesink_async filter 实现录像功能生成视频文件。
  - devsrc filter 从驱动设备文件中获取 video 数据，通过 streamselect filter 将数据送到 devsink，最终通过驱动设备节点 /dev/fb0 将 video 数据送到 framebuffer 中显示出来实现图像预览功能。