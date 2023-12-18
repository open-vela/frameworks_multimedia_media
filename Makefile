############################################################################
# frameworks/media/Makefile
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

include $(APPDIR)/Make.defs
CXXEXT     := .cpp
CXXFLAGS   += -std=c++17

MODULE  = $(CONFIG_MEDIA_SERVER)
CSRCS  += $(wildcard utils/*.c)
CSRCS  += client/media_dtmf.c client/media_focus.c client/media_graph.c \
          client/media_policy.c client/media_session.c
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/frameworks/media/utils

ifneq ($(CONFIG_LIBUV),)
  CSRCS += $(wildcard client/media_uv*.c)
endif

ifeq ($(CONFIG_MEDIA_FEATURE),y)
ifeq ($(CONFIG_ARCH), arm)
TARGETDIR := arm
else ifeq ($(CONFIG_ARCH), arm64)
TARGETDIR := aarch64
else ifeq ($(CONFIG_ARCH), xtensa)
TARGETDIR := xtensa
else
TARGETDIR := x86
endif
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/external/libffi
CXXFLAGS += ${INCDIR_PREFIX}$(APPDIR)/external/libffi
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/external/libffi/libffi/src/$(TARGETDIR)
CXXFLAGS += ${INCDIR_PREFIX}$(APPDIR)/external/libffi/libffi/src/$(TARGETDIR)
CXXFLAGS += ${INCDIR_PREFIX}$(APPDIR)/frameworks/base/feature/include
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/frameworks/base/feature/include
CXXFLAGS += ${INCDIR_PREFIX}$(APPDIR)/frameworks/base/feature/src
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/frameworks/base/feature/src
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/frameworks/media/feature
CXXFLAGS += ${INCDIR_PREFIX}$(APPDIR)/frameworks/media/feature
CXXSRCS  += feature/volume.cpp
CXXSRCS  += feature/volume_impl.cpp
endif

ifneq ($(CONFIG_MEDIA_FOCUS),)
  CSRCS += server/media_focus.c server/focus_stack.c
endif

ifneq ($(CONFIG_MEDIA_SERVER),)
  CSRCS    += server/media_stub.c server/media_server.c
  MAINSRC   = server/media_daemon.c
  PROGNAME  = $(CONFIG_MEDIA_SERVER_PROGNAME)
  PRIORITY  = $(CONFIG_MEDIA_SERVER_PRIORITY)
  STACKSIZE = $(CONFIG_MEDIA_SERVER_STACKSIZE)
endif

ifneq ($(CONFIG_LIB_FFMPEG),)
  CSRCS  += server/media_graph.c server/media_session.c
  CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/external/ffmpeg/ffmpeg
endif

ifneq ($(CONFIG_LIB_PFW),)
  CSRCS  += server/media_policy.c
  CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/frameworks/pfw/include
endif

ifneq ($(CONFIG_MEDIA_TOOL),)
  MAINSRC   += media_tool.c
  PROGNAME  += mediatool
  PRIORITY  += $(CONFIG_MEDIA_TOOL_PRIORITY)
  STACKSIZE += $(CONFIG_MEDIA_TOOL_STACKSIZE)
endif

ASRCS := $(wildcard $(ASRCS))
CSRCS := $(wildcard $(CSRCS))
CXXSRCS := $(wildcard $(CXXSRCS))
MAINSRC := $(wildcard $(MAINSRC))
NOEXPORTSRCS = $(ASRCS)$(CSRCS)$(CXXSRCS)$(MAINSRC)

ifneq ($(NOEXPORTSRCS),)
BIN := $(APPDIR)/staging/libmedia.a
endif

EXPORT_FILES := include

include $(APPDIR)/Application.mk
