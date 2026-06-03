#pragma once

#ifdef ENABLE_DUMPER

#include "types/dumper_types.h"

namespace Engine::Dumper
{
enum class InvokeParamSupport {
    Primitive,
    Enum,
    Reference,
    Unsupported,
};

InvokeParamSupport ClassifyInvokeParam(const MethodParam& param);
bool MethodIsInvokable(const MethodInfo& method);
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
