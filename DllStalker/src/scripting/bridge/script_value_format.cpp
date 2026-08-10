#include "pch.h"

#ifdef ENABLE_DUMPER

#include "scripting/bridge/script_value_format.h"

#include <cstdio>

namespace Scripting
{
namespace
{
bool EndsWithDll(const std::string& name) {
    if (name.size() < 4) {
        return false;
    }
    const char a = name[name.size() - 4];
    const char b = name[name.size() - 3];
    const char c = name[name.size() - 2];
    const char d = name[name.size() - 1];
    return a == '.'
        && (b == 'd' || b == 'D')
        && (c == 'l' || c == 'L')
        && (d == 'l' || d == 'L');
}

std::string WithoutDllSuffix(const std::string& name) {
    if (EndsWithDll(name)) {
        return name.substr(0, name.size() - 4);
    }
    return name;
}
} // namespace

bool ImageNamesMatch(const std::string& requested, const std::string& runtimeName) {
    if (requested.empty() || runtimeName.empty()) {
        return false;
    }
    if (requested == runtimeName) {
        return true;
    }
    // Bare "Assembly-CSharp" ↔ "Assembly-CSharp.dll" (and reverse).
    return WithoutDllSuffix(requested) == WithoutDllSuffix(runtimeName);
}

std::string FormatScriptValueAsLuaInput(const ScriptValue& value) {
    switch (value.kind) {
    case ScriptValueKind::Invalid:
        return {};
    case ScriptValueKind::Nil:
        return "null";
    case ScriptValueKind::Boolean:
        return value.booleanValue ? "true" : "false";
    case ScriptValueKind::Integer:
        return std::to_string(value.integerValue);
    case ScriptValueKind::Unsigned:
        return std::to_string(value.unsignedValue);
    case ScriptValueKind::Number: {
        char buffer[64]{};
        snprintf(buffer, sizeof(buffer), "%.17g", value.numberValue);
        return buffer;
    }
    case ScriptValueKind::String:
        return value.stringValue;
    default:
        return {};
    }
}
} // namespace Scripting

#endif // ENABLE_DUMPER
