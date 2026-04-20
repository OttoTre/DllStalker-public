#include "pch.h"

#ifdef ENABLE_DUMPER

#include "unity_dumper.h"
#include <fstream>
#include <iomanip>

namespace Engine
{
namespace
{
void SafeHexDump(void* instance, size_t size) {
    if (!instance) return;

    printf("[*] --- Raw Memory Dump: %p ---\n", instance);
    unsigned char* p = (unsigned char*)instance;

    for (size_t i = 0; i < size; i += 16) {
        printf("%04zX: ", i); // Offset

        // Hex bytes
        for (size_t j = 0; j < 16; j++) {
            if (i + j < size) {
                printf("%02X ", p[i + j]);
            }
            else {
                printf("   ");
            }
        }

        printf(" | ");

        // ASCII representation
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
} // namespace

UnityDumper::UnityDumper(UnityResolver& resolver) 
    : m_resolver(resolver)
{
    if (!InitDumperExports()) {
        printf("[!] Warning: Some dumper exports failed to initialize.\n");
    }
}

bool UnityDumper::InitDumperExports() {
    std::string prefix = m_resolver.isIL2CPP ? "il2cpp_" : "mono_";

    auto Resolve = [&](const char* name) {
        std::string fullName = prefix + name;
        return GetProcAddress(m_resolver.hModule, fullName.c_str());
    };

    // --- Image (engine-specific) ---
    if (m_resolver.isIL2CPP) {
        fnImageGetClassCount = (t_ImageGetClassCount)Resolve("image_get_class_count");
    }
    else {
        fnImageGetTableInfo = (t_ImageGetTableInfo)Resolve("image_get_table_info");
        fnTableInfoGetRows  = (t_TableInfoGetRows)Resolve("table_info_get_rows");
    }

    // --- Class ---
    fnGetMethods              = (t_ClassGetMethods)Resolve("class_get_methods");
    fnClassGetFields          = (t_ClassGetFields)Resolve("class_get_fields");
    fnGetSize                 = (t_ClassGetInstanceSize)Resolve("class_instance_size");
    fnClassGetStaticFieldsPtr = (t_GetStaticFieldsPtr)Resolve("class_get_static_field_data");

    // --- Method ---
    fnMethodGetName       = (t_MethodGetName)Resolve("method_get_name");
    fnMethodGetParamCount = (t_MethodGetParamCount)Resolve("method_get_param_count");

    // --- Field ---
    fnFieldGetName = (t_FieldGetName)Resolve("field_get_name");

    bool coreValid = (fnMethodGetName && fnMethodGetParamCount && m_resolver.fnGetParent);
    bool engineSpecificValid = m_resolver.isIL2CPP
        ? (fnImageGetClassCount && fnGetMethods)
        : (fnImageGetTableInfo && fnTableInfoGetRows);

    return coreValid && engineSpecificValid;
}

void UnityDumper::DumpClasses(void* image, const char* fileName) {
    if (!image) return;
    m_resolver.EnsureThreadAttached();

    if (!m_resolver.fnClassFromIndex || !m_resolver.fnClassGetName) {
        printf("[-] Required class enumeration functions not available.\n");
        return;
    }

    int classCount = 0;
    if (m_resolver.isIL2CPP) {
        if (fnImageGetClassCount) classCount = fnImageGetClassCount(image);
    }
    else {
        if (fnImageGetTableInfo && fnTableInfoGetRows) {
            void* tableInfo = fnImageGetTableInfo(image, 2);
            classCount = fnTableInfoGetRows(tableInfo);
        }
    }

    if (classCount <= 0) {
        printf("[-] Failed to retrieve class count.\n");
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

    Log("--- Dumping %d Classes ---\n", classCount);

    for (int i = 0; i < classCount; i++) {
        void* klass = nullptr;

        if (m_resolver.isIL2CPP) {
            klass = m_resolver.fnClassFromIndex(image, i);
        }
        else {
            klass = m_resolver.fnClassFromIndex(image, (i + 1) | 0x02000000);
            if (!klass) klass = m_resolver.fnClassFromIndex(image, i + 1);
        }

        if (!klass) continue;

        const char* name = m_resolver.fnClassGetName(klass);
        const char* ns = m_resolver.fnClassGetNamespace ? m_resolver.fnClassGetNamespace(klass) : "";

        Log("[%d] %s%s%s\n", i,
            (ns && ns[0] != '\0') ? ns : "",
            (ns && ns[0] != '\0') ? "::" : "",
            name ? name : "Unnamed");
    }

    Log("--- Dump Complete ---\n");

    if (toFile) {
        printf("[+] Dump saved to: %s\n", fileName);
        outFile.close();
    }
}

void UnityDumper::DumpMethods(void* image, const char* className, const char* ns) {
    m_resolver.EnsureThreadAttached();
    void* klass = m_resolver.fnGetClass(image, ns, className);
    if (!klass) {
        printf("[-] Class '%s' not found.\n", className);
        return;
    }

    DumpMethods(klass);
}

void UnityDumper::DumpMethods(void* klass) {
    if (!klass) return;

    const char* className = m_resolver.fnClassGetName(klass);
    printf("\n[*] --- Method Mapping: %s [%s] ---\n",
        className, m_resolver.isIL2CPP ? "IL2CPP" : "Mono");

    void* iter = nullptr;
    void* method = nullptr;
    int count = 0;

    while ((method = fnGetMethods(klass, &iter)) != nullptr) {
        // 1. Get Name (Usually safe in both)
        const char* name = fnMethodGetName(method);

        int args = 0;
        uintptr_t addr = 0;

        if (m_resolver.isIL2CPP) {
            // IL2CPP: method is MethodInfo*, stable to read
            args = fnMethodGetParamCount(method);
            addr = *(uintptr_t*)method;
        }
        else {
            addr = (uintptr_t)m_resolver.fnCompileMethod(method);

            // Temporary: Skip args for Mono if it's causing the crash
            args = -1;
        }

        if (name) {
            printf("  [+] %-25s (%d args) | %s: %p\n",
                name, args, m_resolver.isIL2CPP ? "RVA" : "JIT", (void*)addr);
            count++;
        }
    }

    // 2. The "Princess in another castle" Logic
    if (count == 0) {
        void* parent = m_resolver.fnGetParent(klass);
        if (parent && parent != klass) {
            printf("  [!] No local methods. Checking Parent: %s\n", m_resolver.fnClassGetName(parent));
            DumpMethods(parent);
        }
    }
    printf("[*] --------------------------------------------\n");
}

void UnityDumper::DumpFields(void* image, const char* className, const char* ns) {
    void* klass = m_resolver.fnGetClass(image, ns, className);
    if (!klass) {
        printf("[-] DumpFields: Could not find class %s\n", className);
        return;
    }

    DumpFields(klass);
}

void UnityDumper::DumpFields(void* klass) {
    if (!klass || !fnClassGetFields || !fnFieldGetName) {
        printf("[-] Field dumping not available.\n");
        return;
    }

    const char* className = m_resolver.fnClassGetName(klass);
    const char* ns = m_resolver.fnClassGetNamespace(klass);

    printf("\n[*] --- Fields for %s [%s] ---\n", className, ns ? ns : "Global");
    void* iter = nullptr;
    void* field = nullptr;

    while ((field = fnClassGetFields(klass, &iter)) != nullptr) {
        const char* name = fnFieldGetName(field);
        size_t offset = m_resolver.fnGetFieldOffset(field);
        printf("  [+] %-20s | Offset: 0x%zX\n", name, offset);
    }
    printf("[*] ------------------------------------\n");
}

int UnityDumper::GetObjectSize(void* instance) const {
    if (!instance) return 0;
    void* klass = *(void**)instance;

    // In most IL2CPP versions, the instance_size is at offset 0xB0 or 0xB8 
    return fnGetSize ? fnGetSize(klass) : 256;
}

void UnityDumper::IdentifyObject(void* instance) {
    if (!instance) {
        printf("[-] NULL instance provided.\n");
        return;
    }

    // The first 8 bytes of an IL2CPP object is the Klass pointer
    void* klass = *(void**)instance;
    if (!klass) {
        printf("[-] Invalid klass pointer in instance.\n");
        return;
    }

    const char* className = m_resolver.fnClassGetName(klass);
    const char* ns = m_resolver.fnClassGetNamespace(klass);

    printf("[!] --- Instance Identification ---\n");
    printf("  Location:  %p\n", instance);
    printf("  Class:     %s\n", className ? className : "Unknown");
    printf("  Namespace: %s\n", ns ? ns : "None");
    printf("[!] -------------------------------\n");

    // Basic sanity check
    if ((uintptr_t)instance < 0x100000) {
        printf("[-] Instance pointer seems invalid: %p\n", instance);
        return;
    }

    SafeHexDump(instance, GetObjectSize(instance));

	if (!m_resolver.isIL2CPP) {
		printf("[*] Mono. Skipping static field dump as Mono's static fields are stored differently and require a different approach.\n");
        return;
    }

    void* staticFields = fnClassGetStaticFieldsPtr(klass);
    if (staticFields) {
        printf("[*] --- Static Fields (%p) ---\n", staticFields);
        // Dump the first 64 bytes of the static block
        SafeHexDump(staticFields, 0x40);
    }
    else {
        printf("[*] No static fields or failed to retrieve static field pointer.\n");
	}
}

void UnityDumper::SmartSearchAndReplace(void* instance, int searchValue, int newValue) {
    if (!instance) return;

    int size = GetObjectSize(instance);
    if (size <= 0 || size > 1024) size = 0x200; // Sanity cap at 1KB

    unsigned char* base = (unsigned char*)instance;

    printf("[*] Scanning SkillDeck (Size: 0x%X) for value: %d\n", size, searchValue);

    // Start at 0x10 to skip Header. 
    // Increment by 4 because ints are 4-byte aligned.
    for (int offset = 0x10; offset <= size - sizeof(int); offset += 4) {
        int* currentAddr = (int*)(base + offset);

        // 1. Check if it matches our search
        if (*currentAddr == searchValue) {

            // 2. Safety Check: Ensure we aren't overwriting a Pointer by mistake.
            // Pointers in 64-bit usually have high bytes like 0x000001... or 0x00007F...
            // If the next 4 bytes are also part of a valid-looking pointer, skip it.
            uintptr_t fullValue = *(uintptr_t*)currentAddr;
            if (fullValue > 0xFFFFFFFF) {
                // This looks like a 64-bit pointer, don't touch it!
                continue;
            }

            printf("[+] Match found at Offset 0x%X. Patching...\n", offset);
            *currentAddr = newValue;
        }
    }
}

} // namespace Engine

#endif // ENABLE_DUMPER
