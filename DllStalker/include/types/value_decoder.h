#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <cstdint>
#include <string>
#include <string_view>

// GUI-friendly display formatting for managed field values. Hands off to
// type_classifier for the category branch, then runs a memory-safe decode
// per branch. Works against an instance address (`valueAddress`) -- not the
// instance itself -- so the same code path serves static fields, instance
// fields, and synthesized collection-element rows.
namespace Engine::Decode
{
// Reads a managed System.String at `address` (which must point at the
// pointer slot, not the string object itself) and returns a quoted UTF-8
// rendering. Returns "null", "\"<unreadable>\"", or "<?>" sentinels when
// the indirection chain breaks.
std::string DecodeManagedString(uintptr_t address);

// `objectPtr` is the managed System.String object (not a pointer slot).
std::string DecodeManagedStringFromObject(uintptr_t objectPtr);

// Decode a value passed in a register (object ptr for STRING, direct bits for scalars).
std::string DecodeRegisterArgument(const std::string& typeName, uintptr_t registerValue);

// Top-level dispatch: pick a category from `fieldType`, then decode the
// value at `valueAddress` accordingly. Returns "-" when `hasValue` is false
// or the address is null; "??" when the read itself fails. Callers (GUI,
// console dumper) treat these sentinels as non-clickable.
std::string DecodeFieldValue(const std::string& fieldType, uintptr_t valueAddress, bool hasValue);

// Fields-tab edit helpers. StripQuotes removes the outer pair from decoded
// display ("hello" -> hello). NormalizeStringFieldInput trims, treats
// null/empty as assign-null, and applies StripQuotes on the payload.
std::string StripQuotesForFieldEdit(std::string_view display);
std::string NormalizeStringFieldInput(std::string_view raw);
} // namespace Engine::Decode

#endif // ENABLE_DUMPER
