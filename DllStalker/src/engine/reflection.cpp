#include "pch.h"

#include "engine/reflection.h"

#include "types/memory_guard.h"

#include <cstring>
#include <string>

namespace Engine
{
Reflection::Reflection(const UnityModule& module)
    : m_module(module)
{
}

uintptr_t Reflection::GetMethodAddress(void* image, const char* className, const char* methodName,
                                       int args, const char* ns) const {
    m_module.EnsureThreadAttached();

    void* klass = m_module.exports.fnGetClass(image, ns, className);
    if (!klass) return 0;

    // Walk the inheritance chain once, probing both the raw method name and its
    // property-getter form on each class before climbing to the parent. Saves a
    // full second walk for property-heavy types.
    const bool alreadyGetter = (strncmp(methodName, "get_", 4) == 0);
    const std::string getterName = alreadyGetter ? std::string{} : "get_" + std::string(methodName);

    void* method = nullptr;
    for (void* currentKlass = klass; currentKlass != nullptr; currentKlass = m_module.exports.fnGetParent(currentKlass)) {
        if ((method = m_module.exports.fnGetMethod(currentKlass, methodName, args))) break;
        if (!alreadyGetter && (method = m_module.exports.fnGetMethod(currentKlass, getterName.c_str(), args))) break;
    }

    if (!method) {
        printf("[-] Failed to find method '%s' even in parent classes.\n", methodName);
        return 0;
    }

    if (m_module.isIL2CPP) {
        // Prefer il2cpp_method_get_pointer when resolved. Fallback: MethodInfo
        // historically stores the pointer as the first field (version skew risk).
        if (m_module.exports.fnIl2cppMethodGetPointer) {
            return reinterpret_cast<uintptr_t>(m_module.exports.fnIl2cppMethodGetPointer(method));
        }
        uintptr_t addr = 0;
        if (!Memory::TryReadValue(reinterpret_cast<uintptr_t>(method), addr)) {
            return 0;
        }
        return addr;
    }
    if (!m_module.exports.fnCompileMethod) {
        return 0;
    }
    return reinterpret_cast<uintptr_t>(m_module.exports.fnCompileMethod(method));
}

uintptr_t Reflection::GetFieldOffset(void* image, const char* className, const char* fieldName,
                                     const char* ns) const {
    m_module.EnsureThreadAttached();

    if (!image || !className || !fieldName) {
        return 0;
    }

    void* klass = m_module.exports.fnGetClass(image, ns, className);
    if (!klass) {
        printf("[-] Invalid class for field '%s'.\n", fieldName);
        return 0;
    }

    void* field = m_module.exports.fnGetFieldFromName(klass, fieldName);
    if (!field) {
        printf("[-] Field '%s' not found in class.\n", fieldName);
        return 0;
    }

    uintptr_t offset = (uintptr_t)m_module.exports.fnGetFieldOffset(field);

    if (offset == 0) {
        printf("[!] Warning: Field '%s' returned offset 0. Is it static?\n", fieldName);
    }

    return offset;
}
} // namespace Engine
