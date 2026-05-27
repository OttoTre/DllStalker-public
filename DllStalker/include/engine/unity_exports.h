#pragma once

#include <cstdint>

// Function-pointer table for the Unity engine's IL2CPP / Mono export surface.
// Plain struct -- every field is publicly readable -- because the dumper, the
// main-thread dispatcher, and the various engine sub-services all reach into
// it directly. Resolution lives in UnityModule::ResolveExports; this header
// is concerned only with the typed shape.
//
// The IL2CPP-specific and Mono-specific entries coexist so the same struct
// works for both runtimes. Fields not applicable to the current runtime
// remain nullptr.
namespace Engine
{
struct UnityExports
{
    // ---- Typedefs ----
    // Domain / Thread
    typedef void*  (__cdecl* t_GetDomain)();
    typedef void*  (__cdecl* t_ThreadAttach)(void* domain);
    typedef void** (__cdecl* t_GetAssemblies)(void* domain, size_t* size);
    // Assembly / Image
    typedef void*        (__cdecl* t_GetImage)(void* assembly);
    typedef const char*  (__cdecl* t_GetImageName)(void* image);
    typedef void         (__cdecl* t_AssemblyForeach)(void* func, void* user_data); // Mono
    typedef void*        (__cdecl* t_MonoAssemblyOpen)(void* domain, const char* name); // Mono
    // Class
    typedef void*        (__cdecl* t_GetClass)(void* image, const char* ns, const char* name);
    typedef void*        (__cdecl* t_GetParent)(void* klass);
    typedef void*        (__cdecl* t_ClassFromIndex)(void* image, uintptr_t index);
    typedef const char*  (__cdecl* t_ClassGetName)(void* klass);
    typedef const char*  (__cdecl* t_ClassGetNamespace)(void* klass);
    typedef void*        (__cdecl* t_ClassGetType)(void* klass);
    // Method
    typedef void*    (__cdecl* t_GetMethod)(void* klass, const char* name, int args);
    typedef void*    (__cdecl* t_CompileMethod)(void* method);
    typedef void*    (__cdecl* t_MonoMethodSignature)(void* method);   // Mono
    typedef uint32_t (__cdecl* t_MonoSigGetParamCount)(void* sig);     // Mono
    typedef void*    (__cdecl* t_MonoSigGetParams)(void* sig, void** iter);   // Mono
    typedef void*    (__cdecl* t_Il2CppMethodGetParam)(void* method, uint32_t index); // IL2CPP
    typedef uint32_t (__cdecl* t_MethodGetFlags)(void* method, uint32_t* outImplFlags); // Shared
    // Managed-string allocation. Different signatures per engine: IL2CPP
    // keeps a process-global default domain so it doesn't take one;
    // mono_string_new takes the active MonoDomain explicitly.
    typedef void*    (__cdecl* t_Il2CppStringNew)(const char* utf8);
    typedef void*    (__cdecl* t_MonoStringNew)(void* domain, const char* utf8);
    // Object
    typedef void*    (__cdecl* t_ObjectGetClass)(void* obj);           // Shared
    // Class element / size / value-type queries (Shared). Used by the
    // collection inspector to compute element stride for both reference
    // arrays (= sizeof(void*)) and value-type arrays (= class_value_size).
    typedef void*    (__cdecl* t_ClassGetElementClass)(void* klass);
    typedef int32_t  (__cdecl* t_ClassValueSize)(void* klass, uint32_t* alignOut);
    typedef int32_t  (__cdecl* t_ClassIsValueType)(void* klass);
    // Field
    typedef void*   (__cdecl* t_FieldFromName)(void* klass, const char* name);
    typedef size_t  (__cdecl* t_FieldGetOffset)(void* field);
    typedef void*   (__cdecl* t_FieldGetStaticAddr)(void* field);
    typedef void*   (__cdecl* t_ClassGetVTable)(void* domain, void* klass);
    typedef void*   (__cdecl* t_RuntimeInvoke)(void* method, void* obj, void** params, void** exc);
    typedef void*   (__cdecl* t_TypeGetObject)(void* type);                 // IL2CPP
    typedef void*   (__cdecl* t_MonoTypeGetObject)(void* domain, void* type); // Mono

    // Dumper-only introspection (image/class iteration, method/field metadata).
    // Resolved by the dumper's own bootstrap path because they are not needed
    // for hook installation or runtime_invoke.
    typedef int   (__cdecl* t_ImageGetClassCount)(void* image);              // IL2CPP
    typedef void* (__cdecl* t_ImageGetTableInfo)(void* image, int table_id); // Mono
    typedef int   (__cdecl* t_TableInfoGetRows)(void* table);                // Mono
    typedef void*        (__cdecl* t_ClassGetMethods)(void* klass, void** iter);
    typedef void*        (__cdecl* t_ClassGetFields)(void* klass, void** iter);
    typedef int32_t      (__cdecl* t_ClassGetInstanceSize)(void* klass);
    typedef void*        (__cdecl* t_GetStaticFieldsPtr)(void* klass);    // IL2CPP
    typedef const char*  (__cdecl* t_MethodGetName)(void* method);
    typedef int          (__cdecl* t_MethodGetParamCount)(void* method);  // IL2CPP
    typedef const char*  (__cdecl* t_FieldGetName)(void* field);
    typedef uint32_t     (__cdecl* t_FieldGetFlags)(void* field);
    typedef void*        (__cdecl* t_FieldGetType)(void* field);
    typedef const char*  (__cdecl* t_TypeGetName)(void* type);

