#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <cstdint>
#include <string>
#include <vector>

#include "types/dumper_types.h"

namespace Engine::Services
{
constexpr size_t kCallLogMaxParamTypes = 4;
constexpr size_t kCallLogTypeNameBytes = 96;
constexpr size_t kCallLogLabelBytes    = 128;
constexpr size_t kCallLogTimeBytes     = 32;

std::string MakeCallLogDisplayLabel(const std::string& className, const std::string& methodName);

struct CallLogHookSpec {
    uint32_t                      hookId = 0;
    int                           slotIndex = -1;
    uintptr_t                     target = 0;
    std::string                   className{};
    std::string                   methodName{};
    std::string                   displayLabel{};
    bool                          isStatic = false;
    std::vector<Engine::MethodParam> paramTypes{};

    std::string DisplayLabel() const;
};

struct CallLogEvent {
    uint32_t  hookId = 0;
    bool      isStatic = false;
    uint8_t   paramCount = 0;
    char      timeLabel[kCallLogTimeBytes]{};
    char      displayLabel[kCallLogLabelBytes]{};
    char      paramTypeNames[kCallLogMaxParamTypes][kCallLogTypeNameBytes]{};
    uintptr_t argRegisters[4]{};
};
} // namespace Engine::Services

#endif // ENABLE_DUMPER
