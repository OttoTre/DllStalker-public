#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <cstdint>

#include "scripting/core/script_result.h"

namespace Scripting
{
// SEH/Lua boundary contract:
// 1) Native helpers run inside SEH and return DS_Status/ScriptResult normally.
// 2) Lua bindings convert to nil, err or luaL_error only after SEH has exited.
// Never call luaL_error / lua_error from inside an active __try region.

using SehProtectedStatusFn = DS_Status(__cdecl*)(void* context) noexcept;

__declspec(noinline) DS_Status RunSehProtected(SehProtectedStatusFn fn,
                                               void* context,
                                               unsigned long& outSehCode) noexcept;

} // namespace Scripting

#endif // ENABLE_DUMPER
