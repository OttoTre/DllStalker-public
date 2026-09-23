#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include "types/dumper_types.h"

namespace Engine::Dumper
{
enum class InvokeParamSupport {
    Primitive,
    Enum,
    Reference,
    InlineStruct, // Allowlisted Unity value structs (VEC2/3/4, QUAT, COLOR, COLOR32, RECT)
    Unsupported,
};

InvokeParamSupport ClassifyInvokeParam(const MethodParam& param);
bool MethodIsInvokable(const MethodInfo& method);
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
