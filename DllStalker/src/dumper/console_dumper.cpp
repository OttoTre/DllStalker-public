#include "pch.h"

#ifdef ENABLE_DUMPER

#include "dumper/console_dumper.h"

#include <cstdarg>
#include <cstdio>
#include <fstream>

#include "dumper/class_catalog.h"

namespace Engine::Dumper
{
ConsoleDumper::ConsoleDumper(UnityResolver& resolver, ClassCatalog& classes)
    : m_resolver(resolver)
    , m_classes(classes)
{
}

void ConsoleDumper::DumpClasses(void* image, const char* fileName) {
    const auto klassInfo = m_classes.GetRawClasses(image);

    if (klassInfo.empty()) {
        printf("[-] No classes found to dump.\n");
        return;
    }

    /* Setup Output (File or Console) */
    std::ofstream outFile;
    bool toFile = (fileName != nullptr);
    if (toFile) {
        outFile.open(fileName, std::ios::out | std::ios::trunc);
        if (!outFile.is_open()) {
            printf("[!] Failed to open file for dumping: %s\n", fileName);
            toFile = false;
        }
    }

    auto Log = [&](const char* format, ...) {
        char buffer[512];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        if (toFile) outFile << buffer;
        else printf("%s", buffer);
        };

    Log("--- Dumping %zu Classes ---\n", klassInfo.size());
    for (const auto& klass : klassInfo) {
        Log("[%p] %s%s%s\n", klass.klassPtr,
            (klass.ns.empty()) ? "" : klass.ns.c_str(),
            (klass.ns.empty()) ? "" : "::",
            klass.name.c_str());
    }
    Log("--- Dump Complete ---\n");

    if (toFile) {
        printf("[+] Dump saved to: %s\n", fileName);
        outFile.close();
    }
}

void ConsoleDumper::DumpMethods(void* image, const char* className, const char* ns) {
    m_resolver.module.EnsureThreadAttached();
    void* klass = m_resolver.module.exports.fnGetClass(image, ns, className);
    if (!klass) {
        printf("[-] Class '%s' not found.\n", className);
        return;
    }

    DumpMethods(klass);
}

void ConsoleDumper::DumpMethods(void* klass) {
    if (!klass) return;

    m_resolver.module.EnsureThreadAttached();

    const char* className = m_resolver.module.exports.fnClassGetName(klass);
    printf("\n[*] --- Method Mapping: %s [%s] ---\n", className, m_resolver.module.isIL2CPP ? "IL2CPP" : "Mono");

    void* iter   = nullptr;
    void* method = nullptr;
    int   count  = 0;

    while ((method = m_resolver.module.exports.fnGetMethods(klass, &iter)) != nullptr) {
        const char* name = m_resolver.module.exports.fnMethodGetName(method);
        if (!name) continue;

        int       args = 0;
        uintptr_t addr = 0;

        if (m_resolver.module.isIL2CPP) {
            args = m_resolver.module.exports.fnMethodGetParamCount ? m_resolver.module.exports.fnMethodGetParamCount(method) : 0;
            addr = *(uintptr_t*)method;
        }
        else if (m_resolver.module.exports.fnCompileMethod) {
            addr = (uintptr_t)m_resolver.module.exports.fnCompileMethod(method);
            // Mono lacks a direct param-count export. Reconstruct it via
            // the signature accessor pair when available; -1 keeps the
            // existing "unknown" sentinel for builds where the pair didn't
            // resolve.
            args = -1;
            if (m_resolver.module.exports.fnMonoMethodSignature && m_resolver.module.exports.fnMonoSignatureGetParamCount) {
                if (void* sig = m_resolver.module.exports.fnMonoMethodSignature(method)) {
                    args = static_cast<int>(m_resolver.module.exports.fnMonoSignatureGetParamCount(sig));
                }
            }
        }

        printf("  [+] %-25s (%d args) | %s: %p\n",
            name, args, m_resolver.module.isIL2CPP ? "RVA" : "JIT", (void*)addr);
        count++;
    }

    if (count == 0) {
        void* parent = m_resolver.module.exports.fnGetParent ? m_resolver.module.exports.fnGetParent(klass) : nullptr;
        if (parent && parent != klass) {
            printf("  [!] No local methods. Checking Parent: %s\n", m_resolver.module.exports.fnClassGetName(parent));
            DumpMethods(parent);
        }
    }

    printf("[*] --------------------------------------------\n");
}

void ConsoleDumper::DumpFields(void* image, const char* className, const char* ns) {
    m_resolver.module.EnsureThreadAttached();
    void* klass = m_resolver.module.exports.fnGetClass(image, ns, className);
    if (!klass) {
        printf("[-] DumpFields: Could not find class %s\n", className);
        return;
    }

    DumpFields(klass);
}

void ConsoleDumper::DumpFields(void* klass) {
    if (!klass || !m_resolver.module.exports.fnClassGetFields || !m_resolver.module.exports.fnFieldGetName) {
        printf("[-] Field dumping not available.\n");
        return;
    }

    m_resolver.module.EnsureThreadAttached();

    const char* className = m_resolver.module.exports.fnClassGetName(klass);
    const char* ns        = m_resolver.module.exports.fnClassGetNamespace(klass);

    printf("\n[*] --- Fields for %s [%s] ---\n", className, ns ? ns : "Global");

    void* iter  = nullptr;
    void* field = nullptr;

    while ((field = m_resolver.module.exports.fnClassGetFields(klass, &iter)) != nullptr) {
        const char* name   = m_resolver.module.exports.fnFieldGetName(field);
        size_t      offset = m_resolver.module.exports.fnGetFieldOffset(field);
        printf("  [+] %-20s | Offset: 0x%zX\n", name ? name : "?", offset);
    }

    printf("[*] ------------------------------------\n");
}
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
