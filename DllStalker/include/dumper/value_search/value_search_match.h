#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <string>
#include <unordered_set>

#include "types/name_fuzzy.h"
#include "types/type_classifier.h"
#include "types/value_search_schema.h"
#include "types/value_search_types.h"
#include "types/dumper_types.h"

namespace Engine::Dumper
{
// Number chip: scalars + allowlisted Unity inlines (VEC2/3/4, QUAT, COLOR…).
inline bool IsValueSearchNumberCategory(Types::TypeCategory cat) {
    switch (cat) {
    case Types::TypeCategory::I1:
    case Types::TypeCategory::I2:
    case Types::TypeCategory::I4:
    case Types::TypeCategory::I8:
    case Types::TypeCategory::U1:
    case Types::TypeCategory::U2:
    case Types::TypeCategory::U4:
    case Types::TypeCategory::U8:
    case Types::TypeCategory::R4:
    case Types::TypeCategory::R8:
        return true;
    default:
        return Types::IsInlineValueStruct(cat);
    }
}

inline bool IsValueSearchCollectionCategory(Types::TypeCategory cat) {
    return cat == Types::TypeCategory::ARRAY || cat == Types::TypeCategory::LIST;
}

// Kind gate for live fields / schema. ARRAY/LIST when Deep.
inline bool ValueSearchFieldKindAllowed(bool isEnum,
                                        const std::string& typeName,
                                        const ValueSearchParams& params) {
    if (isEnum) {
        return params.chipEnum;
    }
    const auto cat = Types::GetCategory(typeName);
    if (cat == Types::TypeCategory::BOOLEAN) {
        return params.chipBool;
    }
    if (cat == Types::TypeCategory::STRING) {
        return params.chipString;
    }
    if (IsValueSearchNumberCategory(cat)) {
        return params.chipNumber;
    }
    if (cat == Types::TypeCategory::PTR) {
        return params.chipPtr;
    }
    if (cat == Types::TypeCategory::ARRAY || cat == Types::TypeCategory::LIST) {
        return params.chipDeep;
    }
    return false;
}

inline bool ValueSearchFieldKindAllowed(const FieldInfo& field, const ValueSearchParams& params) {
    return ValueSearchFieldKindAllowed(field.isEnum, field.type, params);
}

inline bool ValueSearchFieldKindAllowed(const ValueSearchFieldSchema& field,
                                        const ValueSearchParams& params) {
    return ValueSearchFieldKindAllowed(field.isEnum, field.type, params);
}

// Element / Deep-interior leaf gate — never ARRAY/LIST (no nested expand).
inline bool ValueSearchElementKindAllowed(bool isEnum,
                                          const std::string& typeName,
                                          const ValueSearchParams& params) {
    if (isEnum) {
        return params.chipEnum;
    }
    const auto cat = Types::GetCategory(typeName);
    if (cat == Types::TypeCategory::ARRAY || cat == Types::TypeCategory::LIST) {
        return false;
    }
    return ValueSearchFieldKindAllowed(false, typeName, params);
}

inline bool ValueSearchElementKindAllowed(const FieldInfo& field,
                                          const ValueSearchParams& params) {
    return ValueSearchElementKindAllowed(field.isEnum, field.type, params);
}

inline bool ValueSearchNamePasses(const std::string& fieldName,
                                  const std::string& nameNeedle,
                                  bool nameMatchStrict) {
    if (nameNeedle.empty()) {
        return true;
    }
    if (nameMatchStrict) {
        return Types::NameStrictMatch(fieldName, nameNeedle);
    }
    return Types::NameFuzzyMatch(fieldName, nameNeedle);
}

inline bool ValueSearchNamePasses(const std::string& fieldName,
                                  const ValueSearchParams& params) {
    return ValueSearchNamePasses(fieldName, params.nameNeedle, params.nameMatchStrict);
}

// Live hit "cheats[0].name" → schema pattern "cheats[].name" ([digits] → []).
inline std::string LiveFieldNameToSchemaPattern(const std::string& liveName) {
    std::string out;
    out.reserve(liveName.size());
    for (size_t i = 0; i < liveName.size();) {
        if (liveName[i] == '[') {
            size_t j = i + 1;
            while (j < liveName.size() && liveName[j] >= '0' && liveName[j] <= '9') {
                ++j;
            }
            if (j > i + 1 && j < liveName.size() && liveName[j] == ']') {
                out += "[]";
                i = j + 1;
                continue;
            }
        }
        out.push_back(liveName[i]);
        ++i;
    }
    return out;
}

inline bool SchemaNameAllowContains(const std::unordered_set<std::string>& allow,
                                    const std::string& liveOrSchemaName) {
    if (allow.find(liveOrSchemaName) != allow.end()) {
        return true;
    }
    const std::string pattern = LiveFieldNameToSchemaPattern(liveOrSchemaName);
    return pattern != liveOrSchemaName && allow.find(pattern) != allow.end();
}
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
