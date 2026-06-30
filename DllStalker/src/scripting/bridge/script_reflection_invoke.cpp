#include "pch.h"

#ifdef ENABLE_DUMPER

#include "scripting/bridge/script_reflection_api.h"

#include <cstdio>

#include "types/type_classifier.h"

namespace Scripting
{
namespace
{
std::string FormatLuaInput(const ScriptValue& value) {
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

} // namespace

ScriptApiResult ScriptReflectionApi::ConvertValueToInvokeInput(const ScriptValue& value,
                                                               const Engine::MethodParam& param,
                                                               const ScriptInstanceId& scriptId,
                                                               std::string& outInput) const {
    using Cat = Engine::Types::TypeCategory;
    const std::string typeName = param.isEnum && !param.underlyingType.empty()
                               ? param.underlyingType : param.typeName;
    const Cat category = Engine::Types::GetCategory(typeName);

    if (value.kind == ScriptValueKind::Invalid) {
        return ScriptApiResult::Fail(DS_Status::DS_ERR_ARG_TYPE_MISMATCH,
                                     "unsupported Lua argument type");
    }

    if (value.kind == ScriptValueKind::Nil) {
        if (category == Cat::STRING || category == Cat::PTR) {
            outInput = "null";
            return ScriptApiResult::Ok();
        }
        return ScriptApiResult::Fail(DS_Status::DS_ERR_ARG_TYPE_MISMATCH,
                                     "nil is only valid for reference/string parameters");
    }

    if (category == Cat::STRING) {
        if (value.kind != ScriptValueKind::String) {
            return ScriptApiResult::Fail(DS_Status::DS_ERR_ARG_TYPE_MISMATCH,
                                         "string parameter expects string");
        }
        outInput = value.stringValue;
        return ScriptApiResult::Ok();
    }

    if (category == Cat::PTR) {
        if (value.kind != ScriptValueKind::Handle) {
            return ScriptApiResult::Fail(DS_Status::DS_ERR_ARG_TYPE_MISMATCH,
                                         "object parameter expects instance handle");
        }
        const ResolvedHandle resolved =
            registry_.ResolveAndValidate(value.handleValue, scriptId, ScriptHandleKind::Instance);
        if (!IsOk(resolved.status)) {
            return ScriptApiResult::Fail(resolved.status, "object argument is dead");
        }
        char buffer[32]{};
        snprintf(buffer, sizeof(buffer), "0x%llX",
                 static_cast<unsigned long long>(resolved.transientNativeAddress));
        outInput = buffer;
        return ScriptApiResult::Ok();
    }

    if (category == Cat::BOOLEAN && value.kind != ScriptValueKind::Boolean) {
        return ScriptApiResult::Fail(DS_Status::DS_ERR_ARG_TYPE_MISMATCH,
                                     "boolean parameter expects boolean");
    }

    if (category == Cat::ARRAY || category == Cat::LIST || category == Cat::VEC3 ||
        category == Cat::UNKNOWN) {
        return ScriptApiResult::Fail(DS_Status::DS_ERR_UNSUPPORTED_TYPE,
                                     "unsupported parameter type");
    }

    outInput = FormatLuaInput(value);
    if (outInput.empty()) {
        return ScriptApiResult::Fail(DS_Status::DS_ERR_ARG_TYPE_MISMATCH,
                                     "argument value type mismatch");
    }
    return ScriptApiResult::Ok();
}

} // namespace Scripting

#endif // ENABLE_DUMPER
