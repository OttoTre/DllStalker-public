#include "pch.h"

#include "engine/runtime_invoke.h"

namespace Engine
{
namespace
{
// SEH-only helper. C++ try/catch under MSVC's default /EHsc does not catch
// access violations; the engine's "wrong thread / freed object / corrupted
// vtable" failures all manifest as AVs. Wrapping fnRuntimeInvoke in a
// noinline frame with __try/__except keeps the host process alive when
// the engine asserts; the caller then surfaces it as a clean "crashed"
// result instead of taking down the whole DLL.
//
// SEH and C++ unwinding can't share the same function -- this is its own
// noinline TU-local frame so the compiler can't merge it with the
// invoking code.
__declspec(noinline) bool RuntimeInvokeSehFrame(UnityExports::t_RuntimeInvoke fn,
                                                void* method,
                                                void* instance,
                                                void** args,
                                                void** outException,
                                                void*& outReturn) noexcept {
    __try {
        outReturn = fn(method, instance, args, outException);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        outReturn = nullptr;
        return false;
    }
}
} // namespace

RuntimeInvoker::RuntimeInvoker(const UnityModule& module)
    : m_module(module)
{
}

bool RuntimeInvoker::InvokeWithSEH(void* method, void* instance, void** args,
                                   void** outException, void*& outReturn) const noexcept {
    auto fn = m_module.exports.fnRuntimeInvoke;
    if (!fn) {
        outReturn = nullptr;
        if (outException) *outException = nullptr;
        return false;
    }
    return RuntimeInvokeSehFrame(fn, method, instance, args, outException, outReturn);
}

UnityExports::t_RuntimeInvoke RuntimeInvoker::Raw() const {
    return m_module.exports.fnRuntimeInvoke;
}
} // namespace Engine
