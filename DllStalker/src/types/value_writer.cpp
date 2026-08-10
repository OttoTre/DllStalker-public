#include "pch.h"

#ifdef ENABLE_DUMPER

#include "types/value_writer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "types/type_classifier.h"

namespace Engine::Write
{
namespace
{
void SetError(std::string* error, const char* message) {
    if (error) *error = message;
}

bool ParseFloatList(const std::string& input, size_t expected, std::vector<float>& out, std::string* error) {
    out.clear();
    out.reserve(expected);

    std::string buf = input;
    for (char& c : buf) {
        if (c == '(' || c == ')' || c == '[' || c == ']' || c == ',') c = ' ';
    }

    const char* p = buf.c_str();
    while (*p) {
        while (*p && std::isspace(static_cast<unsigned char>(*p))) ++p;
        if (!*p) break;
        char* end = nullptr;
        const float v = std::strtof(p, &end);
        if (end == p) {
            SetError(error, "Invalid format: expected float components");
            return false;
        }
        if (!std::isfinite(v)) {
            SetError(error, "Value must be a finite number");
            return false;
        }
        out.push_back(v);
        p = end;
    }

    if (out.size() != expected) {
        char msg[96];
        snprintf(msg, sizeof(msg), "Expected %zu float components, got %zu", expected, out.size());
        SetError(error, msg);
        return false;
    }
    return true;
}

bool WriteFloatComponents(uintptr_t addr, const std::vector<float>& values, std::string* error) {
    for (size_t i = 0; i < values.size(); ++i) {
        if (!Memory::TryWriteValue(addr + i * sizeof(float), values[i])) {
            SetError(error, "Memory write failed (Access Denied)");
            return false;
        }
    }
    return true;
}

bool WriteByteComponents(uintptr_t addr, const std::vector<uint8_t>& values, std::string* error) {
    for (size_t i = 0; i < values.size(); ++i) {
        if (!Memory::TryWriteValue(addr + i * sizeof(uint8_t), values[i])) {
            SetError(error, "Memory write failed (Access Denied)");
            return false;
        }
    }
    return true;
}

bool ParseByteList(const std::string& input, size_t expected, std::vector<uint8_t>& out, std::string* error) {
    out.clear();
    out.reserve(expected);

    std::string buf = input;
    for (char& c : buf) {
        if (c == '(' || c == ')' || c == '[' || c == ']' || c == ',') c = ' ';
    }

    const char* p = buf.c_str();
    while (*p) {
        while (*p && std::isspace(static_cast<unsigned char>(*p))) ++p;
        if (!*p) break;
        char* end = nullptr;
        const unsigned long v = std::strtoul(p, &end, 0);
        if (end == p) {
            SetError(error, "Invalid format: expected byte components");
            return false;
        }
        if (v > 255u) {
            SetError(error, "Byte component out of range (0-255)");
            return false;
        }
        out.push_back(static_cast<uint8_t>(v));
        p = end;
    }

    if (out.size() != expected) {
        char msg[96];
        snprintf(msg, sizeof(msg), "Expected %zu byte components, got %zu", expected, out.size());
        SetError(error, msg);
        return false;
    }
    return true;
}

bool WriteInlineFloats(uintptr_t addr, const std::string& input, size_t count, std::string* error) {
    std::vector<float> values;
    if (!ParseFloatList(input, count, values, error)) return false;
    return WriteFloatComponents(addr, values, error);
}
} // namespace

bool SetFieldValue(const Engine::FieldInfo& field, const std::string& newValue, std::string* error) {
    if (!field.hasValue || !field.valueAddress) {
        SetError(error, "No writable address");
        return false;
    }

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
        std::transform(low.begin(), low.end(), low.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        // Trim spaces
        while (!low.empty() && std::isspace(static_cast<unsigned char>(low.front()))) low.erase(low.begin());
        while (!low.empty() && std::isspace(static_cast<unsigned char>(low.back()))) low.pop_back();

        uint8_t b = 0;
        if (low == "true" || low == "1" || low == "yes") {
            b = 1;
        }
        else if (low == "false" || low == "0" || low == "no") {
            b = 0;
        }
        else {
            SetError(error, "Invalid boolean (use true/false, 1/0, yes/no)");
            return false;
        }
        if (!Memory::TryWriteValue(field.valueAddress, b)) {
            SetError(error, "Memory write failed (Access Denied)");
            return false;
        }
        return true;
    }
    case Cat::STRING:
        SetError(error, "String editing is handled by FieldCatalog");
        return false;
    case Cat::VEC2:
        return WriteInlineFloats(field.valueAddress, newValue, 2, error);
    case Cat::VEC3:
        return WriteInlineFloats(field.valueAddress, newValue, 3, error);
    case Cat::VEC4:
    case Cat::QUAT:
    case Cat::COLOR:
    case Cat::RECT:
        return WriteInlineFloats(field.valueAddress, newValue, 4, error);
    case Cat::COLOR32: {
        std::vector<uint8_t> bytes;
        if (!ParseByteList(newValue, 4, bytes, error)) return false;
        return WriteByteComponents(field.valueAddress, bytes, error);
    }
    default:
        if (error) *error = "Unsupported type: " + field.type;
        return false;
    }
}
} // namespace Engine::Write

#endif
