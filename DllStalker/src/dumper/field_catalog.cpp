#include "pch.h"

#ifdef ENABLE_DUMPER

#include "dumper/field_catalog.h"

#include "types/value_decoder.h"
#include "types/value_writer.h"

namespace Engine::Dumper
{
FieldCatalog::FieldCatalog(UnityResolver& resolver)
    : m_resolver(resolver)
{
}

std::vector<FieldInfo> FieldCatalog::GetRawFields(void* klass) {
    return GetRawFields(klass, nullptr);
}

std::vector<FieldInfo> FieldCatalog::GetRawFields(void* klass, void* instancePtr) {
    std::vector<FieldInfo> fields;
    if (!klass || !m_resolver.module.exports.fnClassGetFields || !m_resolver.module.exports.fnFieldGetName) {
        return fields;
    }

    m_resolver.module.EnsureThreadAttached();
    void* iter = nullptr;
    void* field = nullptr;

    while ((field = m_resolver.module.exports.fnClassGetFields(klass, &iter)) != nullptr) {
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

        if (isStatic) {
            void* staticData = m_resolver.module.exports.fnClassGetStaticFieldsPtr
                ? m_resolver.module.exports.fnClassGetStaticFieldsPtr(klass) : nullptr;
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

        fields.push_back({
            name ? name : "UNKNOWN_FIELD",
            typeName,
            offset,
            staticValueAddr,
            valueAddress,
            hasValue,
            Decode::DecodeFieldValue(typeName, valueAddress, hasValue)
            });
    }

    return fields;
}

bool FieldCatalog::SetFieldValue(const FieldInfo& field, const std::string& newValue, std::string* error) {
    return Write::SetFieldValue(field, newValue, error);
}
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
