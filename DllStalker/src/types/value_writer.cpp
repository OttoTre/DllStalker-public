#include "pch.h"

#ifdef ENABLE_DUMPER

#include "types/value_writer.h"

#include <algorithm>
#include <cctype>
#include <cstdint>

#include "types/type_classifier.h"

namespace Engine::Write
{
bool SetFieldValue(const Engine::FieldInfo& field, const std::string& newValue, std::string* error) {
    if (!field.hasValue || !field.valueAddress) return false;

    using Cat = Types::TypeCategory;
    switch (Types::GetCategory(field.type)) {
    case Cat::I1: return ParseAndWrite<int8_t>(field.valueAddress, newValue, error);
    case Cat::I2: return ParseAndWrite<int16_t>(field.valueAddress, newValue, error);
    case Cat::I4: return ParseAndWrite<int32_t>(field.valueAddress, newValue, error);
    case Cat::I8: return ParseAndWrite<int64_t>(field.valueAddress, newValue, error);
    case Cat::U1: return ParseAndWrite<uint8_t>(field.valueAddress, newValue, error);
    case Cat::U2: return ParseAndWrite<uint16_t>(field.valueAddress, newValue, error);
    case Cat::U4: return ParseAndWrite<uint32_t>(field.valueAddress, newValue, error);
    case Cat::U8: return ParseAndWrite<uint64_t>(field.valueAddress, newValue, error);
    case Cat::R4: return ParseAndWrite<float>(field.valueAddress, newValue, error);
    case Cat::R8: return ParseAndWrite<double>(field.valueAddress, newValue, error);
    case Cat::BOOLEAN: {
        std::string low = newValue;
        std::transform(low.begin(), low.end(), low.begin(), ::tolower);
        uint8_t b = (low == "true" || low == "1" || low == "yes") ? 1 : 0;
        return Memory::TryWriteValue(field.valueAddress, b);
    }
    case Cat::STRING:
        if (error) *error = "String editing not supported";
        return false;
    default:
        if (error) *error = "Unsupported type: " + field.type;
        return false;
    }
}
} // namespace Engine::Write

#endif
