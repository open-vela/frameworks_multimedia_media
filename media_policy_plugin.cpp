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

#include <fcntl.h>
#include <nuttx/audio/audio.h>
#include <nuttx/compiler.h>
#include <nuttx/symtab.h>
#include <string>
#include <sys/ioctl.h>

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

/****************************************************************************
 * FFmpegCommander : send commands to media graph
 ****************************************************************************/

class FFmpegCommander : public CSubsystemObject {
private:
    const size_t paramSize;

public:
    FFmpegCommander(const std::string& mappingValue,
        CInstanceConfigurableElement* instanceConfigurableElement,
        const CMappingContext& context, core::log::Logger& logger)
        : CSubsystemObject(instanceConfigurableElement, logger)
        , paramSize(getSize())
    {
    }

protected:
    bool sendToHW(std::string& /*error*/)
    {
        char *target, *cmd, *arg, *params;
        char *outptr, *inptr;

        params = new char[paramSize];
        if (!params)
            return false;
        blackboardRead(params, paramSize);

        target = strtok_r(params, ";", &outptr);
        while (target) {
            target = strtok_r(target, ",", &inptr);
            if (!target) {
                delete[] params;
                return false;
            }

            cmd = strtok_r(NULL, ",", &inptr);
            if (!cmd) {
                delete[] params;
                return false;
            }

            arg = strtok_r(NULL, ",", &inptr);

            media_policy_process_command(target, cmd, arg);

            target = strtok_r(NULL, ";", &outptr);
        }

        delete[] params;
        return true;
    }
};

class MixerCommander : public CSubsystemObject {
private:
    const size_t paramSize;

public:
    MixerCommander(const std::string& mappingValue,
        CInstanceConfigurableElement* instanceConfigurableElement,
        const CMappingContext& context, core::log::Logger& logger)
        : CSubsystemObject(instanceConfigurableElement, logger)
        , paramSize(getSize())
    {
    }

protected:
    bool sendToHW(std::string& /*error*/)
    {
        char *target, *arg, *params;
        char *outptr, *saveptr;
        int fd;

        params = new char[paramSize];
        if (!params)
            return false;
        blackboardRead(params, paramSize);

        /* params format : target_1,args_1;target_2,args_2;...;target_n,args_n
         * example : dev/audio/mixer1,mode=normal,outdev0=speaker;dev/audio/mixer2,indev=mic;
         */

        outptr = strtok_r(params, ";", &saveptr);

        while (outptr) {
            target = strtok_r(outptr, ",", &arg);
            if (target == NULL) {
                delete[] params;
                return false;
            }

            if (arg != NULL && target != NULL) {
                fd = open(target, O_RDWR | O_CLOEXEC);
                if (fd < 0) {
                    delete[] params;
                    return false;
                }
                if (ioctl(fd, AUDIOIOC_SETPARAMTER, arg) < 0) {
                    delete[] params;
                    close(fd);
                    return false;
                }
                close(fd);
            }

            outptr = strtok_r(NULL, ";", &saveptr);
        }

        delete[] params;

        return true;
    }
};

/****************************************************************************
 * FFmpegSubsystem : define mapping keys and syner creators
 ****************************************************************************/

class FFmpegSubsystem : public CSubsystem {
public:
    FFmpegSubsystem(const std::string& name, core::log::Logger& logger)
        : CSubsystem(name, logger)
    {
        addSubsystemObjectFactory(
            new TSubsystemObjectFactory<FFmpegCommander>("Commander", 0));
    }

private:
    /* Copy facilities are put private to disable copy. */
    FFmpegSubsystem(const FFmpegSubsystem& object);
    FFmpegSubsystem& operator=(const FFmpegSubsystem& object);
};

class MixerSubsystem : public CSubsystem {
public:
    MixerSubsystem(const std::string& name, core::log::Logger& logger)
        : CSubsystem(name, logger)
    {
        addSubsystemObjectFactory(
            new TSubsystemObjectFactory<MixerCommander>("Commander", 0));
    }

private:
    /* Copy facilities are put private to disable copy. */
    MixerSubsystem(const MixerSubsystem& object);
    MixerSubsystem& operator=(const MixerSubsystem& object);
};

/****************************************************************************
 * plugin entry point
 ****************************************************************************/

static void entrypoint(CSubsystemLibrary* subsystemLibrary,
    core::log::Logger& logger)
{
    subsystemLibrary->addElementBuilder(
        "FFmpeg", new TLoggingElementBuilderTemplate<FFmpegSubsystem>(logger));

    subsystemLibrary->addElementBuilder(
        "Mixer", new TLoggingElementBuilderTemplate<MixerSubsystem>(logger));
}

/* let linker find symbol in parameter-framework lib */
extern const symtab_s PARAMETER_FRAMEWORK_PLUGIN_SYMTAB[];
extern int PARAMETER_FRAMEWORK_PLUGIN_SYMTAB_SIZE;

const symtab_s PARAMETER_FRAMEWORK_PLUGIN_SYMTAB[] = {
    { QUOTE(PARAMETER_FRAMEWORK_PLUGIN_ENTRYPOINT_V1), (const void*)entrypoint },
};

int PARAMETER_FRAMEWORK_PLUGIN_SYMTAB_SIZE = sizeof(PARAMETER_FRAMEWORK_PLUGIN_SYMTAB) / sizeof(PARAMETER_FRAMEWORK_PLUGIN_SYMTAB[0]);