    // ---- Function pointers ----
    // Domain / Thread / Assembly
    t_GetDomain         fnGetDomain     = nullptr;
    t_ThreadAttach      fnThreadAttach  = nullptr;
    t_GetAssemblies     fnGetAssemblies = nullptr; // IL2CPP only
    t_GetImage          fnGetImage      = nullptr;
    t_GetImageName      fnGetImageName  = nullptr;
    // Class
    t_GetClass          fnGetClass          = nullptr;
    t_GetParent         fnGetParent         = nullptr;
    t_ClassFromIndex    fnClassFromIndex    = nullptr;
    t_ClassGetName      fnClassGetName      = nullptr;
    t_ClassGetNamespace fnClassGetNamespace = nullptr;
    // Object: cross-engine "instance -> class". On IL2CPP this returns the
    // Il2CppClass directly; on Mono it dereferences the MonoVTable header
    // internally and returns the MonoClass. Routing through this single
    // export is what keeps the Walker safe across both runtimes.
    t_ObjectGetClass    fnObjectGetClass    = nullptr;
    // Collection support (shared). class_get_element_class returns the
    // element klass for an array klass; class_value_size returns the inline
    // storage size for a value type; class_is_valuetype distinguishes them
    // from reference types. Together they let the dumper compute correct
    // per-element stride without poking engine struct internals.
    t_ClassGetElementClass fnClassGetElementClass = nullptr;
    t_ClassValueSize       fnClassValueSize       = nullptr;
    t_ClassIsValueType     fnClassIsValueType     = nullptr;
    // Method
    t_GetMethod         fnGetMethod      = nullptr;
    t_CompileMethod     fnCompileMethod  = nullptr;
    t_MethodGetFlags    fnMethodGetFlags = nullptr;
    // Field
    t_FieldFromName     fnGetFieldFromName  = nullptr;
    t_FieldGetOffset    fnGetFieldOffset    = nullptr;
    t_FieldGetStaticAddr fnFieldGetStaticAddr = nullptr;
    t_ClassGetVTable    fnGetVTable     = nullptr;
    t_RuntimeInvoke     fnRuntimeInvoke = nullptr;
    t_ClassGetType      fnClassGetType  = nullptr;
    t_TypeGetObject     fnTypeGetObject = nullptr;
    t_MonoTypeGetObject fnMonoTypeGetObject = nullptr;
    // Mono assembly enumeration/open helpers
    t_AssemblyForeach   fnAssemblyForeach  = nullptr;
    t_MonoAssemblyOpen  fnMonoAssemblyOpen = nullptr;
    // Mono method signature accessors. IL2CPP exposes the param count
    // directly via mono_/il2cpp_method_get_param_count, but Unity's
    // mono-2.0-bdwgc.dll does not export that symbol. Resolve the
    // signature pair instead and reconstruct the count on the dumper side.
    t_MonoMethodSignature   fnMonoMethodSignature        = nullptr;
    t_MonoSigGetParamCount  fnMonoSignatureGetParamCount = nullptr;
    // Param-type introspection used by the Method Invoker. Engine-specific
    // because the runtimes hand out the per-param Type pointer differently:
    // IL2CPP exposes a direct index getter on the method; Mono iterates the
    // signature's param list.
    t_Il2CppMethodGetParam  fnIl2cppMethodGetParam   = nullptr;
    t_MonoSigGetParams      fnMonoSignatureGetParams = nullptr;
    // Managed-string allocation (one per engine, identical purpose).
    t_Il2CppStringNew       fnIl2cppStringNew = nullptr;
    t_MonoStringNew         fnMonoStringNew   = nullptr;

    // Dumper-only introspection. Resolved by the dumper bootstrap, kept on
    // the shared exports table so the dumper services don't need a parallel
    // function table of their own.
    t_ImageGetClassCount    fnImageGetClassCount      = nullptr; // IL2CPP
    t_ImageGetTableInfo     fnImageGetTableInfo       = nullptr; // Mono
    t_TableInfoGetRows      fnTableInfoGetRows        = nullptr; // Mono
    t_ClassGetMethods       fnGetMethods              = nullptr;
    t_ClassGetFields        fnClassGetFields          = nullptr;
    t_ClassGetInstanceSize  fnGetSize                 = nullptr;
    t_GetStaticFieldsPtr    fnClassGetStaticFieldsPtr = nullptr; // IL2CPP
    t_MethodGetName         fnMethodGetName           = nullptr;
    t_MethodGetParamCount   fnMethodGetParamCount     = nullptr; // IL2CPP
    t_FieldGetName          fnFieldGetName            = nullptr;
    t_FieldGetFlags         fnFieldGetFlags           = nullptr;
    t_FieldGetType          fnFieldGetType            = nullptr;
    t_TypeGetName           fnTypeGetName             = nullptr;
};
} // namespace Engine
