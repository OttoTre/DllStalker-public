#include "pch.h"

#ifdef ENABLE_DUMPER

#include "dumper/diagnostics.h"

#include <cstdio>

#include "dumper/object_identity.h"
#include "types/type_classifier.h"

namespace Engine::Dumper
{
Diagnostics::Diagnostics(UnityResolver& resolver, const ObjectIdentity& identity)
    : m_resolver(resolver)
    , m_identity(identity)
{
}

void Diagnostics::SafeHexDump(void* instance, size_t size) {
    if (!instance) return;

    printf("[*] --- Raw Memory Dump: %p ---\n", instance);
    unsigned char* p = (unsigned char*)instance;

    for (size_t i = 0; i < size; i += 16) {
        printf("%04zX: ", i);

        for (size_t j = 0; j < 16; j++) {
            if (i + j < size) {
                printf("%02X ", p[i + j]);
            }
            else {
                printf("   ");
            }
        }

        printf(" | ");

        for (size_t j = 0; j < 16; j++) {
            if (i + j < size) {
                unsigned char c = p[i + j];
                printf("%c", (c >= 32 && c <= 126) ? c : '.');
            }
        }

        printf("\n");
    }

    printf("[*] --- End Dump ---\n");
}

void Diagnostics::IdentifyObject(void* instance) {
    if (!instance) {
        printf("[-] NULL instance provided.\n");
        return;
    }

    // KlassFromInstance hides the IL2CPP-vs-Mono indirection. On Mono the
    // first object word is a MonoVTable, not a MonoClass, so a raw deref
    // here would AV; the helper either calls the engine's object_get_class
    // or replicates the right indirection per engine.
    void* klass = m_identity.KlassFromInstance(instance);
    if (!klass) {
        printf("[-] Invalid klass pointer in instance.\n");
        return;
    }

    const char* className = m_resolver.module.exports.fnClassGetName(klass);
    const char* ns = m_resolver.module.exports.fnClassGetNamespace(klass);

    printf("[!] --- Instance Identification ---\n");
    printf("  Location:  %p\n", instance);
    printf("  Class:     %s\n", className ? className : "Unknown");
    printf("  Namespace: %s\n", ns ? ns : "None");
    printf("[!] -------------------------------\n");

    if ((uintptr_t)instance < 0x100000) {
        printf("[-] Instance pointer seems invalid: %p\n", instance);
        return;
    }

    SafeHexDump(instance, m_identity.GetObjectSize(instance));

    if (!m_resolver.module.isIL2CPP) {
        printf("[*] Mono. Skipping static field dump as Mono's static fields are stored differently and require a different approach.\n");
        return;
    }

    void* staticFields = m_resolver.module.exports.fnClassGetStaticFieldsPtr(klass);
    if (staticFields) {
        printf("[*] --- Static Fields (%p) ---\n", staticFields);
        SafeHexDump(staticFields, 0x40);
    }
    else {
        printf("[*] No static fields or failed to retrieve static field pointer.\n");
    }
}

void Diagnostics::SmartSearchAndReplace(void* instance, int searchValue, int newValue) {
    if (!instance) return;

    int size = m_identity.GetObjectSize(instance);
    if (size <= 0 || size > 1024) size = 0x200;

    unsigned char* base = (unsigned char*)instance;
    printf("[*] Scanning (Size: 0x%X) for value: %d\n", size, searchValue);

    // Start at 0x10 to skip object header; step by 4 (int alignment).
    // Bound by sizeof(uintptr_t) because the heuristic below reads 8 bytes from
    // each candidate slot to filter out pointer-sized values; using sizeof(int)
    // here would let that 8-byte read run past the object's allocated size.
    for (int offset = 0x10; offset <= size - (int)sizeof(uintptr_t); offset += 4) {
        int* currentAddr = (int*)(base + offset);
        if (*currentAddr != searchValue) continue;

        if (*(uintptr_t*)currentAddr > Types::kMax32BitValue) continue;

        printf("[+] Match at offset 0x%X. Patching...\n", offset);
        *currentAddr = newValue;
    }
}
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
