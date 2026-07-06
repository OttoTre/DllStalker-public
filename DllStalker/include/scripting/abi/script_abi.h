#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <cstdint>

#include "scripting/core/script_result.h"
#include "scripting/handles/script_handles.h"

// Curated ABI exported from the proxy DLL itself. Keep this surface flat and
// primitive-only so LuaJIT helpers are insulated from C++ implementation churn.
#define DS_API extern "C" __declspec(dllexport)

constexpr uint32_t DS_ABI_VERSION = 100;
constexpr uint32_t DS_ABI_FEATURE_INSTANCE_PRIMITIVES = 1u << 0;
constexpr uint32_t DS_ABI_FEATURE_SINGLE_PROXY_DLL = 1u << 1;

DS_API uint32_t __cdecl DS_Core_GetAbiVersion();
DS_API uint32_t __cdecl DS_Core_GetFeatureFlags();
DS_API int32_t __cdecl DS_Core_GetBuildId(char* outBuffer, uint32_t bufferSize);

DS_API int32_t __cdecl DS_Instance_ReadI32(uint64_t scriptHandle,
                                           uint32_t offset,
                                           int32_t* outValue);
DS_API int32_t __cdecl DS_Instance_WriteI32(uint64_t scriptHandle,
                                            uint32_t offset,
                                            int32_t value);

namespace Scripting
{
struct LuaScriptHostContext;

void BindCurrentScriptAbiContext(LuaScriptHostContext* hostContext) noexcept;
void ClearCurrentScriptAbiContext(LuaScriptHostContext* hostContext) noexcept;

DS_Status AbiInstanceReadI32(ScriptHandle scriptHandle, uint32_t offset, int32_t& outValue) noexcept;
DS_Status AbiInstanceWriteI32(ScriptHandle scriptHandle, uint32_t offset, int32_t value) noexcept;

} // namespace Scripting

#endif // ENABLE_DUMPER
