#include "pch.h"

#ifdef ENABLE_DUMPER

#include "scripting/core/script_sandbox.h"

namespace Scripting
{
namespace
{
constexpr const char* kBlockedBaseGlobals[] = {
    "dofile",
    "loadfile",
    "load",
    "loadstring",
    "newproxy",
    "collectgarbage",
};
} // namespace

SandboxPolicy MakeDefaultSafePolicy() noexcept {
    SandboxPolicy policy{};
    policy.profile = ScriptProfile::Safe;
    policy.libraries.allowBase = true;
    policy.libraries.allowTable = true;
    policy.libraries.allowString = true;
    policy.libraries.allowMath = true;
    policy.gates.allowWhitelistedRequireOnly = true;
    return policy;
}

SandboxPolicy MakeDefaultCuratedPolicy() noexcept {
    SandboxPolicy policy = MakeDefaultSafePolicy();
    policy.profile = ScriptProfile::Curated;
    return policy;
}

SandboxPolicy MakeDefaultUnrestrictedPolicy() noexcept {
    SandboxPolicy policy{};
    policy.profile = ScriptProfile::Unrestricted;
    policy.gates.allowUserFfi = true;
    policy.gates.allowPackageLoadlib = true;
    policy.gates.allowIo = true;
    policy.gates.allowOs = true;
    policy.gates.allowDebug = true;
    policy.gates.allowRawJitControl = true;
    policy.gates.allowWhitelistedRequireOnly = false;
    return policy;
}

const char* const* GetBlockedBaseGlobals() noexcept {
    return kBlockedBaseGlobals;
}

size_t GetBlockedBaseGlobalCount() noexcept {
    return sizeof(kBlockedBaseGlobals) / sizeof(kBlockedBaseGlobals[0]);
}

} // namespace Scripting

#endif // ENABLE_DUMPER
