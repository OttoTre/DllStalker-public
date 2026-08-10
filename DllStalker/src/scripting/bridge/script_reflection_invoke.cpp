#include "pch.h"

#ifdef ENABLE_DUMPER

#include "scripting/bridge/script_reflection_api.h"

#include "scripting/bridge/script_value_format.h"

#include <cstdio>

#include "types/type_classifier.h"

namespace Scripting
{
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

    // I8/U8: reject Lua numbers (mantissa loss); require string like read path.
    if (category == Cat::I8 || category == Cat::U8) {
        if (value.kind != ScriptValueKind::String) {
            return ScriptApiResult::Fail(DS_Status::DS_ERR_ARG_TYPE_MISMATCH,
                                         "I8/U8 requires a string literal (Lua number is lossy)");
        }
        outInput = value.stringValue;
        return ScriptApiResult::Ok();
    }

    if (category == Cat::ARRAY || category == Cat::LIST
        || Engine::Types::IsInlineValueStruct(category)
        || category == Cat::UNKNOWN) {
        return ScriptApiResult::Fail(DS_Status::DS_ERR_UNSUPPORTED_TYPE,
                                     "unsupported parameter type");
    }

    outInput = FormatScriptValueAsLuaInput(value);
    if (outInput.empty()) {
        return ScriptApiResult::Fail(DS_Status::DS_ERR_ARG_TYPE_MISMATCH,
                                     "argument value type mismatch");
    }
    return ScriptApiResult::Ok();
}

} // namespace Scripting

#endif // ENABLE_DUMPER
