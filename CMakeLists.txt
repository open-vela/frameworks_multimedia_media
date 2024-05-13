# ##############################################################################
# frameworks/media/CMakeLists.txt
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
    list(FILTER CSRCS EXCLUDE REGEX client/media_uv*.c)
  endif()

  file(GLOB UTILS_SRCS utils/*.c)
  set(CSRCS ${CSRCS} ${UTILS_SRCS})

  if(CONFIG_MEDIA_FEATURE)
    set(FEATURE_TOP ${NUTTX_APPS_DIR}/frameworks/base/feature)
    list(APPEND INCDIR ${FEATURE_TOP}/include)

    set(CSRCS ${CSRCS} ${CMAKE_CURRENT_LIST_DIR}/feature/audio_impl.c
              ${CMAKE_CURRENT_LIST_DIR}/feature/volume_impl.c)
  endif()

  if(CONFIG_MEDIA_FOCUS)
    list(APPEND SERVER_SRCS server/media_focus.c server/focus_stack.c)
  endif()

  if(CONFIG_MEDIA_SERVER)
    set(DAEMON_SRCS server/media_daemon.c server/media_stub.c
                    server/media_server.c)
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

  if(CONFIG_LIB_FFMPEG)
    list(APPEND SERVER_SRCS server/media_graph.c server/media_session.c)
    list(APPEND INCDIR ${CMAKE_BINARY_DIR}/apps/external/ffmpeg)
    list(APPEND INCDIR ${NUTTX_APPS_DIR}/external/ffmpeg/ffmpeg)
  endif()

  if(CONFIG_LIB_PFW)
    list(APPEND SERVER_SRCS server/media_policy.c)
    list(APPEND INCDIR ${CMAKE_CURRENT_LIST_DIR}/../pfw/include)
    list(APPEND INCDIR ${CMAKE_CURRENT_LIST_DIR}/../utils/include)
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

  nuttx_add_library(libmedia STATIC)
  target_sources(libmedia PRIVATE ${CSRCS})
  target_include_directories(libmedia PRIVATE ${INCDIR})
  nuttx_add_dependencies(TARGET libmedia DEPENDS ffmpeg)

  if(CONFIG_MEDIA_FEATURE)
    set(JIDL_PATHS ${CMAKE_CURRENT_LIST_DIR}/feature/volume.jidl
                   ${CMAKE_CURRENT_LIST_DIR}/feature/audio.jidl)

    include(nuttx_add_jidl)
    set(PY_SCRIPT ${FEATURE_TOP}/tools/jidl/jsongensource.py)
    set(BINARY_EXT_MODULES_DIR ${CMAKE_BINARY_DIR}/feature/modules/)

    nuttx_add_jidl(
      TARGET
      libmedia
      JIDL_SCRIPT
      ${PY_SCRIPT}
      JIDL_OUT_DIR
      ${BINARY_EXT_MODULES_DIR}
      JIDLS
      ${JIDL_PATHS}
      OUT_SRC_EXT
      c)
  endif() # CONFIG_MEDIA_FEATURE

  set_property(
    TARGET nuttx
    APPEND
    PROPERTY NUTTX_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_LIST_DIR}/include)

endif()
