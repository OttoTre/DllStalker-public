#pragma once

#include "pch.h"

#ifdef ENABLE_DUMPER

#include <cstdint>
#include <string>
#include <vector>

#include "types/dumper_types.h"

namespace Engine::Services
{
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
} // namespace Engine::Services

#endif // ENABLE_DUMPER
