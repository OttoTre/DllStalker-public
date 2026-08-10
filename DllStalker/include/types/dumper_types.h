#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <cstdint>
#include <string>
#include <vector>

namespace Engine
{
struct ClassInfo {
    std::string name;
    std::string ns;
    void* klassPtr;
};

struct ImageInfo {
    std::string name;
    void* imagePtr = nullptr;
    int classCount = 0;
};

struct MethodParam {
    std::string typeName;   // e.g. "System.Int32", "System.String", "UnityEngine.Vector3"
    bool        isEnum = false;
    void*       enumKlass = nullptr;
    std::string underlyingType{};   // e.g. "System.Int32" when isEnum
};

struct EnumLiteral {
    std::string name;
    int64_t     value = 0;
};

struct MethodInfo {
    std::string name;
    std::string returnType;
    std::string parameters;             // human-readable display string ("3 args (jit)")
    // Native method pointer for display / MinHook: IL2CPP =
    // il2cpp_method_get_pointer (or MethodInfo[0] fallback); Mono =
    // mono_compile_method JIT address. Absolute VA — not a module-relative RVA.
    uintptr_t   address = 0;
    // Method Invoker support. engineHandle is the runtime's MethodInfo* /
    // MonoMethod* pointer — what fnRuntimeInvoke actually wants. Stored
    // separately because `address` is the callable native pointer (display).
    void*                       engineHandle = nullptr;
    std::vector<MethodParam>    paramTypes{};
    bool                        isStatic     = false;
    bool                        paramsKnown  = false;  // false on Mono builds without signature exports
    bool                        jitFailed    = false;  // Mono: mono_compile_method raised SEH (e.g. open generics)
};

struct FieldInfo {
    std::string name;
    std::string type;
    size_t offset = 0;
    bool isStatic = false;
    uintptr_t staticValue = 0;
    uintptr_t valueAddress = 0;
    bool hasValue = false;
    std::string valueDisplay;
    bool        isEnum = false;
    void*       enumKlass = nullptr;
    std::string underlyingType{};
};

// Result of a Method Invoker run. `error` is empty on success; on a managed
// exception it contains "<ExceptionClass>: <message>". `returnDisplay` is
// always populated with a short, GUI-friendly preview of the return value.
struct InvokeResult {
    bool        succeeded     = false;
    std::string error{};
    std::string returnDisplay = "void";
};

} // namespace Engine

#endif // ENABLE_DUMPER
