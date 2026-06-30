#include "pch.h"

#ifdef ENABLE_DUMPER

#include "scripting/handles/script_handle_registry.h"

#include "types/memory_guard.h"

namespace Scripting
{
ScriptHandle ScriptHandleRegistry::Register(const RegisterHandleRequest& request) {
    std::lock_guard<std::mutex> lock(mutex_);
    const uint32_t index = static_cast<uint32_t>(entries_.size());
    HandleRegistryEntry entry{};
    entry.registryIndex = index;
    entry.generation = nextGeneration_++;
    entry.nativeAddress = request.nativeAddress;
    entry.kind = request.kind;
    entry.runtimeKind = request.runtimeKind;
    entry.expectedClassIdentity = request.expectedClassIdentity;
    entry.unityInstanceId = request.unityInstanceId;
    entry.ownerScriptId = request.ownerScriptId;
    entry.isAlive = request.kind != ScriptHandleKind::Invalid && request.nativeAddress != 0;
    entries_.push_back(entry);
    return PackHandle(index, entry.generation);
}

void ScriptHandleRegistry::InvalidateOwnedBy(const ScriptInstanceId& ownerScriptId) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    for (HandleRegistryEntry& entry : entries_) {
        if (entry.ownerScriptId == ownerScriptId) {
            entry.isAlive = false;
            ++entry.generation;
        }
    }
}

DS_Status ScriptHandleRegistry::ValidateStructural(const HandleRegistryEntry& entry,
                                                   ScriptHandle handle,
                                                   const ScriptInstanceId& currentScriptId,
                                                   ScriptHandleKind expectedKind) const noexcept {
    if (!IsValidScriptHandle(handle)) {
        return DS_Status::DS_ERR_HANDLE_INVALID;
    }

    const uint32_t index = GetRegistryIndex(handle);
    if (index >= entries_.size()) {
        return DS_Status::DS_ERR_HANDLE_INVALID;
    }

    if (entry.generation != GetHandleGeneration(handle)) {
        return DS_Status::DS_ERR_HANDLE_INVALID;
    }

    if (!entry.isAlive) {
        return DS_Status::DS_ERR_STALE_OBJECT;
    }

    if (entry.ownerScriptId.id != currentScriptId.id
        || entry.ownerScriptId.generation != currentScriptId.generation) {
        return DS_Status::DS_ERR_HANDLE_INVALID;
    }

    if (expectedKind != ScriptHandleKind::Invalid && entry.kind != expectedKind) {
        return DS_Status::DS_ERR_HANDLE_KIND_MISMATCH;
    }

    return DS_Status::DS_OK;
}

DS_Status ScriptHandleRegistry::ProbeUnityLiveness(const HandleRegistryEntry& entry) const noexcept {
    if (entry.nativeAddress == 0) {
        return DS_Status::DS_ERR_STALE_OBJECT;
    }

    const auto* nativePointer = reinterpret_cast<const void*>(entry.nativeAddress);
    if (!Engine::Memory::IsReadablePointer(nativePointer, sizeof(void*))) {
        return DS_Status::DS_ERR_INVALID_POINTER;
    }

    return DS_Status::DS_OK;
}

ResolvedHandle ScriptHandleRegistry::ResolveAndValidate(ScriptHandle handle,
                                                        const ScriptInstanceId& currentScriptId,
                                                        ScriptHandleKind expectedKind) const noexcept {
    ResolvedHandle result{};

    if (!IsValidScriptHandle(handle)) {
        result.status = DS_Status::DS_ERR_HANDLE_INVALID;
        return result;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const uint32_t index = GetRegistryIndex(handle);
    if (index >= entries_.size()) {
        result.status = DS_Status::DS_ERR_HANDLE_INVALID;
        return result;
    }

    const HandleRegistryEntry& entry = entries_[index];
    result.status = ValidateStructural(entry, handle, currentScriptId, expectedKind);
    if (!IsOk(result.status)) {
        return result;
    }

    result.status = ProbeUnityLiveness(entry);
    if (!IsOk(result.status)) {
        return result;
    }

    result.transientNativeAddress = entry.nativeAddress;
    result.kind = entry.kind;
    result.runtimeKind = entry.runtimeKind;
    result.expectedClassIdentity = entry.expectedClassIdentity;
    result.unityInstanceId = entry.unityInstanceId;
    return result;
}
} // namespace Scripting

#endif // ENABLE_DUMPER
