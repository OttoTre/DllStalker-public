#include "pch.h"

#ifdef ENABLE_DUMPER

#include "dumper/collection_view.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>

#include "dumper/object_identity.h"
#include "types/memory_guard.h"
#include "types/type_classifier.h"
#include "types/value_decoder.h"

namespace Engine::Dumper
{
namespace
{
// IL2CPP Il2CppArray and Mono MonoArray happen to share an identical header
// layout on x64: length at +0x18, first element at +0x20.
namespace UnityArrayLayout {
    constexpr uintptr_t LengthOffset   = 0x18;
    constexpr uintptr_t ElementsOffset = 0x20;
}
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

CollectionView::CollectionView(UnityResolver& resolver, const ObjectIdentity& identity)
    : m_resolver(resolver)
    , m_identity(identity)
{
}

std::vector<FieldInfo> CollectionView::GetCollectionView(const FieldInfo& field) const {
    std::vector<FieldInfo> view;

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
        if (!Memory::IsReadablePointer(reinterpret_cast<void*>(arrayBase + UnityArrayLayout::LengthOffset),
                                       sizeof(size_t))) {
            return view;
        }
        std::memcpy(&length,
                    reinterpret_cast<void*>(arrayBase + UnityArrayLayout::LengthOffset),
                    sizeof(size_t));
        elementTypeName = StripArraySuffix(field.type);
    }
    else {
        // LIST path: read the wrapper, then look up "_items" + "_size" by
        // name on the wrapper's klass. Going through the engine's name
        // lookup is robust against List<T>'s layout shifting between
        // Unity / .NET versions, at the cost of one extra resolve per
        // unique List<T> klass per click.
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
        if (!Memory::IsReadablePointer(reinterpret_cast<void*>(arrayBase + UnityArrayLayout::LengthOffset),
                                       sizeof(size_t))) {
            return view;
        }
        size_t allocated = 0;
        std::memcpy(&allocated,
                    reinterpret_cast<void*>(arrayBase + UnityArrayLayout::LengthOffset),
                    sizeof(size_t));
        length = std::min<size_t>(static_cast<size_t>(logicalSize), allocated);
        elementTypeName = ExtractListElementName(field.type);
    }

    if (length == 0 || length > kMaxFindObjectsResultLength) {
        return view;
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
            view.push_back({
                "<value-type elements>",
                elementTypeName,
                0,
                0,
                0,
                false,
                "engine missing class_value_size"
            });
            return view;
        }
    }

    const uintptr_t elementsBase = arrayBase + UnityArrayLayout::ElementsOffset;
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
        row.name         = nameBuf;
        row.type         = elementTypeName;
        row.offset       = i * elementSize;
        row.staticValue  = 0;
        row.valueAddress = slotAddress;
        row.hasValue     = true;
        row.valueDisplay = Decode::DecodeFieldValue(elementTypeName, slotAddress, true);
        view.push_back(std::move(row));
    }

    return view;
}
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
