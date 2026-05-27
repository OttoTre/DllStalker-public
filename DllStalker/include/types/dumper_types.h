#pragma once

#include "pch.h"

#ifdef ENABLE_DUMPER

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
};

struct MethodInfo {
    std::string name;
    std::string returnType;
    std::string parameters;             // human-readable display string ("3 args (jit)")
    uintptr_t   address = 0;            // RVA on IL2CPP, JIT-compiled fn pointer on Mono. Display only.
    // Method Invoker support. engineHandle is the runtime's MethodInfo* /
    // MonoMethod* pointer — what fnRuntimeInvoke actually wants. Stored
    // separately because `address` carries display data (RVA / JIT addr).
    void*                       engineHandle = nullptr;
    std::vector<MethodParam>    paramTypes{};
    bool                        isStatic     = false;
    bool                        paramsKnown  = false;  // false on Mono builds without signature exports
};

struct FieldInfo {
    std::string name;
    std::string type;
    size_t offset = 0;
    uintptr_t staticValue = 0;
    uintptr_t valueAddress = 0;
    bool hasValue = false;
    std::string valueDisplay;
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
