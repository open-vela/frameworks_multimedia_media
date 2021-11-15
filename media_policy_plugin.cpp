/*
 * Copyright (C) 2021 Xiaomi Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <nuttx/compiler.h>
#include <nuttx/symtab.h>
#include <string>

#include "InstanceConfigurableElement.h"
#include "LoggingElementBuilderTemplate.h"
#include "MappingContext.h"
#include "Plugin.h"
#include "Subsystem.h"
#include "SubsystemLibrary.h"
#include "SubsystemObject.h"
#include "SubsystemObjectFactory.h"

#include "media_internal.h"

/****************************************************************************
 * Pre-definitions
 ****************************************************************************/

#define QUOTE_(x) #x
#define QUOTE(x) QUOTE_(x)

enum PolicyItemType
{
    MappingKeyFilter
};

/****************************************************************************
 * FFmpegFilter : send command to target filter
 ****************************************************************************/

class FFmpegFilter : public CSubsystemObject
{
private:
    const std::string filter;
    const std::string command;
    const size_t paramSize;

public:
    FFmpegFilter(const std::string& mappingValue,
                 CInstanceConfigurableElement *instanceConfigurableElement,
                 const CMappingContext& context, core::log::Logger& logger)
        : CSubsystemObject(instanceConfigurableElement, logger),
          filter(context.getItem(MappingKeyFilter)),
          command(mappingValue),
          paramSize(getSize())
    {
    }

protected:
    bool sendToHW(std::string& /*error*/)
    {
        char *param = new char[paramSize];
        if (!param)
            return false;

        blackboardRead(param, paramSize);
        media_graph_process_command(media_get_graph(),
                                    filter.c_str(), command.c_str(), param, 0, 0);

        delete [] param;
        return true;
    }
};

/****************************************************************************
 * FFmpegSubsystem : define mapping keys and syner creators
 ****************************************************************************/

class FFmpegSubsystem : public CSubsystem
{
public:
    FFmpegSubsystem(const std::string& name, core::log::Logger& logger)
        : CSubsystem(name, logger)
    {
        addContextMappingKey("Filter");  // <- target filter's name

        addSubsystemObjectFactory(
            new TSubsystemObjectFactory<FFmpegFilter>(
                "Command",
                (1 << MappingKeyFilter))
            );
    }

private:
    /* Copy facilities are put private to disable copy. */
    FFmpegSubsystem(const FFmpegSubsystem& object);
    FFmpegSubsystem &operator=(const FFmpegSubsystem& object);
};

/****************************************************************************
 * plugin entry point
 ****************************************************************************/

static void entrypoint(CSubsystemLibrary* subsystemLibrary, core::log::Logger& logger)
{
    subsystemLibrary->addElementBuilder(
        "FFmpeg",
        new TLoggingElementBuilderTemplate<FFmpegSubsystem>(logger)
    );
}

extern const symtab_s PARAMETER_FRAMEWORK_PLUGIN_SYMTAB[] =
{
    {
        QUOTE(PARAMETER_FRAMEWORK_PLUGIN_ENTRYPOINT_V1),
        (const void*)entrypoint
    },
};

extern int PARAMETER_FRAMEWORK_PLUGIN_SYMTAB_SIZE =
    sizeof(PARAMETER_FRAMEWORK_PLUGIN_SYMTAB) /
    sizeof(PARAMETER_FRAMEWORK_PLUGIN_SYMTAB[0]);
