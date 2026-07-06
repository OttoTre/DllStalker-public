#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <cstdint>
#include <string>
#include <type_traits>

#include "types/dumper_types.h"
#include "types/memory_guard.h"

// Numeric / boolean field-write dispatch. Parses the user-typed string,
// validates the target pointer is writable, then commits with TryWriteValue.
// System.String writes live in FieldCatalog (managed string_new + pointer
// slot). Pointer, array, and list fields are rejected here: editing them
// would bypass GC bookkeeping or are navigation targets, not scalars.
namespace Engine::Write
{
// Centralized "parse number, write number" for any arithmetic T. On
// success returns true and leaves *err untouched. On any failure returns
// false and -- if err != nullptr -- writes a human-readable reason into
// it. Templated so a single body covers I1..U8 / R4 / R8.
template<typename T>
bool ParseAndWrite(uintptr_t addr, const std::string& input, std::string* err) {
    try {
        T val;
        if constexpr (std::is_floating_point_v<T>) {
            val = static_cast<T>(std::stod(input));
        }
        else if constexpr (std::is_signed_v<T>) {
            val = static_cast<T>(std::stoll(input, nullptr, 0));
        }
        else {
            val = static_cast<T>(std::stoull(input, nullptr, 0));
        }

        if (!Memory::TryWriteValue(addr, val)) {
            if (err) *err = "Memory write failed (Access Denied)";
            return false;
        }
        return true;
    }
    catch (const std::invalid_argument&) {
        if (err) *err = "Invalid format: Not a number";
        return false;
    }
    catch (const std::out_of_range&) {
        if (err) *err = "Value out of range for type";
        return false;
    }
    catch (...) {
        if (err) *err = "Unknown parsing error";
        return false;
    }
}

bool SetFieldValue(const Engine::FieldInfo& field, const std::string& newValue, std::string* error = nullptr);
} // namespace Engine::Write

#endif // ENABLE_DUMPER
