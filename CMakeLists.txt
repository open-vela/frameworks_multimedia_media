# ##############################################################################
# frameworks/multimedia/media/CMakeLists.txt
#
# Licensed to the Apache Software Foundation (ASF) under one or more contributor
# license agreements.  See the NOTICE file distributed with this work for
# additional information regarding copyright ownership.  The ASF licenses this
# file to you under the Apache License, Version 2.0 (the "License"); you may not
# use this file except in compliance with the License.  You may obtain a copy of
# the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations under
# the License.
#
# ##############################################################################

if(CONFIG_MEDIA)

  nuttx_add_prebuilt(
    BIN_TARGETS
    libmedia
    APP_TARGETS
    ${CONFIG_MEDIA_SERVER_PROGNAME}
    mediatool
    EXPORT_FILES
    include)

  # ############################################################################
  # Include Directory
  # ############################################################################

  set(INCDIR ${CMAKE_CURRENT_LIST_DIR}/utils ${CMAKE_CURRENT_LIST_DIR}/include)

  # ############################################################################
  # Sources
  # ############################################################################

  file(GLOB CLIENT_SRCS client/*.c)
  set(CSRCS ${CLIENT_SRCS})
  set(SERVER_SRCS)

  if(NOT CONFIG_LIBUV)
    file(GLOB EXLUDE_FILES ${CMAKE_CURRENT_LIST_DIR}/client/media_uv*.c)
    list(REMOVE_ITEM CSRCS ${EXLUDE_FILES})
  endif()

  file(GLOB UTILS_SRCS utils/*.c)
  set(CSRCS ${CSRCS} ${UTILS_SRCS})

  if(CONFIG_MEDIA_FOCUS)
    list(APPEND SERVER_SRCS server/media_focus.c server/focus_stack.c)
  endif()

  if(CONFIG_MEDIA_GRAPH)
    list(APPEND SERVER_SRCS server/audio_graph.c server/media_session.c
                            server/media_player.c server/media_recorder.c
                            server/media_video_output.c
                            server/audio_negotiation.c)
    list(APPEND INCDIR ${CMAKE_BINARY_DIR}/apps/external/ffmpeg)
    list(APPEND INCDIR ${NUTTX_APPS_DIR}/external/ffmpeg/ffmpeg)
  endif()

  if(CONFIG_MEDIA_POLICY)
    list(APPEND SERVER_SRCS server/media_policy.c)
    list(APPEND INCDIR ${CMAKE_CURRENT_LIST_DIR}/pfw/include)
    list(APPEND INCDIR ${NUTTX_APPS_DIR}/frameworks/system/utils/include)
  endif()

  if(CONFIG_MEDIA_SERVER)
    set(DAEMON_SRCS server/media_daemon.c server/media_stub.c
                    server/media_server.c server/media_plugin.c)
    nuttx_add_application(
      NAME
      ${CONFIG_MEDIA_SERVER_PROGNAME}
      SRCS
      ${DAEMON_SRCS}
      STACKSIZE
      ${CONFIG_MEDIA_SERVER_STACKSIZE}
      PRIORITY
      ${CONFIG_MEDIA_SERVER_PRIORITY}
      INCLUDE_DIRECTORIES
      ${INCDIR})
  endif()

  if(CONFIG_MEDIA_TRIGGER)
    list(APPEND SERVER_SRCS server/media_trigger.c)
  endif()

  if(CONFIG_MEDIA_TOOL)
    nuttx_add_application(
      NAME
      mediatool
      SRCS
      media_tool.c
      STACKSIZE
      ${CONFIG_MEDIA_TOOL_STACKSIZE}
      PRIORITY
      ${CONFIG_MEDIA_TOOL_PRIORITY}
      INCLUDE_DIRECTORIES
      ${INCDIR})
  endif()

  set(CSRCS ${CSRCS} ${SERVER_SRCS})

  # ############################################################################
  # Library Configuration
  # ############################################################################

  nuttx_library_ifndef(SKIP_PREBUILT libmedia)
  nuttx_sources(${CSRCS})
  nuttx_include_directories(${INCDIR})
  nuttx_add_dependencies(TARGET libmedia DEPENDS ffmpeg)

  if(CONFIG_FEATURE_FRAMEWORK)
    set(FEATURE_SRCS ${CMAKE_CURRENT_LIST_DIR}/feature/audio_impl.c
                     ${CMAKE_CURRENT_LIST_DIR}/feature/volume_impl.c
                     ${CMAKE_CURRENT_LIST_DIR}/feature/session_impl.c
                     ${CMAKE_CURRENT_LIST_DIR}/feature/record_impl.c)

    set(JIDL_PATHS ${CMAKE_CURRENT_LIST_DIR}/feature/volume.jidl
                   ${CMAKE_CURRENT_LIST_DIR}/feature/audio.jidl
                   ${CMAKE_CURRENT_LIST_DIR}/feature/session.jidl
                   ${CMAKE_CURRENT_LIST_DIR}/feature/record.jidl)

    set(FEATURE_NAMES system_volume system_audio system_media_session system_record)

    nuttx_add_jidl(
      TARGET
      libmedia
      FEATURE_SRCS
      ${FEATURE_SRCS}
      JIDLS
      ${JIDL_PATHS}
      FEATURE_NAMES
      ${FEATURE_NAMES}
      OUT_SRC_EXT
      c)
  endif() # CONFIG_MEDIA_FEATURE

  set_property(
    TARGET nuttx
    APPEND
    PROPERTY NUTTX_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_LIST_DIR}/include)

endif()

if(EXISTS ${CMAKE_CURRENT_LIST_DIR}/pfw/CMakeLists.txt)
  add_subdirectory(pfw)
endif()
