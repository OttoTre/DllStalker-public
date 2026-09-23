#include "pch.h"

#ifdef ENABLE_DUMPER

#include "dumper/instances/collection_view.h"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <string>
#include <string_view>

#include "dumper/catalog/object_identity.h"
#include "types/memory_guard.h"
#include "types/type_classifier.h"
#include "types/unity_array_layout.h"
#include "types/value_decoder.h"

namespace Engine::Dumper
{
namespace
{
static_assert(sizeof(void*) == 8, "GetCollectionView assumes the x64 Unity array header layout");

constexpr size_t kMaxFindObjectsResultLength = 1'000'000;

// Drops trailing "[]" pairs from a type name. "Stat[]" -> "Stat",
// "int[][]" -> "int[]" (one rank at a time, matching the array's nested
// layout). Leaves non-array names unchanged.
std::string StripArraySuffix(std::string_view typeName) {
    if (typeName.size() >= 2
        && typeName[typeName.size() - 2] == '['
        && typeName[typeName.size() - 1] == ']')
    {
        return std::string(typeName.substr(0, typeName.size() - 2));
    }
    return std::string(typeName);
}

// Best-effort extraction of T from "...List`1<T>" / "...List<T>". Walks
// the string finding the FIRST '<' (so the outermost generic argument)
// and the matching '>' via bracket counting so nested generics like
// "List<Dictionary<string,int>>" extract "Dictionary<string,int>"
// correctly. Returns "object" when no balanced pair is found.
std::string ExtractListElementName(std::string_view typeName) {
    auto open = typeName.find('<');
    if (open == std::string_view::npos) return "object";

    int depth = 0;
    for (size_t i = open; i < typeName.size(); ++i) {
        if (typeName[i] == '<') ++depth;
        else if (typeName[i] == '>') {
            if (--depth == 0) {
                return std::string(typeName.substr(open + 1, i - open - 1));
            }
        }
    }
    return "object";
}
} // namespace

std::string FormatCollectionElementName(const std::string& containerName, size_t index) {
    char indexBuf[32] = {};
    snprintf(indexBuf, sizeof(indexBuf), "[%zu]", index);
    return containerName + indexBuf;
}

bool ParseCollectionElementName(const std::string& fieldName,
                                std::string& outContainer,
                                size_t& outIndex) {
    // Require trailing "[N]" with N decimal; container name non-empty.
    if (fieldName.size() < 4 || fieldName.back() != ']') {
        return false;
    }
    const size_t open = fieldName.rfind('[');
    if (open == std::string::npos || open == 0 || open + 1 >= fieldName.size() - 1) {
        return false;
    }
    // No extra '[' between open and end (reject "a[1][2]").
    if (fieldName.find('[', open + 1) != std::string::npos) {
        return false;
    }
    const char* begin = fieldName.data() + open + 1;
    const char* end = fieldName.data() + fieldName.size() - 1;
    size_t index = 0;
    auto [ptr, ec] = std::from_chars(begin, end, index);
    if (ec != std::errc{} || ptr != end) {
        return false;
    }
    outContainer.assign(fieldName.data(), open);
    outIndex = index;
    return true;
}

bool ParseSynthesizedFieldsElementIndex(const std::string& fieldName, size_t& outIndex) {
    // Fields collection rows: "[3]". open==0 is valid (unlike ParseCollectionElementName).
    if (fieldName.size() >= 3 && fieldName.front() == '[' && fieldName.back() == ']') {
        const char* begin = fieldName.data() + 1;
        const char* end = fieldName.data() + fieldName.size() - 1;
        if (begin < end && fieldName.find('[', 1) == std::string::npos) {
            size_t index = 0;
            auto [ptr, ec] = std::from_chars(begin, end, index);
            if (ec == std::errc{} && ptr == end) {
                outIndex = index;
                return true;
            }
        }
    }
    return false;
}

bool IsSynthesizedCollectionElementName(const std::string& fieldName) {
    size_t index = 0;
    if (ParseSynthesizedFieldsElementIndex(fieldName, index)) {
        return true;
    }
    // Search/Drill: "items[3]".
    std::string container;
    return ParseCollectionElementName(fieldName, container, index);
}

CollectionView::CollectionView(UnityResolver& resolver, const ObjectIdentity& identity)
    : m_resolver(resolver)
    , m_identity(identity)
{
}

void* CollectionView::ElementKlassFromArrayBase(uintptr_t arrayBase) const {
    if (arrayBase == 0 || !m_resolver.module.exports.fnClassGetElementClass) {
        return nullptr;
    }
    void* arrayKlass = m_identity.KlassFromInstance(reinterpret_cast<void*>(arrayBase));
    if (!arrayKlass) {
        return nullptr;
    }
    return m_resolver.module.exports.fnClassGetElementClass(arrayKlass);
}

void* CollectionView::ElementKlassFromFieldType(void* fieldType) const {
    if (!fieldType || !m_resolver.module.exports.fnClassFromType
        || !m_resolver.module.exports.fnTypeGetName
        || !m_resolver.module.exports.fnClassGetElementClass) {
        return nullptr;
    }
    void* fieldKlass = m_resolver.module.exports.fnClassFromType(fieldType);
    if (!fieldKlass) {
        return nullptr;
    }
    const char* rawTypeName = m_resolver.module.exports.fnTypeGetName(fieldType);
    if (!rawTypeName || rawTypeName[0] == '\0') {
        return nullptr;
    }
    const auto cat = Types::GetCategory(rawTypeName);
    if (cat == Types::TypeCategory::ARRAY) {
        return m_resolver.module.exports.fnClassGetElementClass(fieldKlass);
    }
    if (cat == Types::TypeCategory::LIST) {
        // Inflated List`1<T>: _items is T[] — same layout CollectionView uses live.
        if (!m_resolver.module.exports.fnGetFieldFromName
            || !m_resolver.module.exports.fnFieldGetType) {
            return nullptr;
        }
        void* itemsField = m_resolver.module.exports.fnGetFieldFromName(fieldKlass, "_items");
        if (!itemsField) {
            return nullptr;
        }
        void* itemsType = m_resolver.module.exports.fnFieldGetType(itemsField);
        if (!itemsType) {
            return nullptr;
        }
        void* itemsKlass = m_resolver.module.exports.fnClassFromType(itemsType);
        if (!itemsKlass) {
            return nullptr;
        }
        return m_resolver.module.exports.fnClassGetElementClass(itemsKlass);
    }
    return nullptr;
}

void* CollectionView::TryResolveElementKlass(void* ownerKlass, const char* fieldName) const {
    if (!ownerKlass || !fieldName || !fieldName[0]
        || !m_resolver.module.exports.fnGetFieldFromName
        || !m_resolver.module.exports.fnFieldGetType) {
        return nullptr;
    }
    void* field = m_resolver.module.exports.fnGetFieldFromName(ownerKlass, fieldName);
    if (!field) {
        return nullptr;
    }
    void* fieldType = m_resolver.module.exports.fnFieldGetType(field);
    return ElementKlassFromFieldType(fieldType);
}

void* CollectionView::TryResolveElementKlass(const FieldInfo& field) const {
    if (!field.hasValue || field.valueAddress == 0) {
        return nullptr;
    }
    const auto category = Types::GetCategory(field.type);
    if (category != Types::TypeCategory::ARRAY && category != Types::TypeCategory::LIST) {
        return nullptr;
    }

    uintptr_t arrayBase = 0;
    if (category == Types::TypeCategory::ARRAY) {
        if (!Memory::TryReadValue<uintptr_t>(field.valueAddress, arrayBase) || arrayBase == 0) {
            return nullptr;
        }
        return ElementKlassFromArrayBase(arrayBase);
    }

    // LIST: wrapper → _items array (same as GetCollectionView).
    uintptr_t listObj = 0;
    if (!Memory::TryReadValue<uintptr_t>(field.valueAddress, listObj) || listObj == 0) {
        return nullptr;
    }
    void* listKlass = m_identity.KlassFromInstance(reinterpret_cast<void*>(listObj));
    if (!listKlass || !m_resolver.module.exports.fnGetFieldFromName
        || !m_resolver.module.exports.fnGetFieldOffset) {
        return nullptr;
    }
    void* itemsField = m_resolver.module.exports.fnGetFieldFromName(listKlass, "_items");
    if (!itemsField) {
        return nullptr;
    }
    const size_t itemsOffset = m_resolver.module.exports.fnGetFieldOffset(itemsField);
    if (!Memory::TryReadValue<uintptr_t>(listObj + itemsOffset, arrayBase) || arrayBase == 0) {
        return nullptr;
    }
    return ElementKlassFromArrayBase(arrayBase);
}

bool CollectionView::IsAddressInLiveElementBuffer(const FieldInfo& collectionField,
                                                 uintptr_t elementAddress,
                                                 std::string* error) const {
    auto setError = [&](const char* msg) {
        if (error) {
            *error = msg;
        }
    };

    if (elementAddress == 0) {
        setError("Collection element address is null");
        return false;
    }
    if (!collectionField.hasValue || collectionField.valueAddress == 0) {
        setError("Collection field has no readable address");
        return false;
    }

    const auto category = Types::GetCategory(collectionField.type);
    if (category != Types::TypeCategory::ARRAY && category != Types::TypeCategory::LIST) {
        setError("Owning field is not an Array/List");
        return false;
    }

    uintptr_t arrayBase = 0;
    size_t length = 0;

    if (category == Types::TypeCategory::ARRAY) {
        if (!Memory::TryReadValue<uintptr_t>(collectionField.valueAddress, arrayBase) || arrayBase == 0) {
            setError("Array is null or unreadable");
            return false;
        }
        if (!Memory::TryReadValue(arrayBase + Engine::UnityArrayLayout::LengthOffset, length)) {
            setError("Array length unreadable");
            return false;
        }
    }
    else {
        uintptr_t listObj = 0;
        if (!Memory::TryReadValue<uintptr_t>(collectionField.valueAddress, listObj) || listObj == 0) {
            setError("List is null or unreadable");
            return false;
        }
        void* listKlass = m_identity.KlassFromInstance(reinterpret_cast<void*>(listObj));
        if (!listKlass || !m_resolver.module.exports.fnGetFieldFromName
            || !m_resolver.module.exports.fnGetFieldOffset) {
            setError("List klass / fields unresolved");
            return false;
        }

        void* itemsField = m_resolver.module.exports.fnGetFieldFromName(listKlass, "_items");
        void* sizeField  = m_resolver.module.exports.fnGetFieldFromName(listKlass, "_size");
        if (!itemsField || !sizeField) {
            setError("List _items/_size fields missing");
            return false;
        }

        const size_t itemsOffset = m_resolver.module.exports.fnGetFieldOffset(itemsField);
        const size_t sizeOffset  = m_resolver.module.exports.fnGetFieldOffset(sizeField);

        if (!Memory::TryReadValue<uintptr_t>(listObj + itemsOffset, arrayBase) || arrayBase == 0) {
            setError("List _items is null or unreadable");
            return false;
        }

        int32_t logicalSize = 0;
        if (!Memory::TryReadValue<int32_t>(listObj + sizeOffset, logicalSize) || logicalSize < 0) {
            setError("List _size unreadable");
            return false;
        }

        size_t allocated = 0;
        if (!Memory::TryReadValue(arrayBase + Engine::UnityArrayLayout::LengthOffset, allocated)) {
            setError("List backing array length unreadable");
            return false;
        }
        length = (std::min)(static_cast<size_t>(logicalSize), allocated);
    }

    if (length == 0) {
        setError("Collection is empty (shrunk or cleared)");
        return false;
    }
    if (length > kMaxFindObjectsResultLength) {
        setError("Collection length out of safe bounds");
        return false;
    }

    constexpr size_t kReferenceSlotSize = sizeof(void*);
    size_t elementSize = kReferenceSlotSize;

    void* elementKlass = nullptr;
    if (m_resolver.module.exports.fnClassGetElementClass) {
        void* arrayKlass = m_identity.KlassFromInstance(reinterpret_cast<void*>(arrayBase));
        if (arrayKlass) {
            elementKlass = m_resolver.module.exports.fnClassGetElementClass(arrayKlass);
        }
    }

    if (elementKlass && m_resolver.module.exports.fnClassIsValueType
        && m_resolver.module.exports.fnClassIsValueType(elementKlass)) {
        if (m_resolver.module.exports.fnClassValueSize) {
            uint32_t align = 0;
            const int32_t vsz = m_resolver.module.exports.fnClassValueSize(elementKlass, &align);
            if (vsz > 0) {
                elementSize = static_cast<size_t>(vsz);
            }
        }
    }

    const uintptr_t elementsBase = arrayBase + Engine::UnityArrayLayout::ElementsOffset;
    const uintptr_t elementsEnd  = elementsBase + static_cast<uintptr_t>(length) * elementSize;
    if (elementAddress < elementsBase || elementAddress >= elementsEnd) {
        setError("Element address out of live collection range (collection may have shrunk or moved)");
        return false;
    }
    if (((elementAddress - elementsBase) % elementSize) != 0) {
        setError("Element address is not aligned to a collection slot");
        return false;
    }
    return true;
}

bool CollectionView::TryWriteListLogicalSize(const FieldInfo& collectionField,
                                             std::string* error) const {
    auto setError = [&](const char* msg) {
        if (error) {
            *error = msg;
        }
    };

    if (!collectionField.hasValue || collectionField.valueAddress == 0
        || Types::GetCategory(collectionField.type) != Types::TypeCategory::LIST) {
        setError("collection unreadable");
        return false;
    }

    uintptr_t listObj = 0;
    if (!Memory::TryReadValue<uintptr_t>(collectionField.valueAddress, listObj) || listObj == 0) {
        setError("collection unreadable");
        return false;
    }

    void* listKlass = m_identity.KlassFromInstance(reinterpret_cast<void*>(listObj));
    if (!listKlass || !m_resolver.module.exports.fnGetFieldFromName
        || !m_resolver.module.exports.fnGetFieldOffset) {
        setError("collection unreadable");
        return false;
    }

    void* itemsField = m_resolver.module.exports.fnGetFieldFromName(listKlass, "_items");
    void* sizeField  = m_resolver.module.exports.fnGetFieldFromName(listKlass, "_size");
    if (!itemsField || !sizeField) {
        setError("collection unreadable");
        return false;
    }

    const size_t itemsOffset = m_resolver.module.exports.fnGetFieldOffset(itemsField);
    const size_t sizeOffset  = m_resolver.module.exports.fnGetFieldOffset(sizeField);

    uintptr_t arrayBase = 0;
    if (!Memory::TryReadValue<uintptr_t>(listObj + itemsOffset, arrayBase) || arrayBase == 0) {
        setError("collection unreadable");
        return false;
    }

    int32_t logicalSize = 0;
    if (!Memory::TryReadValue<int32_t>(listObj + sizeOffset, logicalSize) || logicalSize < 0) {
        setError("collection unreadable");
        return false;
    }

    size_t allocated = 0;
    if (!Memory::TryReadValue(arrayBase + Engine::UnityArrayLayout::LengthOffset, allocated)) {
        setError("collection unreadable");
        return false;
    }

    const size_t currentSize = static_cast<size_t>(logicalSize);
    if (allocated == 0 || currentSize >= allocated) {
        setError("no spare capacity");
        return false;
    }
    if (currentSize >= kMaxFindObjectsResultLength) {
        setError("too many elements");
        return false;
    }

    const int32_t newSize = logicalSize + 1;
    if (!Memory::TryWriteValue<int32_t>(listObj + sizeOffset, newSize)) {
        setError("write failed");
        return false;
    }
    return true;
}

std::vector<FieldInfo> CollectionView::GetCollectionView(const FieldInfo& field,
                                                         size_t maxElements,
                                                         bool* outEmptyButReadable) const {
    std::vector<FieldInfo> view;
    if (outEmptyButReadable) {
        *outEmptyButReadable = false;
    }

    if (!field.hasValue || field.valueAddress == 0) return view;

    const auto category = Types::GetCategory(field.type);
    if (category != Types::TypeCategory::ARRAY && category != Types::TypeCategory::LIST) {
        return view;
    }

    // ---- Step 1: resolve (arrayBase, length, elementTypeName) ----
    uintptr_t arrayBase = 0;
    size_t    length    = 0;
    std::string elementTypeName;

    if (category == Types::TypeCategory::ARRAY) {
        if (!Memory::TryReadValue<uintptr_t>(field.valueAddress, arrayBase) || arrayBase == 0) {
            return view;
        }
        if (!Memory::TryReadValue(arrayBase + Engine::UnityArrayLayout::LengthOffset, length)) {
            return view;
        }
        elementTypeName = StripArraySuffix(field.type);
    }
    else {
        // LIST path: read the wrapper, then look up "_items" + "_size" by
        // name on the wrapper's klass (IL2CPP + Mono). Value Search element
        // match reuses this view — do not fork List layout logic in the scanner.
        uintptr_t listObj = 0;
        if (!Memory::TryReadValue<uintptr_t>(field.valueAddress, listObj) || listObj == 0) {
            return view;
        }
        void* listKlass = m_identity.KlassFromInstance(reinterpret_cast<void*>(listObj));
        if (!listKlass || !m_resolver.module.exports.fnGetFieldFromName || !m_resolver.module.exports.fnGetFieldOffset) {
            return view;
        }

        void* itemsField = m_resolver.module.exports.fnGetFieldFromName(listKlass, "_items");
        void* sizeField  = m_resolver.module.exports.fnGetFieldFromName(listKlass, "_size");
        if (!itemsField || !sizeField) {
            return view;
        }

        const size_t itemsOffset = m_resolver.module.exports.fnGetFieldOffset(itemsField);
        const size_t sizeOffset  = m_resolver.module.exports.fnGetFieldOffset(sizeField);

        if (!Memory::TryReadValue<uintptr_t>(listObj + itemsOffset, arrayBase) || arrayBase == 0) {
            return view;
        }

        int32_t logicalSize = 0;
        if (!Memory::TryReadValue<int32_t>(listObj + sizeOffset, logicalSize) || logicalSize < 0) {
            return view;
        }

        // Cross-check against the underlying array's allocated capacity so
        // a corrupted _size can't make us walk past the buffer.
        size_t allocated = 0;
        if (!Memory::TryReadValue(arrayBase + Engine::UnityArrayLayout::LengthOffset, allocated)) {
            return view;
        }
        length = (std::min)(static_cast<size_t>(logicalSize), allocated);
        elementTypeName = ExtractListElementName(field.type);
    }

    if (length > kMaxFindObjectsResultLength) {
        return view;
    }
    // Readable header + logical length 0: empty-success (no dummy [i] rows).
    if (length == 0) {
        if (outEmptyButReadable) {
            *outEmptyButReadable = true;
        }
        return view;
    }
    // Search/Drill: stop synthesize+decode at the caller bound. Inspector
    // passes 0 (uncapped aside from kMaxFindObjectsResultLength above).
    if (maxElements != 0 && length > maxElements) {
        length = maxElements;
    }

    // ---- Step 2: compute element stride ----
    constexpr size_t kReferenceSlotSize = sizeof(void*);
    size_t elementSize = kReferenceSlotSize;

    void* elementKlass = nullptr;
    if (m_resolver.module.exports.fnClassGetElementClass) {
        void* arrayKlass = m_identity.KlassFromInstance(reinterpret_cast<void*>(arrayBase));
        if (arrayKlass) {
            elementKlass = m_resolver.module.exports.fnClassGetElementClass(arrayKlass);
        }
    }

    if (elementKlass && m_resolver.module.exports.fnClassIsValueType && m_resolver.module.exports.fnClassIsValueType(elementKlass)) {
        if (m_resolver.module.exports.fnClassValueSize) {
            uint32_t align = 0;
            int32_t  vsz   = m_resolver.module.exports.fnClassValueSize(elementKlass, &align);
            if (vsz > 0) {
                elementSize = static_cast<size_t>(vsz);
            }
        }
        // If we know it's a value type but couldn't get the size, surface a
        // single sentinel row instead of guessing an offset. That's better
        // than silently producing misaligned reads.
        if (elementSize == kReferenceSlotSize && !m_resolver.module.exports.fnClassValueSize) {
            FieldInfo sentinel{};
            sentinel.name         = "<value-type elements>";
            sentinel.type         = elementTypeName;
            sentinel.valueDisplay = "engine missing class_value_size";
            view.push_back(std::move(sentinel));
            return view;
        }
    }

    // Enum element enrichment (same exports as FieldCatalog field rows):
    // isEnum + enumKlass + underlying for decode / Enum chip match.
    bool isEnum = false;
    void* enumKlass = nullptr;
    std::string underlyingType;
    if (elementKlass
        && m_resolver.module.exports.fnClassIsEnum
        && m_resolver.module.exports.fnClassIsEnum(elementKlass)) {
        isEnum = true;
        enumKlass = elementKlass;
        if (m_resolver.module.exports.fnClassEnumBasetype) {
            if (void* baseType = m_resolver.module.exports.fnClassEnumBasetype(elementKlass)) {
                if (m_resolver.module.exports.fnTypeGetName) {
                    if (const char* baseName = m_resolver.module.exports.fnTypeGetName(baseType)) {
                        if (baseName[0] != '\0') {
                            underlyingType = baseName;
                        }
                    }
                }
            }
        }
    }
    const std::string decodeType = isEnum && !underlyingType.empty()
                                       ? underlyingType
                                       : elementTypeName;

    const uintptr_t elementsBase = arrayBase + Engine::UnityArrayLayout::ElementsOffset;
    if (!Memory::IsReadablePointer(reinterpret_cast<void*>(elementsBase), elementSize * length)) {
        return view;
    }

    // ---- Step 3: synthesize one FieldInfo per element ----
    view.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        const uintptr_t slotAddress = elementsBase + i * elementSize;

        char nameBuf[32] = {};
        snprintf(nameBuf, sizeof(nameBuf), "[%zu]", i);

        FieldInfo row{};
        row.name           = nameBuf;
        row.type           = elementTypeName;
        row.offset         = i * elementSize;
        row.staticValue    = 0;
        row.valueAddress   = slotAddress;
        row.hasValue       = true;
        row.isEnum         = isEnum;
        row.enumKlass      = enumKlass;
        row.elementKlass   = elementKlass;
        row.underlyingType = underlyingType;
        const bool customValueTypeDisplay =
            elementKlass
            && !isEnum
            && m_resolver.module.exports.fnClassIsValueType
            && m_resolver.module.exports.fnClassIsValueType(elementKlass)
            && !Types::IsInlineValueStruct(Types::GetCategory(elementTypeName));
        if (customValueTypeDisplay) {
            // Do not PTR-decode the first 8 slot bytes (GetCategory scar).
            row.valueDisplay = "-";
        }
        else {
            row.valueDisplay = Decode::DecodeFieldValue(decodeType, slotAddress, true);
            if (row.valueDisplay == "null") {
                row.valueDisplay = "[null]";
            }
        }
        view.push_back(std::move(row));
    }

    return view;
}
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
