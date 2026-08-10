#include "pch.h"

#ifdef ENABLE_DUMPER

#include "dumper/value_search/value_search_nested_leaves.h"

#include "types/value_decoder.h"

namespace Engine::Dumper
{
namespace
{
constexpr NestedLeafLayout kVec2Leaves[] = {
    {"x", "System.Single", 0, sizeof(float)},
    {"y", "System.Single", 1, sizeof(float)},
};
constexpr NestedLeafLayout kVec3Leaves[] = {
    {"x", "System.Single", 0, sizeof(float)},
    {"y", "System.Single", 1, sizeof(float)},
    {"z", "System.Single", 2, sizeof(float)},
};
constexpr NestedLeafLayout kVec4Leaves[] = {
    {"x", "System.Single", 0, sizeof(float)},
    {"y", "System.Single", 1, sizeof(float)},
    {"z", "System.Single", 2, sizeof(float)},
    {"w", "System.Single", 3, sizeof(float)},
};
constexpr NestedLeafLayout kColorLeaves[] = {
    {"r", "System.Single", 0, sizeof(float)},
    {"g", "System.Single", 1, sizeof(float)},
    {"b", "System.Single", 2, sizeof(float)},
    {"a", "System.Single", 3, sizeof(float)},
};
constexpr NestedLeafLayout kColor32Leaves[] = {
    {"r", "System.Byte", 0, sizeof(uint8_t)},
    {"g", "System.Byte", 1, sizeof(uint8_t)},
    {"b", "System.Byte", 2, sizeof(uint8_t)},
    {"a", "System.Byte", 3, sizeof(uint8_t)},
};
constexpr NestedLeafLayout kRectLeaves[] = {
    {"x", "System.Single", 0, sizeof(float)},
    {"y", "System.Single", 1, sizeof(float)},
    {"width", "System.Single", 2, sizeof(float)},
    {"height", "System.Single", 3, sizeof(float)},
};

FieldInfo MakeNestedLeafField(const FieldInfo& parent, const NestedLeafLayout& layout) {
    FieldInfo leaf{};
    leaf.name = parent.name;
    leaf.name.push_back('.');
    leaf.name.append(layout.name);
    leaf.type = layout.typeName;
    const size_t delta = layout.index * layout.stride;
    leaf.offset = parent.offset + delta;
    leaf.isStatic = parent.isStatic;
    leaf.isEnum = false;
    if (parent.valueAddress != 0) {
        leaf.valueAddress = parent.valueAddress + delta;
        leaf.hasValue = parent.hasValue;
        leaf.valueDisplay =
            Decode::DecodeFieldValue(leaf.type, leaf.valueAddress, leaf.hasValue);
    }
    return leaf;
}

bool TryBuildNestedLeaf(const FieldInfo& parent,
                        const std::string& childName,
                        FieldInfo& out) {
    const auto cat = Types::GetCategory(parent.type);
    size_t count = 0;
    const NestedLeafLayout* leaves = GetAllowlistedNestedLeaves(cat, count);
    if (!leaves || count == 0) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        if (childName != leaves[i].name) {
            continue;
        }
        out = MakeNestedLeafField(parent, leaves[i]);
        return true;
    }
    return false;
}
} // namespace

const NestedLeafLayout* GetAllowlistedNestedLeaves(Types::TypeCategory cat,
                                                   size_t& outCount) {
    using Cat = Types::TypeCategory;
    switch (cat) {
    case Cat::VEC2:
        outCount = sizeof(kVec2Leaves) / sizeof(kVec2Leaves[0]);
        return kVec2Leaves;
    case Cat::VEC3:
        outCount = sizeof(kVec3Leaves) / sizeof(kVec3Leaves[0]);
        return kVec3Leaves;
    case Cat::VEC4:
    case Cat::QUAT:
        outCount = sizeof(kVec4Leaves) / sizeof(kVec4Leaves[0]);
        return kVec4Leaves;
    case Cat::COLOR:
        outCount = sizeof(kColorLeaves) / sizeof(kColorLeaves[0]);
        return kColorLeaves;
    case Cat::COLOR32:
        outCount = sizeof(kColor32Leaves) / sizeof(kColor32Leaves[0]);
        return kColor32Leaves;
    case Cat::RECT:
        outCount = sizeof(kRectLeaves) / sizeof(kRectLeaves[0]);
        return kRectLeaves;
    default:
        outCount = 0;
        return nullptr;
    }
}

void AppendAllowlistedNestedSchemaLeaves(const ValueSearchFieldSchema& parent,
                                         std::vector<ValueSearchFieldSchema>& out) {
    const auto cat = Types::GetCategory(parent.type);
    size_t count = 0;
    const NestedLeafLayout* leaves = GetAllowlistedNestedLeaves(cat, count);
    if (!leaves || count == 0) {
        return;
    }
    // Copy parent fields before push_back — `parent` may alias an element of
    // `out` (schema Build), and reallocation would invalidate the reference.
    const std::string parentName = parent.name;
    const size_t parentOffset = parent.offset;
    const bool parentStatic = parent.isStatic;
    for (size_t i = 0; i < count; ++i) {
        ValueSearchFieldSchema leaf{};
        leaf.name = parentName;
        leaf.name.push_back('.');
        leaf.name.append(leaves[i].name);
        leaf.type = leaves[i].typeName;
        leaf.offset = parentOffset + leaves[i].index * leaves[i].stride;
        leaf.isStatic = parentStatic;
        leaf.isEnum = false;
        out.push_back(std::move(leaf));
    }
}

void ExpandAllowlistedNestedLeaves(std::vector<FieldInfo>& fields) {
    const size_t parentCount = fields.size();
    for (size_t i = 0; i < parentCount; ++i) {
        // Copy parent — appends may reallocate and invalidate fields[i].
        const FieldInfo parent = fields[i];
        const auto cat = Types::GetCategory(parent.type);
        size_t count = 0;
        const NestedLeafLayout* leaves = GetAllowlistedNestedLeaves(cat, count);
        if (!leaves || count == 0) {
            continue;
        }
        for (size_t c = 0; c < count; ++c) {
            fields.push_back(MakeNestedLeafField(parent, leaves[c]));
        }
    }
}

const FieldInfo* ResolveFieldOrNestedLeaf(const std::vector<FieldInfo>& rawFields,
                                          const std::string& fieldName,
                                          FieldInfo& storage) {
    for (const auto& field : rawFields) {
        if (field.name == fieldName) {
            return &field;
        }
    }

    // One-level Parent.Child only (no deep recurse).
    const size_t dot = fieldName.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= fieldName.size()) {
        return nullptr;
    }
    if (fieldName.find('.', dot + 1) != std::string::npos) {
        return nullptr;
    }
    const std::string parentName = fieldName.substr(0, dot);
    const std::string childName = fieldName.substr(dot + 1);
    for (const auto& parent : rawFields) {
        if (parent.name != parentName) {
            continue;
        }
        if (TryBuildNestedLeaf(parent, childName, storage)) {
            return &storage;
        }
        return nullptr;
    }
    return nullptr;
}
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
