#include "pch.h"

#ifdef ENABLE_DUMPER

#include "dumper/method_catalog.h"

#include <string>

namespace Engine::Dumper
{
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

    while ((method = m_resolver.module.exports.fnGetMethods(klass, &iter)) != nullptr) {
        const char* name   = m_resolver.module.exports.fnMethodGetName(method);
        uintptr_t   addr   = 0;
        std::string params = "unknown";

        MethodInfo info{};
        info.engineHandle = method;

        if (m_resolver.module.isIL2CPP) {
            addr = *(uintptr_t*)method;
            if (m_resolver.module.exports.fnMethodGetParamCount) {
                const int count = m_resolver.module.exports.fnMethodGetParamCount(method);
                params = std::to_string(count) + " args";
                info.paramsKnown = true;

                if (count > 0 && m_resolver.module.exports.fnIl2cppMethodGetParam && m_resolver.module.exports.fnTypeGetName) {
                    info.paramTypes.reserve(count);
                    for (int i = 0; i < count; ++i) {
                        void* paramType = m_resolver.module.exports.fnIl2cppMethodGetParam(method, static_cast<uint32_t>(i));
                        const char* typeName = paramType ? m_resolver.module.exports.fnTypeGetName(paramType) : nullptr;
                        info.paramTypes.push_back({ typeName ? std::string(typeName) : std::string("Unknown") });
                    }
                }
            }
        }
        else if (m_resolver.module.exports.fnCompileMethod) {
            addr   = (uintptr_t)m_resolver.module.exports.fnCompileMethod(method);
            // Mono path: no direct method_get_param_count export. When the
            // signature accessor pair resolved we surface a real count;
            // otherwise we fall back to the legacy "jit" placeholder so
            // older targets still render.
            params = "jit";
            if (m_resolver.module.exports.fnMonoMethodSignature && m_resolver.module.exports.fnMonoSignatureGetParamCount) {
                if (void* sig = m_resolver.module.exports.fnMonoMethodSignature(method)) {
                    const uint32_t count = m_resolver.module.exports.fnMonoSignatureGetParamCount(sig);
                    params = std::to_string(count) + " args (jit)";
                    info.paramsKnown = true;

                    if (count > 0 && m_resolver.module.exports.fnMonoSignatureGetParams && m_resolver.module.exports.fnTypeGetName) {
                        info.paramTypes.reserve(count);
                        void* paramIter = nullptr;
                        while (void* paramType = m_resolver.module.exports.fnMonoSignatureGetParams(sig, &paramIter)) {
                            const char* typeName = m_resolver.module.exports.fnTypeGetName(paramType);
                            info.paramTypes.push_back({ typeName ? std::string(typeName) : std::string("Unknown") });
                            if (info.paramTypes.size() >= count) break;
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
        info.returnType = "Unknown";
        info.parameters = std::move(params);
        info.address    = addr;
        methods.push_back(std::move(info));
    }

    return methods;
}
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
