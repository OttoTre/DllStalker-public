#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "scripting/core/script_result.h"
#include "scripting/handles/script_handles.h"

namespace Scripting
{
enum class ScriptValueKind : uint8_t {
    Invalid = 0,
    Nil,
    Boolean,
    Integer,
    Unsigned,
    Number,
    String,
    Handle,
};

struct ScriptValue {
    ScriptValueKind kind = ScriptValueKind::Nil;
    bool booleanValue = false;
    int64_t integerValue = 0;
    uint64_t unsignedValue = 0;
    double numberValue = 0.0;
    std::string stringValue;
    ScriptHandle handleValue = kInvalidScriptHandle;
};

struct ScriptApiResult {
    DS_Status status = DS_Status::DS_ERR_BAD_ARGUMENT;
    std::string message;

    static ScriptApiResult Ok() { return {DS_Status::DS_OK, {}}; }
    static ScriptApiResult Fail(DS_Status status, std::string message = {}) {
        return {status, std::move(message)};
    }
};

struct ScriptProxyInfo {
    ScriptHandle handle = kInvalidScriptHandle;
    ScriptHandleKind kind = ScriptHandleKind::Invalid;
    std::string name;
    std::string ns;
    std::string typeName;
};

struct ScriptImageInfo {
    ScriptProxyInfo proxy;
    int classCount = 0;
};

struct ScriptFieldValueResult {
    ScriptApiResult result;
    ScriptValue value;
    std::string typeName;
};

struct ScriptInvokeResult {
    ScriptApiResult result;
    ScriptValue value;
};

struct ScriptImagesResult {
    ScriptApiResult result;
    std::vector<ScriptImageInfo> images;
};

struct ParsedMethodSignature {
    std::string name;
    std::vector<std::string> parameterTypes;
};

} // namespace Scripting

#endif // ENABLE_DUMPER
