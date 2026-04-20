#include "pch.h"

#include <fstream>
#include <iomanip>

#include "unity_resolver.h"

namespace Engine
{
namespace 
{
struct SearchContext {
    const char* targetName;
    void* foundImage;
    UnityResolver* resolver;
};

void __cdecl OnAssemblyIterate(void* assembly, void* user_data) {
    Sleep(100);
    auto ctx = static_cast<SearchContext*>(user_data);
    if (!assembly || ctx->foundImage) return;

    // Use the resolver instance passed in the context to call exports
    void* image = ctx->resolver->fnGetImage(assembly);
    const char* name = ctx->resolver->fnGetImageName(image);

    //if (name) printf("[DEBUG] Checking Assembly: %s\n", name);
    if (name && strstr(name, ctx->targetName)) {
        ctx->foundImage = image;
    }
}
} // namespace

UnityResolver Unity;

bool UnityResolver::Init() {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    if (strstr(path, "CrashHandler")) return false;

    printf("[*] Unity Resolver Initializing...\n");
    while (true) {
        Sleep(500);
        if ((hModule = GetModuleHandleA("GameAssembly.dll"))) { isIL2CPP = true; break; }
        if ((hModule = GetModuleHandleA("mono-2.0-bdwgc.dll"))) { isIL2CPP = false; break; }
    }

    if (!ResolveExports()) return false;

    printf("[*] Exports resolved. Attempting to get domain...\n");
    int attempts = 0;
    while (attempts < 100) {
        __try {
            domain = fnGetDomain();
            if (domain) break;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { domain = nullptr; }
        Sleep(100);
        attempts++;
    }

    printf("[*] Domain retrieval %s after %d attempts.\n", domain ? "succeeded" : "failed", attempts);
    if (!domain) return false;

    EnsureThreadAttached();
    printf("[+] Unity Resolver Ready (%s)\n", isIL2CPP ? "IL2CPP" : "Mono");
    return true;
}

bool UnityResolver::ResolveExports() {
    std::string prefix = isIL2CPP ? "il2cpp_" : "mono_";

    auto Resolve = [&](const char* name) {
        std::string fullName = prefix + name;
        return GetProcAddress(hModule, fullName.c_str());
    };

    // --- Domain / Thread ---
    fnGetDomain    = (t_GetDomain)(isIL2CPP ? Resolve("domain_get") : Resolve("get_root_domain"));
    fnThreadAttach = (t_ThreadAttach)Resolve("thread_attach");

    // --- Assembly / Image ---
    fnGetImage     = (t_GetImage)Resolve("assembly_get_image");
    fnGetImageName = (t_GetImageName)Resolve("image_get_name");

    // --- Class ---
    fnGetClass          = (t_GetClass)Resolve("class_from_name");
    fnClassGetName      = (t_ClassGetName)Resolve("class_get_name");
    fnClassGetNamespace = (t_ClassGetNamespace)Resolve("class_get_namespace");
    fnClassFromIndex    = (t_ClassFromIndex)(isIL2CPP ? Resolve("image_get_class") : Resolve("class_get"));
    fnGetParent         = (t_GetParent)Resolve("class_get_parent");

    // --- Method ---
    fnGetMethod = (t_GetMethod)Resolve("class_get_method_from_name");

    // --- Field ---
    fnGetFieldFromName = (t_FieldFromName)Resolve("class_get_field_from_name");
    fnGetFieldOffset   = (t_FieldGetOffset)Resolve("field_get_offset");

    // --- Engine-Specific ---
    if (isIL2CPP) {
        fnGetAssemblies      = (t_GetAssemblies)Resolve("domain_get_assemblies");
        fnFieldGetStaticAddr = (t_FieldGetStaticAddr)Resolve("field_get_static_terminate_data");
        if (!fnFieldGetStaticAddr) {
            printf("[*] Fallback: Trying alternative export for field_get_static_terminate_data\n");
            fnFieldGetStaticAddr = (t_FieldGetStaticAddr)Resolve("field_static_get_value");
        }
    }
    else {
        fnCompileMethod = (t_CompileMethod)Resolve("compile_method");
        fnGetVTable     = (t_ClassGetVTable)Resolve("class_vtable");
        fnRuntimeInvoke = (t_RuntimeInvoke)Resolve("runtime_invoke");
    }

    bool engineSpecificValid = isIL2CPP ? true : (fnCompileMethod != nullptr);
    bool coreValid = (fnGetDomain && fnGetClass && fnGetMethod && fnThreadAttach && engineSpecificValid);

    if (!coreValid) {
        printf("[-] Failed to resolve core Unity exports.\n");
    }

    return coreValid;
}

void UnityResolver::EnsureThreadAttached() const {
    if (fnThreadAttach && domain) fnThreadAttach(domain);
}

void* UnityResolver::FindImage(const char* assemblyName) {
    this->EnsureThreadAttached();
    if (!domain) return nullptr;

    SearchContext ctx = { assemblyName, nullptr, this };
    int retryCount = 0;
    const int maxRetries = 10; // 5 seconds total

    printf("[*] Searching for image: %s...\n", assemblyName);

    while (retryCount < maxRetries) {
        // --- Stage A: Mono Open ---
        if (!isIL2CPP) {
;           typedef void* (__cdecl* t_mono_open)(void*, const char*);
            auto fnOpen = (t_mono_open)GetProcAddress(hModule, "mono_domain_assembly_open");
            if (fnOpen) {
                void* ass = fnOpen(domain, assemblyName);
                if (ass) {
                    ctx.foundImage = fnGetImage(ass);
                    if (ctx.foundImage) break;
                }
            }
        }

        // --- Stage B: Fallback Iterator (Works for both) ---
        if (isIL2CPP) {
            size_t size = 0;
            void** assemblies = fnGetAssemblies(domain, &size);
            if (assemblies) {
                for (size_t i = 0; i < size; ++i) {
                    void* img = fnGetImage(assemblies[i]);
                    const char* name = fnGetImageName(img);
                    if (name && strstr(name, assemblyName)) {
                        printf("[+] Found image: %s\n", name);
                        ctx.foundImage = img;
                        break;
                    }
                }
            }
        }
        else {
            typedef void(__cdecl* t_AssemblyForeach)(void* func, void* user_data);
            auto fnForeach = (t_AssemblyForeach)GetProcAddress(hModule, "mono_assembly_foreach");
            if (fnForeach) {
                fnForeach((void*)OnAssemblyIterate, &ctx);
            }
        }

        if (ctx.foundImage) break;

        Sleep(500);
        retryCount++;
        printf("[.] Retry number: %d\n", retryCount);
    }

    if (ctx.foundImage) {
        printf("[+] Image '%s' found after %d retries.\n", assemblyName, retryCount);
        return ctx.foundImage;
    }
    else {
        printf("[-] Image '%s' NOT found (Timeout).\n", assemblyName);
        return nullptr;
    }
}

uintptr_t UnityResolver::GetMethodAddress(void* image, const char* className, const char* methodName, int args, const char* ns) const {
    void* klass = fnGetClass(image, ns, className);
    if (!klass) return 0;

    void* method = nullptr;
    void* currentKlass = klass;

    while (currentKlass != nullptr) {
        method = fnGetMethod(currentKlass, methodName, args);
        if (method) break;

        currentKlass = fnGetParent(currentKlass);
    }

    // If still not found, try the "Property Getter" prefix
    if (!method && strncmp(methodName, "get_", 4) != 0) {
        std::string getterName = "get_" + std::string(methodName);
        currentKlass = klass; // Reset to child class
        while (currentKlass != nullptr) {
            method = fnGetMethod(currentKlass, getterName.c_str(), args);
            if (method) break;
            currentKlass = fnGetParent(currentKlass);
        }
    }

    if (!method) {
        printf("[-] Failed to find method '%s' even in parent classes.\n", methodName);
        return 0;
    }

    return isIL2CPP ? *(uintptr_t*)method : (uintptr_t)fnCompileMethod(method);
}

uintptr_t UnityResolver::GetFieldOffset(void* image, const char* className, const char* fieldName, const char* ns) const {
    this->EnsureThreadAttached();

    if (!image || !fnGetFieldFromName || !fnGetFieldOffset) {
        printf("[-] Field resolution functions missing.\n");
        return 0;
    }

    void* klass = fnGetClass(image, ns, className);
    if (!klass || !fnGetFieldOffset) {
        printf("[-] Invalid class or missing field offset function.\n");
        return 0;
    }
    void* field = fnGetFieldFromName(klass, fieldName);
    if (!field) {
        printf("[-] Field '%s' not found in class.\n", fieldName);
        return 0;
    }

    uintptr_t offset = (uintptr_t)fnGetFieldOffset(field);

    if (offset == 0) {
        printf("[!] Warning: Field '%s' returned offset 0. Is it static?\n", fieldName);
    }

    return offset;
}

void* UnityResolver::GetStaticFieldAddr(void* image, const char* className, const char* fieldName, const char* ns) const {
    this->EnsureThreadAttached();
    if (!image) return nullptr;

    void* klass = fnGetClass(image, ns, className);
    if (!klass) return nullptr;

    void* field = fnGetFieldFromName(klass, fieldName);
    if (!field) return nullptr;

    if (isIL2CPP) {
        return fnFieldGetStaticAddr(field);
    }
    else {
        // Mono: Find the VTable for this class
        void* vtable = fnGetVTable(domain, klass);
        if (!vtable) return nullptr;

        // In Mono, the static data is stored at an offset inside the VTable
        uintptr_t offset = (uintptr_t)fnGetFieldOffset(field);
        return (void*)((uintptr_t)vtable + offset);
    }
}

} // namespace Engine
