#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <cmath>
#include <cstdint>
#include <cctype>
#include <limits>
#include <string>
#include <type_traits>

#include "types/dumper_types.h"
#include "types/memory_guard.h"

// Numeric / boolean / inline-struct field-write dispatch. Parses the
// user-typed string, validates the target pointer is writable, then commits
// with TryWriteValue. System.String writes live in FieldCatalog (managed
// string_new + pointer slot). Pointer, array, and list fields are rejected
// here: editing them would bypass GC bookkeeping or are navigation targets.
namespace Engine::Write
{
// Centralized "parse number, write number" for any arithmetic T. On
// success returns true and leaves *err untouched. On any failure returns
// false and -- if err != nullptr -- writes a human-readable reason into
// it. Rejects values that fit the parse API but not T (no silent truncate).
template<typename T>
bool ParseAndWrite(uintptr_t addr, const std::string& input, std::string* err) {
    try {
        // Trim ends for token checks; keep original for parse base.
        std::string trimmed = input;
        while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front()))) {
            trimmed.erase(trimmed.begin());
        }
        while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back()))) {
            trimmed.pop_back();
        }
        if (trimmed.empty()) {
            if (err) *err = "Invalid format: Not a number";
            return false;
        }

        T val;
        size_t idx = 0;
        if constexpr (std::is_floating_point_v<T>) {
            const double parsed = std::stod(trimmed, &idx);
            if (idx != trimmed.size()) {
                if (err) *err = "Invalid format: trailing characters";
                return false;
            }
            if (!std::isfinite(parsed)) {
                if (err) *err = "Value must be a finite number";
                return false;
            }
            if (parsed < static_cast<double>((std::numeric_limits<T>::lowest)())
                || parsed > static_cast<double>((std::numeric_limits<T>::max)())) {
                if (err) *err = "Value out of range for type";
                return false;
            }
            val = static_cast<T>(parsed);
        }
        else if constexpr (std::is_signed_v<T>) {
            const long long parsed = std::stoll(trimmed, &idx, 0);
            if (idx != trimmed.size()) {
                if (err) *err = "Invalid format: trailing characters";
                return false;
            }
            if (parsed < static_cast<long long>((std::numeric_limits<T>::min)())
                || parsed > static_cast<long long>((std::numeric_limits<T>::max)())) {
                if (err) *err = "Value out of range for type";
                return false;
            }
            val = static_cast<T>(parsed);
        }
        else {
            // Reject leading minus before stoull wrap (e.g. "-1" → max).
            if (trimmed.front() == '-') {
                if (err) *err = "Value out of range for type";
                return false;
            }
            const unsigned long long parsed = std::stoull(trimmed, &idx, 0);
            if (idx != trimmed.size()) {
                if (err) *err = "Invalid format: trailing characters";
                return false;
            }
            if (parsed > static_cast<unsigned long long>((std::numeric_limits<T>::max)())) {
                if (err) *err = "Value out of range for type";
                return false;
            }
            val = static_cast<T>(parsed);
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
