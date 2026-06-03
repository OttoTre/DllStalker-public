#include "pch.h"

#include "engine/unity_module.h"

#include <string>

namespace Engine
{
bool UnityModule::WaitForModule() {
    constexpr int kModuleWaitMaxAttempts = 60; // (~30s @ 500ms cadence).
    int attempts = 0;
    while (attempts < kModuleWaitMaxAttempts) {
        Sleep(500);
        if ((hModule = GetModuleHandleA("GameAssembly.dll"))) { isIL2CPP = true; return true; }
        if ((hModule = GetModuleHandleA("mono-2.0-bdwgc.dll"))) { isIL2CPP = false; return true; }
        ++attempts;
    }
    printf("[-] Timed out waiting for IL2CPP/Mono module to load (%d attempts).\n", kModuleWaitMaxAttempts);
    return false;
}

bool UnityModule::ResolveExports() {
    using E = UnityExports;
    const std::string prefix = isIL2CPP ? "il2cpp_" : "mono_";

    auto Resolve = [&](const char* name) {
        const std::string fullName = prefix + name;
        return GetProcAddress(hModule, fullName.c_str());
    };

    // --- Domain / Thread ---
    exports.fnGetDomain    = (E::t_GetDomain)(isIL2CPP ? Resolve("domain_get") : Resolve("get_root_domain"));
    exports.fnThreadAttach = (E::t_ThreadAttach)Resolve("thread_attach");

    // --- Assembly / Image ---
    exports.fnGetImage     = (E::t_GetImage)Resolve("assembly_get_image");
    exports.fnGetImageName = (E::t_GetImageName)Resolve("image_get_name");

    // --- Object (shared) ---
    // Both engines export "object_get_class" with the same signature; this is
    // the cross-engine source of truth for "managed instance -> class" and is
    // what keeps the Walker safe on Mono (where the first object word is a
    // MonoVTable*, not a MonoClass*).
    exports.fnObjectGetClass = (E::t_ObjectGetClass)Resolve("object_get_class");

    // --- Class ---
    exports.fnGetClass          = (E::t_GetClass)Resolve("class_from_name");
    exports.fnClassGetName      = (E::t_ClassGetName)Resolve("class_get_name");
    exports.fnClassGetNamespace = (E::t_ClassGetNamespace)Resolve("class_get_namespace");
    exports.fnClassFromIndex    = (E::t_ClassFromIndex)(isIL2CPP ? Resolve("image_get_class") : Resolve("class_get"));
    exports.fnGetParent         = (E::t_GetParent)Resolve("class_get_parent");
    exports.fnClassIsEnum       = (E::t_ClassIsEnum)Resolve("class_is_enum");
    exports.fnClassEnumBasetype = (E::t_ClassEnumBasetype)Resolve("class_enum_basetype");

    // --- Collection support (shared) ---
    // Used by GetCollectionView to compute element stride. All three exist
    // with identical signatures in IL2CPP and Mono; failures are non-fatal
    // (collection inspection falls back to "<unsupported element type>").
    exports.fnClassGetElementClass = (E::t_ClassGetElementClass)Resolve("class_get_element_class");
    exports.fnClassValueSize       = (E::t_ClassValueSize)Resolve("class_value_size");
    exports.fnClassIsValueType     = (E::t_ClassIsValueType)Resolve("class_is_valuetype");

    // --- Method ---
    exports.fnGetMethod      = (E::t_GetMethod)Resolve("class_get_method_from_name");
    // Shared method flags accessor; the Method Invoker uses this to
    // detect static methods (so it passes nullptr as the instance).
    exports.fnMethodGetFlags = (E::t_MethodGetFlags)Resolve("method_get_flags");

    // --- Field ---
    exports.fnGetFieldFromName = (E::t_FieldFromName)Resolve("class_get_field_from_name");
    exports.fnGetFieldOffset   = (E::t_FieldGetOffset)Resolve("field_get_offset");

    // --- Engine-Specific ---
    bool releaseEngineValid = false;
    bool debugEngineValid = false;

    if (isIL2CPP) {
        exports.fnGetAssemblies      = (E::t_GetAssemblies)Resolve("domain_get_assemblies");
        exports.fnFieldGetStaticAddr = (E::t_FieldGetStaticAddr)Resolve("field_get_static_terminate_data");
        exports.fnRuntimeInvoke      = (E::t_RuntimeInvoke)Resolve("runtime_invoke");
        exports.fnClassGetType       = (E::t_ClassGetType)Resolve("class_get_type");
        exports.fnTypeGetObject      = (E::t_TypeGetObject)Resolve("type_get_object");
        // Method Invoker support: per-param Type pointer + managed string
        // allocation. Best-effort; missing exports just disable certain
        // arg shapes in the UI.
        exports.fnIl2cppMethodGetParam = (E::t_Il2CppMethodGetParam)Resolve("method_get_param");
        exports.fnIl2cppStringNew      = (E::t_Il2CppStringNew)Resolve("string_new");
        exports.fnClassFromType        = (E::t_ClassFromType)Resolve("class_from_il2cpp_type");
        if (!exports.fnFieldGetStaticAddr) {
            printf("[*] Fallback: Trying alternative export for field_get_static_terminate_data\n");
            exports.fnFieldGetStaticAddr = (E::t_FieldGetStaticAddr)Resolve("field_static_get_value");
        }

        releaseEngineValid = (exports.fnGetAssemblies != nullptr);
        debugEngineValid   = (exports.fnGetAssemblies != nullptr) && (exports.fnRuntimeInvoke != nullptr)
                          && (exports.fnClassGetType != nullptr) && (exports.fnTypeGetObject != nullptr);
    }
    else {
        exports.fnCompileMethod     = (E::t_CompileMethod)Resolve("compile_method");
        exports.fnGetVTable         = (E::t_ClassGetVTable)Resolve("class_vtable");
        exports.fnRuntimeInvoke     = (E::t_RuntimeInvoke)Resolve("runtime_invoke");
        exports.fnAssemblyForeach   = (E::t_AssemblyForeach)Resolve("assembly_foreach");
        exports.fnMonoAssemblyOpen  = (E::t_MonoAssemblyOpen)Resolve("domain_assembly_open");
        exports.fnClassGetType      = (E::t_ClassGetType)Resolve("class_get_type");
        exports.fnMonoTypeGetObject = (E::t_MonoTypeGetObject)Resolve("type_get_object");
        // Mono lacks a direct method_get_param_count export (Unity's
        // mono-2.0-bdwgc.dll only ships the signature accessors); resolve
        // the pair so the dumper can reconstruct the param count.
        exports.fnMonoMethodSignature        = (E::t_MonoMethodSignature)Resolve("method_signature");
        exports.fnMonoSignatureGetParamCount = (E::t_MonoSigGetParamCount)Resolve("signature_get_param_count");
        // Method Invoker support on Mono: iterate the signature's params
        // for per-arg type names, and allocate managed strings for string
        // arguments. mono_string_new takes the active domain.
        exports.fnMonoSignatureGetParams = (E::t_MonoSigGetParams)Resolve("signature_get_params");
        exports.fnMonoStringNew          = (E::t_MonoStringNew)Resolve("string_new");
        exports.fnClassFromType          = (E::t_ClassFromType)Resolve("class_from_mono_type");

        releaseEngineValid = (exports.fnCompileMethod != nullptr)
                          && (exports.fnAssemblyForeach != nullptr || exports.fnMonoAssemblyOpen != nullptr);
        debugEngineValid   = (exports.fnAssemblyForeach != nullptr) && (exports.fnClassGetType != nullptr)
                          && (exports.fnMonoTypeGetObject != nullptr);
    }

    const bool coreValid = (exports.fnGetDomain && exports.fnThreadAttach && exports.fnGetImage
                         && exports.fnGetImageName && exports.fnGetClass && exports.fnGetMethod
                         && exports.fnGetParent && exports.fnGetFieldFromName && exports.fnGetFieldOffset);
    const bool releaseValid = coreValid && releaseEngineValid;

    if (!releaseValid) {
        printf("[-] Failed to resolve release-required Unity exports.\n");
        return false;
    }

#ifdef ENABLE_DUMPER
    if (!(coreValid && debugEngineValid)) {
        printf("[-] Failed to resolve debug-required Unity exports.\n");
        return false;
    }
#endif

    return true;
}

bool UnityModule::ResolveDomain() {
    int attempts = 0;
    while (attempts < 100) {
        __try {
            domain = exports.fnGetDomain();
            if (domain) break;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { domain = nullptr; }
        Sleep(100);
        attempts++;
    }
    printf("[*] Domain retrieval %s after %d attempts.\n", domain ? "succeeded" : "failed", attempts);
    return domain != nullptr;
}

void UnityModule::EnsureThreadAttached() const {
    if (exports.fnThreadAttach && domain) exports.fnThreadAttach(domain);
}
} // namespace Engine
