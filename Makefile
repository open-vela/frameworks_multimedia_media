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

MODULE  = $(CONFIG_MEDIA_SERVER)
CSRCS  += $(wildcard client/*.c)
CSRCS  += $(wildcard utils/*.c)
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/frameworks/media/utils

ifneq ($(CONFIG_MEDIA_FOCUS),)
  CSRCS  += server/media_focus.c server/focus_stack.c
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
BIN := $(APPDIR)/staging/libframework.a
endif

EXPORT_FILES := include

include $(APPDIR)/Application.mk
