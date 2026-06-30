#include "pch.h"

#ifdef ENABLE_DUMPER

#include "scripting/bridge/script_reflection_api.h"

#include <cstdio>
#include <type_traits>

#include "types/memory_guard.h"
#include "types/type_classifier.h"
#include "types/value_decoder.h"

namespace Scripting
{
namespace
{
template <typename T>
bool ReadNumeric(uintptr_t address, ScriptValue& outValue) {
    T value{};
    if (!Engine::Memory::TryReadValue(address, value)) {
        return false;
    }

    if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
        outValue.kind = ScriptValueKind::Number;
        outValue.numberValue = static_cast<double>(value);
    } else if constexpr (std::is_unsigned_v<T>) {
        outValue.kind = ScriptValueKind::Unsigned;
        outValue.unsignedValue = static_cast<uint64_t>(value);
    } else {
        outValue.kind = ScriptValueKind::Integer;
        outValue.integerValue = static_cast<int64_t>(value);
    }
    return true;
}

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

ScriptFieldValueResult ScriptReflectionApi::ReadFieldValue(const Engine::FieldInfo& field,
                                                           const ScriptInstanceId& scriptId) {
    ScriptFieldValueResult result{};
    result.typeName = field.type;
    if (!field.hasValue || !field.valueAddress) {
        result.result = ScriptApiResult::Fail(DS_Status::DS_ERR_INVALID_POINTER,
                                              "field has no value address");
        return result;
    }

    using Cat = Engine::Types::TypeCategory;
    const std::string decodeType = field.isEnum && !field.underlyingType.empty()
                                 ? field.underlyingType : field.type;
    const Cat category = Engine::Types::GetCategory(decodeType);

    bool readOk = true;
    switch (category) {
    case Cat::I1: readOk = ReadNumeric<int8_t>(field.valueAddress, result.value); break;
    case Cat::I2: readOk = ReadNumeric<int16_t>(field.valueAddress, result.value); break;
    case Cat::I4: readOk = ReadNumeric<int32_t>(field.valueAddress, result.value); break;
    case Cat::I8: {
        int64_t rawValue = 0;
        readOk = Engine::Memory::TryReadValue(field.valueAddress, rawValue);
        if (readOk) {
            result.value.kind = ScriptValueKind::String;
            result.value.stringValue = std::to_string(rawValue);
        }
        break;
    }
    case Cat::U1: readOk = ReadNumeric<uint8_t>(field.valueAddress, result.value); break;
    case Cat::U2: readOk = ReadNumeric<uint16_t>(field.valueAddress, result.value); break;
    case Cat::U4: readOk = ReadNumeric<uint32_t>(field.valueAddress, result.value); break;
    case Cat::U8: {
        uint64_t rawValue = 0;
        readOk = Engine::Memory::TryReadValue(field.valueAddress, rawValue);
        if (readOk) {
            result.value.kind = ScriptValueKind::String;
            result.value.stringValue = std::to_string(rawValue);
        }
        break;
    }
    case Cat::R4: readOk = ReadNumeric<float>(field.valueAddress, result.value); break;
    case Cat::R8: readOk = ReadNumeric<double>(field.valueAddress, result.value); break;
    case Cat::BOOLEAN: {
        uint8_t value = 0;
        readOk = Engine::Memory::TryReadValue(field.valueAddress, value);
        result.value.kind = ScriptValueKind::Boolean;
        result.value.booleanValue = value != 0;
        break;
    }
    case Cat::STRING: {
        uintptr_t managedString = 0;
        if (!Engine::Memory::TryReadValue(field.valueAddress, managedString)) {
            readOk = false;
            break;
        }
        if (managedString == 0) {
            result.value.kind = ScriptValueKind::Nil;
            break;
        }
        result.value.kind = ScriptValueKind::String;
        result.value.stringValue =
            Engine::Decode::StripQuotesForFieldEdit(Engine::Decode::DecodeManagedString(field.valueAddress));
        break;
    }
    case Cat::PTR: {
        uintptr_t objectPtr = 0;
        if (!Engine::Memory::TryReadValue(field.valueAddress, objectPtr)) {
            readOk = false;
            break;
        }
        if (objectPtr == 0) {
            result.value.kind = ScriptValueKind::Nil;
            break;
        }

        void* objectClass = nullptr;
        dumper_.TryGetClassNameFromInstance(reinterpret_cast<void*>(objectPtr), &objectClass);
        result.value.kind = ScriptValueKind::Handle;
        result.value.handleValue = RegisterHandle(scriptId,
                                                  ScriptHandleKind::Instance,
                                                  objectPtr,
                                                  reinterpret_cast<uint64_t>(objectClass));
        break;
    }
    default:
        result.result = ScriptApiResult::Fail(DS_Status::DS_ERR_UNSUPPORTED_TYPE,
                                              "unsupported field type");
        return result;
    }

    if (!readOk) {
        result.result = ScriptApiResult::Fail(DS_Status::DS_ERR_INVALID_POINTER,
                                              "field read failed");
        return result;
    }

    result.result = ScriptApiResult::Ok();
    return result;
}

ScriptApiResult ScriptReflectionApi::ConvertValueToFieldInput(const ScriptValue& value,
                                                              const Engine::FieldInfo& field,
                                                              std::string& outInput) const {
    using Cat = Engine::Types::TypeCategory;
    const std::string typeName = field.isEnum && !field.underlyingType.empty()
                               ? field.underlyingType : field.type;
    const Cat category = Engine::Types::GetCategory(typeName);

    if (category == Cat::STRING) {
        if (value.kind == ScriptValueKind::Invalid) {
            return ScriptApiResult::Fail(DS_Status::DS_ERR_ARG_TYPE_MISMATCH,
                                         "string field expects string or nil");
        }
        if (value.kind == ScriptValueKind::Nil) {
            outInput = "null";
            return ScriptApiResult::Ok();
        }
        if (value.kind != ScriptValueKind::String) {
            return ScriptApiResult::Fail(DS_Status::DS_ERR_ARG_TYPE_MISMATCH,
                                         "string field expects string or nil");
        }
        outInput = value.stringValue;
        return ScriptApiResult::Ok();
    }

    if (category == Cat::BOOLEAN && value.kind != ScriptValueKind::Boolean) {
        return ScriptApiResult::Fail(DS_Status::DS_ERR_ARG_TYPE_MISMATCH,
                                     "boolean field expects boolean");
    }

    if (category == Cat::ARRAY || category == Cat::LIST || category == Cat::VEC3 ||
        category == Cat::UNKNOWN || category == Cat::PTR) {
        return ScriptApiResult::Fail(DS_Status::DS_ERR_UNSUPPORTED_TYPE,
                                     "unsupported field type");
    }

    outInput = FormatLuaInput(value);
    if (outInput.empty() && value.kind != ScriptValueKind::String) {
        return ScriptApiResult::Fail(DS_Status::DS_ERR_ARG_TYPE_MISMATCH,
                                     "field value type mismatch");
    }
    return ScriptApiResult::Ok();
}

} // namespace Scripting

#endif // ENABLE_DUMPER
