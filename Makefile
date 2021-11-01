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

ifneq ($(CONFIG_MEDIA),)

MODULE = $(CONFIG_MEDIA_SERVER)
CSRCS += media_proxy.c media_parcel.c

ifneq ($(CONFIG_MEDIA_SERVER),)
  CSRCS    += media_graph.c media_stub.c
  MAINSRC   = media_daemon.c
  PROGNAME  = mediad
  PRIORITY  = $(CONFIG_MEDIA_SERVER_PRIORITY)
  STACKSIZE = $(CONFIG_MEDIA_SERVER_STACKSIZE)

  CFLAGS += ${shell $(INCDIR) $(INCDIROPT) "$(CC)" $(APPDIR)/external/ffmpeg} -DMEDIA_NO_RPC
endif

ifneq ($(CONFIG_PFW),)
  CSRCS    += media_policy.c
  CFLAGS += ${shell $(INCDIR) $(INCDIROPT) "$(CC)" $(APPDIR)/external/parameter-framework}

  CXXEXT   := .cpp
  CXXSRCS  += media_policy_plugin.cpp
  CXXFLAGS += -fpermissive
  CXXFLAGS += ${shell $(INCDIR) $(INCDIROPT) "$(CC)" $(APPDIR)/external/parameter-framework}
  CXXFLAGS += ${shell $(INCDIR) $(INCDIROPT) "$(CC)" $(APPDIR)/external/parameter-framework/parameter}
  CXXFLAGS += ${shell $(INCDIR) $(INCDIROPT) "$(CC)" $(APPDIR)/external/parameter-framework/parameter/log/include}
  CXXFLAGS += ${shell $(INCDIR) $(INCDIROPT) "$(CC)" $(APPDIR)/external/parameter-framework/xmlserializer}
  CXXFLAGS += ${shell $(INCDIR) $(INCDIROPT) "$(CC)" $(APPDIR)/external/parameter-framework/utility}
endif

ifneq ($(CONFIG_MEDIA_TOOL),)
  MAINSRC   += media_tool.c
  CSRCS     += media_toolrpc.c
  PROGNAME  += mediatool
  PRIORITY  += $(CONFIG_MEDIA_TOOL_PRIORITY)
  STACKSIZE += $(CONFIG_MEDIA_TOOL_STACKSIZE)
endif

endif # CONFIG_MEDIA

include $(APPDIR)/Application.mk
