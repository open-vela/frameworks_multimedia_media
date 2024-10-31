# **Media Client**

[English|[简体中文](./README_zh-cn.md)]

## **Overview**

The Media Client provides a set of interfaces and tools for interacting with the Media Server. It supports various media operations such as playing audio and video, recording audio, managing audio focus, setting media policies, and handling media sessions. Each module on the client side encapsulates both synchronous and asynchronous interfaces for users to call. Users can choose to use asynchronous operations for efficient communication with the server.

## **Project Directory**

```tree
.
├── media_dtmf.c
├── media_focus.c
├── media_graph.c
├── media_policy.c
├── media_proxy.c
├── media_proxy.h
├── media_session.c
├── media_uv.c
├── media_uv_focus.c
├── media_uv_graph.c
├── media_uv.h
├── media_uv_policy.c
├── media_uv_session.c
├── py_mediatool.py
└── README.md
```
## **Module Introduction**

The Media Client encapsulates the synchronous and asynchronous interfaces of the Media Focus, Media Graph, Media Policy, and Media Session modules. Additionally, the client supports both synchronous and asynchronous RPC interfaces for communication with the server.

### **Media Focus**

 The Media Focus module on the Client side implements the management and request operations of Focus signaling on the Client side, and encapsulates the synchronous and asynchronous interfaces respectively.
- Synchronous Interface:
  - **media_focus.c** provides a synchronous interface for the Client to interact with the Server to manage Media Focus.
  - The module supports receiving user commands about Focus, such as the **media_focus_request** interface for requesting Focus, the **media_focus_release** interface for releasing Media Focus, and the **media_focus_dump** interface for printing Focus log information.
  - The commands are forwarded to the Server for processing through **media_proxy**.
- Asynchronous interfaces:
   - **media_uv_focus.c** provides Client asynchronous requesting and releasing Media Focus, and communicates with Server via **media_uv**.
  - When requesting focus, a series of asynchronous operations will be performed, including connecting to the Server, sending Ping request, starting to listen, sending focus request, etc., and finally calling the user-defined Focus callback function to return.
  - When giving up the focus, it sends a give up request and disconnects from the Server, releases resources and so on.

### **Media Graph**

The Media Graph module on the Client side implements the audio processing functionality, including interfaces to the Media Player and Media Recorder. It is mainly used to interact with Media Server to manage the operations of Media Player and Media Recorder, and encapsulates synchronous and asynchronous interfaces respectively.
- Synchronous Interface:
  - The **media_graph.c** provides a series of functions for synchronous interaction with the Server.
  - It supports various operations of Player and Recorder, such as opening, closing, preparing, playing, pausing, stopping, positioning, setting attributes, getting attributes, and so on.
- Asynchronous interface:
   - **media_uv_graph.c** provides some series of functions for interacting with Server in a libuv-based environment.
   - It supports various operations of **Media Player** and **Media Recorder**, including opening, closing, preparing, playing, pausing, stopping, positioning, setting properties, getting properties, and so on.
   - Send the corresponding commands to Server through **media_uv_stream_send** interface, and set the callback function for Server to respond.

### **Media Policy**

 Media Policy on the Client side provides an interface to set and obtain various Media Policy parameters, and through interaction with the server, it realizes flexible control and querying of Media Policy, which can be used to adjust audio settings, manage device usage and control media playback characteristics.
 - Synchronization interface:
   - **media_policy.c** provides a synchronization interface for managing policies, so that clients can easily set and get various media-related parameters. It supports managing the mute mode, subscribing to a policy, and providing a synchronization interface.
   - It supports managing mute mode, subscribing to policy changes, controlling audio device usage interfaces, and setting volume.
- Asynchronous interface:
   - **media_uv_policy.c** provides an interface for Client to set and get various Policy related parameters in an asynchronous way, supporting getting audio mode, device usage status, etc.

### **Media Session**

Media Session is responsible for media session management in Media Client, including opening and closing sessions, registering and unregistering, setting up event callbacks and handling event notifications, performing playback control operations (such as starting, pausing, stopping, switching songs, increasing and decreasing volume, etc.), as well as querying session status and metadata.
   - Synchronous Interface: **media_session.c**
   - Asynchronous Interface: **media_uv_session.c**

## **Media RPC Communication**

 The Client side encapsulates the interface to communicate with the Server and supports both synchronous and asynchronous methods.
 - Synchronous RPC:
   - **media_proxy.c** provides a complete set of synchronous communication interfaces for Client and Server to communicate synchronously.
   - It provides connection management, message sending and receiving, callback setting and request processing.
 - Asynchronous RPC:
   - **media_uv.c** provides the underlying interface for asynchronous media operations, which is used to communicate and interact with the media server.
   - It supports connecting to the media server, disconnecting from the server, reconnecting, creating listeners, and sending message packets.
   - The documentation uses the UV library for asynchronous I/O operations, communicating with the media server through pipes. The sending and receiving of message packets is managed through a queue to ensure message order and reliability.
