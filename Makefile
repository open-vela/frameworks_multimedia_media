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

MODULE = $(CONFIG_MEDIA_SERVER)
CSRCS += media_proxy.c media_parcel.c media_client.c media_wrapper.c

ifneq ($(CONFIG_MEDIA_FOCUS),)
  CSRCS  += media_focus.c
  CFLAGS += ${shell $(INCDIR) $(INCDIROPT) "$(CC)" $(APPDIR)/frameworks/utils/include}
endif

ifneq ($(CONFIG_MEDIA_SERVER),)
  CSRCS    += media_graph.c media_stub.c media_server.c media_policy.c
  MAINSRC   = media_daemon.c
  PROGNAME  = $(CONFIG_MEDIA_SERVER_PROGNAME)
  PRIORITY  = $(CONFIG_MEDIA_SERVER_PRIORITY)
  STACKSIZE = $(CONFIG_MEDIA_SERVER_STACKSIZE)

  CFLAGS += ${shell $(INCDIR) $(INCDIROPT) "$(CC)" $(APPDIR)/external/ffmpeg/ffmpeg}
  CFLAGS += ${shell $(INCDIR) $(INCDIROPT) "$(CC)" $(APPDIR)/external/parameter-framework/parameter-framework/bindings/c}
endif

ifneq ($(CONFIG_PFW),)
  CXXEXT   := .cpp
  CXXSRCS  += media_policy_plugin.cpp
  CXXFLAGS += ${shell $(INCDIR) $(INCDIROPT) "$(CC)" $(APPDIR)/external/parameter-framework/parameter-framework/parameter}
  CXXFLAGS += ${shell $(INCDIR) $(INCDIROPT) "$(CC)" $(APPDIR)/external/parameter-framework/parameter-framework/parameter/include}
  CXXFLAGS += ${shell $(INCDIR) $(INCDIROPT) "$(CC)" $(APPDIR)/external/parameter-framework/parameter-framework/parameter/log/include}
  CXXFLAGS += ${shell $(INCDIR) $(INCDIROPT) "$(CC)" $(APPDIR)/external/parameter-framework/parameter-framework/xmlserializer}
  CXXFLAGS += ${shell $(INCDIR) $(INCDIROPT) "$(CC)" $(APPDIR)/external/parameter-framework/parameter-framework/utility}
endif

ifneq ($(CONFIG_MEDIA_TOOL),)
  MAINSRC   += media_tool.c
  CSRCS     += media_toolrpc.c
  PROGNAME  += mediatool
  PRIORITY  += $(CONFIG_MEDIA_TOOL_PRIORITY)
  STACKSIZE += $(CONFIG_MEDIA_TOOL_STACKSIZE)
endif

include $(APPDIR)/Application.mk
