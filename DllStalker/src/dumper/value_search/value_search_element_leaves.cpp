#include "pch.h"

#ifdef ENABLE_DUMPER

#include "dumper/value_search/value_search_element_leaves.h"

#include "dumper/instances/collection_view.h"
#include "dumper/value_search/value_search_match.h"
#include "types/memory_guard.h"
#include "types/type_classifier.h"

#include <algorithm>

namespace Engine::Dumper
{
namespace
{
bool IsCollectionCategory(Types::TypeCategory cat) {
    return IsValueSearchCollectionCategory(cat);
}

// Instance fields only; statics are not element-interior layout.
void AppendInteriorMembersCapped(std::vector<FieldInfo>& members,
                                 std::vector<FieldInfo>& raw,
                                 size_t maxFields) {
    members.clear();
    members.reserve((std::min)(maxFields, raw.size()));
    for (auto& field : raw) {
        if (field.isStatic) {
            continue;
        }
        members.push_back(std::move(field));
        if (members.size() >= maxFields) {
            break;
        }
    }
}

void RenameInteriorLeaves(std::vector<FieldInfo>& leaves,
                          const std::string& containerName,
                          size_t elementIndex) {
    for (auto& leaf : leaves) {
        leaf.name = FormatCollectionElementInteriorName(containerName, elementIndex, leaf.name);
    }
}
} // namespace

std::string FormatCollectionElementInteriorName(const std::string& containerName,
                                                size_t index,
                                                const std::string& memberName) {
    return FormatCollectionElementName(containerName, index) + "." + memberName;
}

std::string FormatCollectionElementInteriorSchemaName(const std::string& containerName,
                                                      const std::string& memberName) {
    return containerName + "[]." + memberName;
}

bool ParseCollectionElementInteriorName(const std::string& fieldName,
                                        std::string& outContainer,
                                        size_t& outIndex,
                                        std::string& outMember) {
    // Require "container[N].member" — exactly one '.' after ']', member non-empty,
    // no further '.' or '[' (no depth-2 / nested brackets).
    const size_t close = fieldName.rfind(']');
    if (close == std::string::npos || close + 1 >= fieldName.size() || fieldName[close + 1] != '.') {
        return false;
    }
    if (close + 2 >= fieldName.size()) {
        return false;
    }
    const std::string member = fieldName.substr(close + 2);
    if (member.empty() || member.find('.') != std::string::npos
        || member.find('[') != std::string::npos) {
        return false;
    }
    const std::string slotPart = fieldName.substr(0, close + 1);
    std::string container;
    size_t index = 0;
    if (!ParseCollectionElementName(slotPart, container, index)) {
        return false;
    }
    outContainer = std::move(container);
    outIndex = index;
    outMember = member;
    return true;
}

bool IsCollectionElementInteriorSchemaName(const std::string& fieldName) {
    const size_t pos = fieldName.find("[].");
    if (pos == std::string::npos || pos == 0) {
        return false;
    }
    if (pos + 3 >= fieldName.size()) {
        return false;
    }
    // Exactly one "[]" — no extra brackets; member has no '.' (depth-1).
    if (fieldName.find('[', pos + 2) != std::string::npos) {
        return false;
    }
    const std::string member = fieldName.substr(pos + 3);
    return !member.empty() && member.find('.') == std::string::npos;
}

bool CollectionInteriorSchemaBelongsTo(const std::string& schemaName,
                                       const std::string& containerName) {
    if (!IsCollectionElementInteriorSchemaName(schemaName)) {
        return false;
    }
    const std::string prefix = containerName + "[].";
    return schemaName.size() > prefix.size()
        && schemaName.compare(0, prefix.size(), prefix) == 0;
}

void AppendCollectionElementInteriorSchemaLeaves(UnityDumper& dumper,
                                                 void* ownerKlass,
                                                 const ValueSearchFieldSchema& parent,
                                                 std::vector<ValueSearchFieldSchema>& out) {
    if (!ownerKlass || parent.name.empty()) {
        return;
    }
    if (!IsCollectionCategory(Types::GetCategory(parent.type))) {
        return;
    }
    void* elementKlass = dumper.TryResolveCollectionElementKlass(ownerKlass, parent.name.c_str());
    if (!elementKlass) {
        return;
    }

    std::vector<FieldInfo> raw;
    bool enumerationComplete = true;
    try {
        raw = dumper.GetRawFields(elementKlass, nullptr, &enumerationComplete,
                                  /*metadataOnly=*/true);
    }
    catch (...) {
        return;
    }
    if (!enumerationComplete) {
        return;
    }

    // Copy parent name before push_back (parent may alias out.back()).
    const std::string containerName = parent.name;
    size_t emitted = 0;
    for (const auto& field : raw) {
        if (field.isStatic) {
            continue;
        }
        ValueSearchFieldSchema leaf{};
        leaf.name = FormatCollectionElementInteriorSchemaName(containerName, field.name);
        leaf.type = field.type;
        leaf.offset = field.offset;
        leaf.isStatic = false;
        leaf.isEnum = field.isEnum;
        out.push_back(std::move(leaf));
        if (++emitted >= kValueSearchMaxElementInteriorFields) {
            break;
        }
    }
}

std::vector<FieldInfo> ExpandElementInteriorLeaves(UnityDumper& dumper,
                                                   void* ownerKlass,
                                                   const FieldInfo& container,
                                                   size_t elementIndex,
                                                   const FieldInfo& elementSlot,
                                                   size_t maxFields) {
    std::vector<FieldInfo> leaves;
    if (maxFields == 0 || !elementSlot.hasValue || elementSlot.valueAddress == 0) {
        return leaves;
    }

    // Prefer live array element klass (CollectionView path); fall back to
    // owner field metadata when the live resolve fails.
    void* elementKlass = dumper.TryResolveCollectionElementKlass(container);
    if (!elementKlass && ownerKlass) {
        elementKlass = dumper.TryResolveCollectionElementKlass(ownerKlass, container.name.c_str());
    }
    if (!elementKlass) {
        return leaves;
    }

    std::vector<FieldInfo> raw;
    try {
        if (dumper.IsValueTypeKlass(elementKlass)) {
            // Inline valuetype at slot — do not route layout through GetCategory
            // (dotted game struct names often classify as PTR).
            raw = dumper.GetRawFields(elementKlass,
                                      reinterpret_cast<void*>(elementSlot.valueAddress));
        }
        else {
            uintptr_t managedPtr = 0;
            if (!Memory::TryReadValue<uintptr_t>(elementSlot.valueAddress, managedPtr)
                || managedPtr == 0) {
                return leaves;
            }
            void* instance = reinterpret_cast<void*>(managedPtr);
            // Ref: klass from the live instance (may be a subclass).
            void* instanceKlass = dumper.KlassFromInstance(instance);
            if (!instanceKlass) {
                instanceKlass = elementKlass;
            }
            raw = dumper.GetRawFields(instanceKlass, instance);
        }
    }
    catch (...) {
        return leaves;
    }

    AppendInteriorMembersCapped(leaves, raw, maxFields);
    RenameInteriorLeaves(leaves, container.name, elementIndex);
    return leaves;
}

const FieldInfo* ResolveCollectionElementInterior(UnityDumper& dumper,
                                                  void* ownerKlass,
                                                  const std::vector<FieldInfo>& rawFields,
                                                  const std::string& fieldName,
                                                  FieldInfo& storage) {
    std::string containerName;
    size_t index = 0;
    std::string memberName;
    if (!ParseCollectionElementInteriorName(fieldName, containerName, index, memberName)) {
        return nullptr;
    }
    (void)memberName; // non-empty validated by parse; match uses full fieldName

    const FieldInfo* container = nullptr;
    for (const auto& field : rawFields) {
        if (field.name == containerName) {
            container = &field;
            break;
        }
    }
    if (!container || !IsCollectionCategory(Types::GetCategory(container->type))) {
        return nullptr;
    }

    std::vector<FieldInfo> elements;
    try {
        elements = dumper.GetCollectionView(*container, kValueSearchMaxCollectionElements);
    }
    catch (...) {
        return nullptr;
    }
    if (index >= elements.size()) {
        return nullptr;
    }

    std::vector<FieldInfo> leaves = ExpandElementInteriorLeaves(
        dumper, ownerKlass, *container, index, elements[index],
        kValueSearchMaxElementInteriorFields);
    for (auto& leaf : leaves) {
        if (leaf.name == fieldName) {
            storage = std::move(leaf);
            return &storage;
        }
    }
    return nullptr;
}

const FieldInfo* ResolveCollectionElementSlot(UnityDumper& dumper,
                                              const std::vector<FieldInfo>& rawFields,
                                              const std::string& fieldName,
                                              FieldInfo& storage) {
    std::string containerName;
    size_t index = 0;
    if (!ParseCollectionElementName(fieldName, containerName, index)) {
        return nullptr;
    }
    const FieldInfo* container = nullptr;
    for (const auto& field : rawFields) {
        if (field.name == containerName) {
            container = &field;
            break;
        }
    }
    if (!container || !IsValueSearchCollectionCategory(Types::GetCategory(container->type))) {
        return nullptr;
    }
    std::vector<FieldInfo> elements;
    try {
        elements = dumper.GetCollectionView(*container, kValueSearchMaxCollectionElements);
    }
    catch (...) {
        return nullptr;
    }
    if (index >= elements.size()) {
        return nullptr;
    }
    storage = elements[index];
    storage.name = FormatCollectionElementName(containerName, index);
    return &storage;
}
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
