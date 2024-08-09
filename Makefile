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
CXXEXT   := .cpp
CXXFLAGS += -std=c++17

MODULE  = $(CONFIG_MEDIA_SERVER)
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/frameworks/media/utils

CSRCS += $(wildcard client/*.c)
ifeq ($(CONFIG_LIBUV),)
  CSRCS := $(filter-out $(wildcard client/media_uv*.c), $(CSRCS))
endif

CSRCS += $(wildcard utils/*.c)

ifeq ($(CONFIG_MEDIA_FEATURE),y)
depend::
	@python3 $(APPDIR)/frameworks/base/feature/tools/jidl/jsongensource.py \
		$(APPDIR)/frameworks/media/feature/volume.jidl -out-dir \
		$(APPDIR)/frameworks/media/feature -header volume.h -source volume.c
	@python3 $(APPDIR)/frameworks/base/feature/tools/jidl/jsongensource.py \
		$(APPDIR)/frameworks/media/feature/audio.jidl -out-dir \
		$(APPDIR)/frameworks/media/feature -header audio.h -source audio.c
	@python3 $(APPDIR)/frameworks/base/feature/tools/jidl/jsongensource.py \
		$(APPDIR)/frameworks/media/feature/session.jidl -out-dir \
		$(APPDIR)/frameworks/media/feature -header session.h -source session.c

  CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/frameworks/media/feature
  CSRCS  += feature/audio.c
  CSRCS  += feature/audio_impl.c
  CSRCS  += feature/volume.c
  CSRCS  += feature/volume_impl.c
  CSRCS  += feature/session.c
  CSRCS  += feature/session_impl.c
endif # CONFIG_MEDIA_FEATURE

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
