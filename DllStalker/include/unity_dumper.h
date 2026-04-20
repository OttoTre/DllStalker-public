#pragma once

#ifdef ENABLE_DUMPER 

#include "unity_resolver.h"

namespace Engine
{

class UnityDumper
{
public:
    explicit UnityDumper(UnityResolver& resolver);
private:
    UnityResolver& m_resolver;  // Reference to core resolver for shared access

    bool InitDumperExports();

public:
    // --- Dumping API ---
    void DumpClasses(void* image, const char* fileName = nullptr);
    void DumpMethods(void* image, const char* className, const char* ns = GLOBAL_NAMESPACE);
    void DumpMethods(void* klass);
    void DumpFields(void* image, const char* className, const char* ns = GLOBAL_NAMESPACE);
    void DumpFields(void* klass);

    void IdentifyObject(void* instance);
    void SmartSearchAndReplace(void* instance, int searchValue, int newValue);

private:
    // --- Dumper Typedefs ---
    // Image (class enumeration)
    typedef int   (__cdecl* t_ImageGetClassCount)(void* image);           // IL2CPP
    typedef void* (__cdecl* t_ImageGetTableInfo)(void* image, int table_id); // Mono
    typedef int   (__cdecl* t_TableInfoGetRows)(void* table);             // Mono
    // Class
    typedef void*        (__cdecl* t_ClassGetMethods)(void* klass, void** iter);
    typedef void*        (__cdecl* t_ClassGetFields)(void* klass, void** iter);
    typedef int32_t      (__cdecl* t_ClassGetInstanceSize)(void* klass);
    typedef void*        (__cdecl* t_GetStaticFieldsPtr)(void* klass);    // IL2CPP
    // Method
    typedef const char*  (__cdecl* t_MethodGetName)(void* method);
    typedef int          (__cdecl* t_MethodGetParamCount)(void* method);  // IL2CPP
    // Field
    typedef const char*  (__cdecl* t_FieldGetName)(void* field);

    // --- Dumper Function Pointers ---
    // Image (engine-specific)
    t_ImageGetClassCount    fnImageGetClassCount        = nullptr; // IL2CPP
    t_ImageGetTableInfo     fnImageGetTableInfo         = nullptr; // Mono
    t_TableInfoGetRows      fnTableInfoGetRows          = nullptr; // Mono
    // Class
    t_ClassGetMethods       fnGetMethods                = nullptr;
    t_ClassGetFields        fnClassGetFields            = nullptr;
    t_ClassGetInstanceSize  fnGetSize                   = nullptr;
    t_GetStaticFieldsPtr    fnClassGetStaticFieldsPtr   = nullptr; // IL2CPP
    // Method
    t_MethodGetName         fnMethodGetName             = nullptr;
    t_MethodGetParamCount   fnMethodGetParamCount       = nullptr; // IL2CPP
    // Field
    t_FieldGetName          fnFieldGetName              = nullptr;

    int GetObjectSize(void* instance) const;
};

} // namespace Engine

#endif // ENABLE_DUMPER
