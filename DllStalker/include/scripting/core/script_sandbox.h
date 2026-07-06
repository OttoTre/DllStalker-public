#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <cstddef>
#include <cstdint>

namespace Scripting
{
enum class ScriptProfile : uint8_t {
    Safe = 0,
    Curated,
    Unrestricted,
};

struct SandboxLibraryPolicy {
    bool allowBase = false;
    bool allowTable = false;
    bool allowString = false;
    bool allowMath = false;
};

struct SandboxGatePolicy {
    bool allowUserFfi = false;
    bool allowPackageLoadlib = false;
    bool allowIo = false;
    bool allowOs = false;
    bool allowDebug = false;
    bool allowRawJitControl = false;
    bool allowWhitelistedRequireOnly = true;
};

struct SandboxPolicy {
    ScriptProfile profile = ScriptProfile::Safe;
    SandboxLibraryPolicy libraries{};
    SandboxGatePolicy gates{};
};

SandboxPolicy MakeDefaultSafePolicy() noexcept;
SandboxPolicy MakeDefaultCuratedPolicy() noexcept;
SandboxPolicy MakeDefaultUnrestrictedPolicy() noexcept;

// Names of base globals that Safe/Curated profiles must clear after opening base.
const char* const* GetBlockedBaseGlobals() noexcept;
size_t GetBlockedBaseGlobalCount() noexcept;

} // namespace Scripting

#endif // ENABLE_DUMPER
