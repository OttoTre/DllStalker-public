#include "pch.h"

#ifdef ENABLE_DUMPER

#include "dumper/catalog/object_identity.h"

#include "types/memory_guard.h"

namespace Engine::Dumper
{
ObjectIdentity::ObjectIdentity(const UnityResolver& resolver)
    : m_resolver(resolver)
{
}

void* ObjectIdentity::KlassFromInstance(void* instance) const {
    if (!instance || !Memory::IsReadablePointer(instance, sizeof(void*))) {
        return nullptr;
    }

    // Preferred path: the engine's own object_get_class. It already knows
    // whether the host runtime needs one (IL2CPP) or two (Mono via VTable)
    // indirections, and handles boxed value types correctly.
    if (m_resolver.module.exports.fnObjectGetClass) {
        void* klass = m_resolver.module.exports.fnObjectGetClass(instance);
        return Memory::IsReadablePointer(klass, sizeof(void*)) ? klass : nullptr;
    }

    // Fallback: replicate the indirection logic manually for builds that
    // happen to lack the export. IL2CPP stores Il2CppClass* directly at
    // offset 0; Mono stores a MonoVTable* whose first field is the
    // MonoClass*.
    void* firstWord = *reinterpret_cast<void**>(instance);
    if (!Memory::IsReadablePointer(firstWord, sizeof(void*))) {
        return nullptr;
    }
    if (m_resolver.module.isIL2CPP) {
        return firstWord;
    }
    void* monoClass = *reinterpret_cast<void**>(firstWord);
    return Memory::IsReadablePointer(monoClass, sizeof(void*)) ? monoClass : nullptr;
}

std::string ObjectIdentity::TryGetClassNameFromInstance(void* instance, void** outKlass) const {
    if (outKlass) *outKlass = nullptr;

    void* klass = KlassFromInstance(instance);
    if (!klass) {
        return {};
    }

    if (!m_resolver.module.exports.fnClassGetName) {
        return {};
    }
    const char* name = m_resolver.module.exports.fnClassGetName(klass);
    if (!name || !*name) {
        return {};
    }

    if (outKlass) *outKlass = klass;
    return name;
}

bool ObjectIdentity::IsOrInheritsFrom(void* klass, const char* targetName) const {
    if (!klass || !targetName) return false;
    const auto& exp = m_resolver.module.exports;
    if (!exp.fnClassGetName || !exp.fnGetParent) return false;
    void* cur = klass;
    int depth = 0;
    while (cur && depth++ < 12) {
        const char* name = exp.fnClassGetName(cur);
        if (name && strcmp(name, targetName) == 0) return true;
        cur = exp.fnGetParent(cur);
    }
    return false;
}
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
