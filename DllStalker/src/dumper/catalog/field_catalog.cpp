#include "pch.h"

#ifdef ENABLE_DUMPER

#include "dumper/catalog/field_catalog.h"

#include <cstdio>
#include <cstdint>

#include "services/bootstrap_log.h"
#include "types/memory_guard.h"
#include "types/type_classifier.h"
#include "types/value_decoder.h"
#include "types/value_writer.h"

namespace Engine::Dumper
{
namespace
{
// SEH guards around the engine entry points the field loader reaches into.
// Mono targets with broken/exotic class metadata can AV inside
// mono_class_get_fields (iterator walk) or mono_class_get_static_field_data
// (vtable init / static blob materialization). Each helper is its own
// __declspec(noinline) frame so the outer C++ loop can keep std::string /
// std::vector locals without violating MSVC's "no C++ object destruction in
// a __try function" rule; matches the runtime_invoke pattern.
using GetFieldsFn = void* (__cdecl*)(void* klass, void** iter);
using StaticPtrFn = void* (__cdecl*)(void* klass);

__declspec(noinline) bool SafeGetFieldsStep(GetFieldsFn fn, void* klass, void** iter,
                                             void*& outField,
                                             unsigned long& outSehCode) noexcept {
    __try {
        outField = fn(klass, iter);
        return true;
    }
    __except (outSehCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
        outField = nullptr;
        return false;
    }
}

__declspec(noinline) bool SafeClassGetStaticFieldsPtr(StaticPtrFn fn, void* klass,
                                                       void*& outPtr,
                                                       unsigned long& outSehCode) noexcept {
    __try {
        outPtr = fn(klass);
        return true;
    }
    __except (outSehCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
        outPtr = nullptr;
        return false;
    }
}
constexpr uint32_t kFieldAttrLiteral = 0x0040u;

bool ReadInt64AtAddress(uintptr_t address, Types::TypeCategory cat, int64_t& out) {
    using Cat = Types::TypeCategory;
    switch (cat) {
    case Cat::I1: {
        int8_t v = 0;
        if (!Memory::TryReadValue(address, v)) return false;
        out = v;
        return true;
    }
    case Cat::I2: {
        int16_t v = 0;
        if (!Memory::TryReadValue(address, v)) return false;
        out = v;
        return true;
    }
    case Cat::I4: {
        int32_t v = 0;
        if (!Memory::TryReadValue(address, v)) return false;
        out = v;
        return true;
    }
    case Cat::I8: {
        int64_t v = 0;
        if (!Memory::TryReadValue(address, v)) return false;
        out = v;
        return true;
    }
    case Cat::U1: {
        uint8_t v = 0;
        if (!Memory::TryReadValue(address, v)) return false;
        out = static_cast<int64_t>(v);
        return true;
    }
    case Cat::U2: {
        uint16_t v = 0;
        if (!Memory::TryReadValue(address, v)) return false;
        out = static_cast<int64_t>(v);
        return true;
    }
    case Cat::U4: {
        uint32_t v = 0;
        if (!Memory::TryReadValue(address, v)) return false;
        out = static_cast<int64_t>(v);
        return true;
    }
    case Cat::U8: {
        uint64_t v = 0;
        if (!Memory::TryReadValue(address, v)) return false;
        out = static_cast<int64_t>(v);
        return true;
    }
    default:
        return false;
    }
}

bool SetStringFieldValue(UnityResolver& resolver, const FieldInfo& field,
                         const std::string& newValue, std::string* error) {
    if (!field.hasValue || !field.valueAddress) {
        if (error) *error = "Field has no writable address";
        return false;
    }

    resolver.module.EnsureThreadAttached();

    const std::string payload = Decode::NormalizeStringFieldInput(newValue);
    if (payload.empty()) {
        const uintptr_t nullPtr = 0;
        if (!Memory::TryWriteValue(field.valueAddress, nullPtr)) {
            if (error) *error = "Memory write failed (Access Denied)";
            return false;
        }
        return true;
    }

    void* managed = nullptr;
    if (resolver.module.isIL2CPP) {
        if (!resolver.module.exports.fnIl2cppStringNew) {
            if (error) *error = "il2cpp_string_new not resolved";
            return false;
        }
        managed = resolver.module.exports.fnIl2cppStringNew(payload.c_str());
    }
    else {
        if (!resolver.module.exports.fnMonoStringNew || !resolver.module.domain) {
            if (error) *error = "mono_string_new / domain not resolved";
            return false;
        }
        managed = resolver.module.exports.fnMonoStringNew(resolver.module.domain, payload.c_str());
    }

    if (!managed) {
        if (error) *error = "Failed to allocate managed string";
        return false;
    }

    const uintptr_t managedPtr = reinterpret_cast<uintptr_t>(managed);
    if (!Memory::TryWriteValue(field.valueAddress, managedPtr)) {
        if (error) *error = "Memory write failed (Access Denied)";
        return false;
    }
    return true;
}
} // namespace

FieldCatalog::FieldCatalog(UnityResolver& resolver)
    : m_resolver(resolver)
{
}

std::vector<FieldInfo> FieldCatalog::GetRawFields(void* klass) {
    return GetRawFields(klass, nullptr, nullptr);
}

std::vector<FieldInfo> FieldCatalog::GetRawFields(void* klass, void* instancePtr,
                                                  bool* enumerationComplete,
                                                  bool metadataOnly) {
    std::vector<FieldInfo> fields;
    if (enumerationComplete) {
        *enumerationComplete = true;
    }
    if (!klass || !m_resolver.module.exports.fnClassGetFields || !m_resolver.module.exports.fnFieldGetName) {
        return fields;
    }

    m_resolver.module.EnsureThreadAttached();
    void* iter = nullptr;
    void* field = nullptr;

    // Cached once before the loop so diagnostic logging keeps a stable class
    // label even if iteration faults later. class_get_name was already
    // exercised by ClassCatalog when this klass was put on screen.
    const char* klassName = m_resolver.module.exports.fnClassGetName
        ? m_resolver.module.exports.fnClassGetName(klass) : nullptr;
    const char* enginePrefix = m_resolver.module.isIL2CPP ? "il2cpp" : "mono";

    // Per-class throttle: a single broken static blob can hit every static
    // field in the class; one log line is enough to point at the culprit.
    bool staticPtrFailureLogged = false;

    while (true) {
        unsigned long iterSeh = 0;
        if (!SafeGetFieldsStep(m_resolver.module.exports.fnClassGetFields, klass, &iter, field, iterSeh)) {
            Engine::Services::BootstrapLog::Write(
                "[!] SEH 0x%08lX in %s_class_get_fields: klass=%p (%s) -- stopping enumeration after %zu field(s)\n",
                   iterSeh, enginePrefix, klass,
                   klassName ? klassName : "?",
                   fields.size());
            if (enumerationComplete) {
                *enumerationComplete = false;
            }
            break;
        }
        if (!field) break;

        const char* name = m_resolver.module.exports.fnFieldGetName(field);
        size_t offset = m_resolver.module.exports.fnGetFieldOffset(field);
        uintptr_t staticValueAddr = 0;
        uintptr_t valueAddress = 0;
        bool hasValue = false;
        std::string typeName = "Unknown";

        if (m_resolver.module.exports.fnFieldGetType && m_resolver.module.exports.fnTypeGetName) {
            void* fieldType = m_resolver.module.exports.fnFieldGetType(field);
            const char* rawTypeName = fieldType ? m_resolver.module.exports.fnTypeGetName(fieldType) : nullptr;
            if (rawTypeName && rawTypeName[0] != '\0') {
                typeName = rawTypeName;
            }
        }

        bool isStatic = false;
        if (m_resolver.module.exports.fnFieldGetFlags) {
            uint32_t flags = m_resolver.module.exports.fnFieldGetFlags(field);
            isStatic = ((flags & 0x0010u) != 0u);
        }

        // Schema / name-gate path: offsets + kinds only — no static blob SEH
        // and no DecodeFieldValue. Live Search still uses the full path.
        if (!metadataOnly) {
            if (isStatic) {
                void* staticData = nullptr;
                if (m_resolver.module.exports.fnClassGetStaticFieldsPtr) {
                    unsigned long staticSeh = 0;
                    if (!SafeClassGetStaticFieldsPtr(
                            m_resolver.module.exports.fnClassGetStaticFieldsPtr,
                            klass, staticData, staticSeh)) {
                        if (!staticPtrFailureLogged) {
                            Engine::Services::BootstrapLog::Write(
                                "[!] SEH 0x%08lX in %s_class_get_static_field_data: klass=%p (%s) first faulting static field='%s' -- skipping static data for this class\n",
                                   staticSeh, enginePrefix, klass,
                                   klassName ? klassName : "?",
                                   name ? name : "?");
                            staticPtrFailureLogged = true;
                        }
                        staticData = nullptr;
                    }
                }
                if (staticData) {
                    staticValueAddr = (uintptr_t)staticData + offset;
                    valueAddress = staticValueAddr;
                    hasValue = true;
                }
            }
            else if (instancePtr) {
                valueAddress = (uintptr_t)instancePtr + offset;
                hasValue = true;
            }
        }

        FieldInfo info{};
        info.name         = name ? name : "UNKNOWN_FIELD";
        info.type         = typeName;
        info.offset       = offset;
        info.isStatic     = isStatic;
        info.staticValue  = staticValueAddr;
        info.valueAddress = valueAddress;
        info.hasValue     = hasValue;

        if (m_resolver.module.exports.fnFieldGetType
            && m_resolver.module.exports.fnClassFromType
            && m_resolver.module.exports.fnClassIsEnum
            && m_resolver.module.exports.fnClassEnumBasetype) {
            if (void* fieldType = m_resolver.module.exports.fnFieldGetType(field)) {
                if (void* fieldKlass = m_resolver.module.exports.fnClassFromType(fieldType)) {
                    if (m_resolver.module.exports.fnClassIsEnum(fieldKlass)) {
                        info.isEnum    = true;
                        info.enumKlass = fieldKlass;
                        if (void* baseType = m_resolver.module.exports.fnClassEnumBasetype(fieldKlass)) {
                            if (m_resolver.module.exports.fnTypeGetName) {
                                if (const char* baseName = m_resolver.module.exports.fnTypeGetName(baseType)) {
                                    if (baseName[0] != '\0') {
                                        info.underlyingType = baseName;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        if (!metadataOnly) {
            const std::string decodeType = info.isEnum && !info.underlyingType.empty()
                                         ? info.underlyingType : typeName;
            info.valueDisplay = Decode::DecodeFieldValue(decodeType, valueAddress, hasValue);
        }
        fields.push_back(std::move(info));
    }

    return fields;
}

std::vector<uintptr_t> FieldCatalog::EnumerateStaticFieldAddresses(void* klass) {
    std::vector<uintptr_t> addresses;
    if (!klass || !m_resolver.module.exports.fnClassGetFields
        || !m_resolver.module.exports.fnClassGetStaticFieldsPtr
        || !m_resolver.module.exports.fnGetFieldOffset
        || !m_resolver.module.exports.fnFieldGetFlags) {
        return addresses;
    }

    m_resolver.module.EnsureThreadAttached();

    void* staticData = nullptr;
    {
        unsigned long staticSeh = 0;
        if (!SafeClassGetStaticFieldsPtr(
                m_resolver.module.exports.fnClassGetStaticFieldsPtr,
                klass, staticData, staticSeh)
            || !staticData) {
            return addresses;
        }
    }

    void* iter = nullptr;
    void* field = nullptr;
    while (true) {
        unsigned long iterSeh = 0;
        if (!SafeGetFieldsStep(m_resolver.module.exports.fnClassGetFields, klass, &iter, field, iterSeh)) {
            break;
        }
        if (!field) {
            break;
        }

        const uint32_t flags = m_resolver.module.exports.fnFieldGetFlags(field);
        if ((flags & 0x0010u) == 0u) {
            continue;
        }

        const size_t offset = m_resolver.module.exports.fnGetFieldOffset(field);
        addresses.push_back(reinterpret_cast<uintptr_t>(staticData) + offset);
    }

    return addresses;
}

void* FieldCatalog::TryResolveFieldTypeKlass(void* ownerKlass, const char* fieldName) const {
    if (!ownerKlass || !fieldName || !fieldName[0]
        || !m_resolver.module.exports.fnGetFieldFromName
        || !m_resolver.module.exports.fnFieldGetType
        || !m_resolver.module.exports.fnClassFromType) {
        return nullptr;
    }
    void* field = m_resolver.module.exports.fnGetFieldFromName(ownerKlass, fieldName);
    if (!field) {
        return nullptr;
    }
    void* fieldType = m_resolver.module.exports.fnFieldGetType(field);
    if (!fieldType) {
        return nullptr;
    }
    return m_resolver.module.exports.fnClassFromType(fieldType);
}

std::vector<EnumLiteral> FieldCatalog::GetEnumLiterals(void* enumKlass) {
    std::vector<EnumLiteral> literals;
    if (!enumKlass || !m_resolver.module.exports.fnClassGetFields
        || !m_resolver.module.exports.fnFieldGetName
        || !m_resolver.module.exports.fnGetFieldOffset) {
        return literals;
    }

    m_resolver.module.EnsureThreadAttached();

    std::string underlyingType;
    if (m_resolver.module.exports.fnClassEnumBasetype && m_resolver.module.exports.fnTypeGetName) {
        if (void* baseType = m_resolver.module.exports.fnClassEnumBasetype(enumKlass)) {
            if (const char* baseName = m_resolver.module.exports.fnTypeGetName(baseType)) {
                if (baseName[0] != '\0') {
                    underlyingType = baseName;
                }
            }
        }
    }
    if (underlyingType.empty()) {
        underlyingType = "System.Int32";
    }

    const Types::TypeCategory underlyingCat = Types::GetCategory(underlyingType);

    void* staticData = nullptr;
    if (m_resolver.module.exports.fnClassGetStaticFieldsPtr) {
        unsigned long staticSeh = 0;
        if (!SafeClassGetStaticFieldsPtr(
                m_resolver.module.exports.fnClassGetStaticFieldsPtr,
                enumKlass, staticData, staticSeh)) {
            staticData = nullptr;
        }
    }
    if (!staticData) {
        return literals;
    }

    void* iter = nullptr;
    void* enumField = nullptr;
    while (true) {
        unsigned long iterSeh = 0;
        if (!SafeGetFieldsStep(m_resolver.module.exports.fnClassGetFields, enumKlass, &iter, enumField, iterSeh)) {
            break;
        }
        if (!enumField) {
            break;
        }

        if (m_resolver.module.exports.fnFieldGetFlags) {
            const uint32_t flags = m_resolver.module.exports.fnFieldGetFlags(enumField);
            if ((flags & kFieldAttrLiteral) == 0u) {
                continue;
            }
        }

        const char* litName = m_resolver.module.exports.fnFieldGetName(enumField);
        const size_t litOffset = m_resolver.module.exports.fnGetFieldOffset(enumField);
        int64_t litValue = 0;
        if (!ReadInt64AtAddress(reinterpret_cast<uintptr_t>(staticData) + litOffset, underlyingCat, litValue)) {
            continue;
        }

        literals.push_back({ litName ? litName : "?", litValue });
    }

    return literals;
}

bool FieldCatalog::SetFieldValue(const FieldInfo& field, const std::string& newValue, std::string* error) {
    if (field.isEnum && !field.underlyingType.empty()) {
        FieldInfo shim = field;
        shim.type = field.underlyingType;
        return Write::SetFieldValue(shim, newValue, error);
    }
    if (Types::GetCategory(field.type) == Types::TypeCategory::STRING) {
        return SetStringFieldValue(m_resolver, field, newValue, error);
    }
    return Write::SetFieldValue(field, newValue, error);
}
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
