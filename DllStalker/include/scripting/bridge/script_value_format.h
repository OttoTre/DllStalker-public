#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <string>

#include "scripting/bridge/script_value.h"

namespace Scripting
{
// True when requested matches runtime image name, or either side differs only
// by a trailing ".dll".
bool ImageNamesMatch(const std::string& requested, const std::string& runtimeName);

// Shared Lua/native stringifier for field write + invoke args.
std::string FormatScriptValueAsLuaInput(const ScriptValue& value);
} // namespace Scripting

#endif // ENABLE_DUMPER
