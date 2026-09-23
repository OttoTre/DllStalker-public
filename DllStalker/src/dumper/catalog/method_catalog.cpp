#include "pch.h"

#ifdef ENABLE_DUMPER

#include "dumper/catalog/method_catalog.h"

#include <cstdio>
#include <string>

#include "services/bootstrap_log.h"
#include "types/memory_guard.h"

namespace Engine::Dumper
{
namespace
{
// SEH-only helpers. C++ try/catch under MSVC's default /EHsc does not catch
// access violations, and certain Unity-Mono targets (notably games shipping
// custom Game.dll metadata) AV inside mono_compile_method or even inside
// mono_class_get_methods for a small subset of classes (GameManager-style
// managers etc.). Each helper is its own __declspec(noinline) frame because
// SEH and C++ unwinding cannot share a function -- this lets GetRawMethods
// keep its std::string / std::vector locals while still containing the
// engine fault, log diagnostics, and continue with safe defaults instead of
// taking down the entire game process.
using GetMethodsFn = void* (__cdecl*)(void* klass, void** iter);
using CompileFn    = void* (__cdecl*)(void* method);

__declspec(noinline) bool SafeGetMethodsStep(GetMethodsFn fn, void* klass, void** iter,
                                              void*& outMethod,
                                              unsigned long& outSehCode) noexcept {
    __try {
        outMethod = fn(klass, iter);
        return true;
    }
    __except (outSehCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
        outMethod = nullptr;
        return false;
    }
}

__declspec(noinline) bool SafeMonoCompileMethod(CompileFn fn, void* method,
                                                uintptr_t& outAddr,
                                                unsigned long& outSehCode) noexcept {
    __try {
        outAddr = reinterpret_cast<uintptr_t>(fn(method));
        return true;
    }
    __except (outSehCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
        outAddr = 0;
        return false;
    }
}

void EnrichTypeEnumMetadata(const UnityModule& module, void* type,
                            bool& isEnum, void*& enumKlass, std::string& underlyingType) {
    const auto& exp = module.exports;
    if (!type || !exp.fnClassFromType || !exp.fnClassIsEnum || !exp.fnClassEnumBasetype) {
        return;
    }
    void* klass = exp.fnClassFromType(type);
    if (!klass || !exp.fnClassIsEnum(klass)) {
        return;
    }
    isEnum    = true;
    enumKlass = klass;
    if (void* baseType = exp.fnClassEnumBasetype(klass)) {
        if (exp.fnTypeGetName) {
            if (const char* baseName = exp.fnTypeGetName(baseType)) {
                if (baseName[0] != '\0') {
                    underlyingType = baseName;
                }
            }
        }
    }
}

MethodParam MakeMethodParam(const UnityModule& module, void* paramType) {
    MethodParam param{};
    const auto& exp = module.exports;
    if (paramType && exp.fnTypeGetName) {
        if (const char* typeName = exp.fnTypeGetName(paramType)) {
            if (typeName[0] != '\0') {
                param.typeName = typeName;
            }
        }
    }
    if (param.typeName.empty()) {
        param.typeName = "Unknown";
    }
    EnrichTypeEnumMetadata(module, paramType, param.isEnum, param.enumKlass, param.underlyingType);
    return param;
}

void ResolveReturnType(const UnityModule& module, void* method, void* monoSig, MethodInfo& info) {
    const auto& exp = module.exports;
    info.returnType = "Unknown";
    if (!exp.fnTypeGetName) {
        return;
    }

    void* returnType = nullptr;
    if (module.isIL2CPP) {
        if (exp.fnIl2cppMethodGetReturnType) {
            returnType = exp.fnIl2cppMethodGetReturnType(method);
        }
    }
    else if (exp.fnMonoSignatureGetReturnType) {
        void* sig = monoSig;
        if (!sig && exp.fnMonoMethodSignature) {
            sig = exp.fnMonoMethodSignature(method);
        }
        if (sig) {
            returnType = exp.fnMonoSignatureGetReturnType(sig);
        }
    }

    if (!returnType) {
        return;
    }
    if (const char* typeName = exp.fnTypeGetName(returnType)) {
        if (typeName[0] != '\0') {
            info.returnType = typeName;
        }
    }
    EnrichTypeEnumMetadata(module, returnType,
                           info.returnIsEnum, info.returnEnumKlass, info.returnUnderlyingType);
}
} // namespace

MethodCatalog::MethodCatalog(UnityResolver& resolver)
    : m_resolver(resolver)
{
}

std::vector<MethodInfo> MethodCatalog::GetRawMethods(void* klass) {
    std::vector<MethodInfo> methods;
    if (!klass || !m_resolver.module.exports.fnGetMethods || !m_resolver.module.exports.fnMethodGetName)
        return methods;

    m_resolver.module.EnsureThreadAttached();
    void* iter   = nullptr;
    void* method = nullptr;

    // METHOD_ATTRIBUTE_STATIC bit, identical between ECMA-335 and both
    // engines' MethodAttributes encoding.
    constexpr uint32_t kMethodAttrStatic = 0x0010;

    // Cached once before the loop so the diagnostic logger has a stable
    // class label even if iteration faults later. class_get_name was already
    // exercised by ClassCatalog when this klass was put on screen.
    const char* klassName = m_resolver.module.exports.fnClassGetName
        ? m_resolver.module.exports.fnClassGetName(klass) : nullptr;
    const char* enginePrefix = m_resolver.module.isIL2CPP ? "il2cpp" : "mono";

    while (true) {
        unsigned long iterSeh = 0;
        if (!SafeGetMethodsStep(m_resolver.module.exports.fnGetMethods, klass, &iter, method, iterSeh)) {
            Engine::Services::BootstrapLog::Write(
                "[!] %s_class_get_methods SEH 0x%08lX on %s -- stopped after %zu method(s)\n",
                   enginePrefix, iterSeh,
                   klassName ? klassName : "?",
                   methods.size());
            break;
        }
        if (!method) break;

        const char* name   = m_resolver.module.exports.fnMethodGetName(method);
        uintptr_t   addr   = 0;
        std::string params = "unknown";

        MethodInfo info{};
        info.engineHandle = method;
        void* monoSigForReturn = nullptr;

        if (m_resolver.module.isIL2CPP) {
            if (m_resolver.module.exports.fnIl2cppMethodGetPointer) {
                addr = reinterpret_cast<uintptr_t>(
                    m_resolver.module.exports.fnIl2cppMethodGetPointer(method));
            }
            else if (!Memory::TryReadValue(reinterpret_cast<uintptr_t>(method), addr)) {
                // Fallback: MethodInfo.methodPointer at offset 0 failed.
                addr = 0;
            }
            if (m_resolver.module.exports.fnMethodGetParamCount) {
                const int count = m_resolver.module.exports.fnMethodGetParamCount(method);
                params = std::to_string(count) + " args";
                info.paramsKnown = true;

                if (count > 0 && m_resolver.module.exports.fnIl2cppMethodGetParam) {
                    info.paramTypes.reserve(count);
                    for (int i = 0; i < count; ++i) {
                        void* paramType = m_resolver.module.exports.fnIl2cppMethodGetParam(method, static_cast<uint32_t>(i));
                        info.paramTypes.push_back(MakeMethodParam(m_resolver.module, paramType));
                    }
                }
            }
        }
        else if (m_resolver.module.exports.fnCompileMethod) {
            unsigned long compileSeh = 0;
            const bool compiled = SafeMonoCompileMethod(
                m_resolver.module.exports.fnCompileMethod, method, addr, compileSeh);
            if (!compiled) {
                Engine::Services::BootstrapLog::Write(
                    "[!] mono_compile_method SEH 0x%08lX: %s.%s\n",
                       compileSeh,
                       klassName ? klassName : "?",
                       name ? name : "?");
                addr = 0;
                params = "jit (crashed)";
                info.jitFailed = true;
            }
            else {
                // Mono: no direct param-count export. Use signature accessors when
                // available; otherwise show a generic "jit" label.
                params = "jit";
                if (m_resolver.module.exports.fnMonoMethodSignature && m_resolver.module.exports.fnMonoSignatureGetParamCount) {
                    if (void* sig = m_resolver.module.exports.fnMonoMethodSignature(method)) {
                        monoSigForReturn = sig;
                        const uint32_t count = m_resolver.module.exports.fnMonoSignatureGetParamCount(sig);
                        params = std::to_string(count) + " args (jit)";
                        info.paramsKnown = true;

                        if (count > 0 && m_resolver.module.exports.fnMonoSignatureGetParams) {
                            info.paramTypes.reserve(count);
                            void* paramIter = nullptr;
                            while (void* paramType = m_resolver.module.exports.fnMonoSignatureGetParams(sig, &paramIter)) {
                                info.paramTypes.push_back(MakeMethodParam(m_resolver.module, paramType));
                                if (info.paramTypes.size() >= count) break;
                            }
                        }
                    }
                }
            }
        }

        if (m_resolver.module.exports.fnMethodGetFlags) {
            uint32_t implFlags = 0;
            const uint32_t flags = m_resolver.module.exports.fnMethodGetFlags(method, &implFlags);
            info.isStatic = (flags & kMethodAttrStatic) != 0;
        }

        info.name       = name ? name : "UNKNOWN_METHOD";
        ResolveReturnType(m_resolver.module, method, monoSigForReturn, info);
        info.parameters = std::move(params);
        info.address    = addr; // absolute native VA / Mono JIT ptr (not RVA)
        methods.push_back(std::move(info));
    }

    return methods;
}

std::vector<MethodNameRow> MethodCatalog::EnumerateMethodNames(void* klass, size_t maxPerClass,
                                                               bool* truncated) {
    // Names-only index. Must not call GetRawMethods, fnCompileMethod,
    // SafeMonoCompileMethod, fnIl2cppMethodGetPointer, or param/return
    // type_get_name enrichment — Mono JIT lives only on the inspector
    // catalog path (GetRawMethods).
    if (truncated) {
        *truncated = false;
    }

    std::vector<MethodNameRow> rows;
    if (!klass || !m_resolver.module.exports.fnGetMethods || !m_resolver.module.exports.fnMethodGetName) {
        return rows;
    }

    m_resolver.module.EnsureThreadAttached();
    void* iter   = nullptr;
    void* method = nullptr;

    constexpr uint32_t kMethodAttrStatic = 0x0010;

    const char* klassName = m_resolver.module.exports.fnClassGetName
        ? m_resolver.module.exports.fnClassGetName(klass) : nullptr;
    const char* enginePrefix = m_resolver.module.isIL2CPP ? "il2cpp" : "mono";

    while (true) {
        if (maxPerClass > 0 && rows.size() >= maxPerClass) {
            if (truncated) {
                *truncated = true;
            }
            break;
        }

        unsigned long iterSeh = 0;
        if (!SafeGetMethodsStep(m_resolver.module.exports.fnGetMethods, klass, &iter, method, iterSeh)) {
            Engine::Services::BootstrapLog::Write(
                "[!] %s_class_get_methods SEH 0x%08lX on %s -- stopped after %zu method(s)\n",
                   enginePrefix, iterSeh,
                   klassName ? klassName : "?",
                   rows.size());
            break;
        }
        if (!method) {
            break;
        }

        MethodNameRow row{};
        const char* name = m_resolver.module.exports.fnMethodGetName(method);
        row.name = name ? name : "UNKNOWN_METHOD";

        if (m_resolver.module.exports.fnMethodGetFlags) {
            uint32_t implFlags = 0;
            const uint32_t flags = m_resolver.module.exports.fnMethodGetFlags(method, &implFlags);
            row.isStatic = (flags & kMethodAttrStatic) != 0;
        }

        if (m_resolver.module.isIL2CPP) {
            if (m_resolver.module.exports.fnMethodGetParamCount) {
                row.paramCount = m_resolver.module.exports.fnMethodGetParamCount(method);
                row.paramCountKnown = true;
            }
        }
        else if (m_resolver.module.exports.fnMonoMethodSignature
                 && m_resolver.module.exports.fnMonoSignatureGetParamCount) {
            if (void* sig = m_resolver.module.exports.fnMonoMethodSignature(method)) {
                row.paramCount = static_cast<int>(
                    m_resolver.module.exports.fnMonoSignatureGetParamCount(sig));
                row.paramCountKnown = true;
            }
        }

        rows.push_back(std::move(row));
    }

    return rows;
}
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
