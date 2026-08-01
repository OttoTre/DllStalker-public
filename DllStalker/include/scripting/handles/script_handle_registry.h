#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <cstdint>
#include <mutex>
#include <vector>

#include "scripting/core/script_context.h"
#include "scripting/core/script_result.h"
#include "scripting/handles/script_handles.h"

namespace Scripting
{
struct HandleRegistryEntry {
    uint32_t        registryIndex = 0;
    uint32_t        generation = 0;
    uintptr_t       nativeAddress = 0;
    ScriptHandleKind kind = ScriptHandleKind::Invalid;
    RuntimeKind     runtimeKind = RuntimeKind::Unknown;
    uint64_t        expectedClassIdentity = 0;
    ScriptInstanceId ownerScriptId{};
    bool            isAlive = false;
};

// Result of execution-time handle resolution. transientNativeAddress must not be
// cached across enqueue; validation and use must happen inside the executing task.
struct ResolvedHandle {
    DS_Status status = DS_Status::DS_ERR_HANDLE_INVALID;
    uintptr_t transientNativeAddress = 0;
    ScriptHandleKind kind = ScriptHandleKind::Invalid;
    RuntimeKind runtimeKind = RuntimeKind::Unknown;
    uint64_t expectedClassIdentity = 0;
};

struct RegisterHandleRequest {
    uintptr_t       nativeAddress = 0;
    ScriptHandleKind kind = ScriptHandleKind::Invalid;
    RuntimeKind     runtimeKind = RuntimeKind::Unknown;
    uint64_t        expectedClassIdentity = 0;
    ScriptInstanceId ownerScriptId{};
};

class ScriptHandleRegistry {
public:
    ScriptHandle Register(const RegisterHandleRequest& request);
    void InvalidateOwnedBy(const ScriptInstanceId& ownerScriptId) noexcept;

    // Validates structural ownership/kind at call time, then probes native readability.
    ResolvedHandle ResolveAndValidate(ScriptHandle handle,
                                      const ScriptInstanceId& currentScriptId,
                                      ScriptHandleKind expectedKind) const noexcept;

private:
    DS_Status ValidateStructural(const HandleRegistryEntry& entry,
                                 ScriptHandle handle,
                                 const ScriptInstanceId& currentScriptId,
                                 ScriptHandleKind expectedKind) const noexcept;

    // Cheap liveness: reject null/unreadable native slots via memory_guard (not full GC liveness).
    DS_Status ProbeUnityLiveness(const HandleRegistryEntry& entry) const noexcept;

    mutable std::mutex mutex_{};
    std::vector<HandleRegistryEntry> entries_{};
    uint32_t nextGeneration_ = 1;
};

} // namespace Scripting

#endif // ENABLE_DUMPER
