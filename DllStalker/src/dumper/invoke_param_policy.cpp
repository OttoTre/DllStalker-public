#include "pch.h"

#ifdef ENABLE_DUMPER

#include "dumper/invoke_param_policy.h"

#include "types/type_classifier.h"

namespace Engine::Dumper
{
InvokeParamSupport ClassifyInvokeParam(const MethodParam& param) {
    if (param.isEnum && !param.underlyingType.empty()) {
        return InvokeParamSupport::Enum;
    }

    using Cat = Types::TypeCategory;
    switch (Types::GetCategory(param.typeName)) {
    case Cat::I1:
    case Cat::I2:
    case Cat::I4:
    case Cat::I8:
    case Cat::U1:
    case Cat::U2:
    case Cat::U4:
    case Cat::U8:
    case Cat::R4:
    case Cat::R8:
    case Cat::BOOLEAN:
    case Cat::STRING:
        return InvokeParamSupport::Primitive;
    case Cat::PTR:
        return InvokeParamSupport::Reference;
    default:
        return InvokeParamSupport::Unsupported;
    }
}

bool MethodIsInvokable(const MethodInfo& method) {
    for (const auto& param : method.paramTypes) {
        if (ClassifyInvokeParam(param) == InvokeParamSupport::Unsupported) {
            return false;
        }
    }
    return true;
}
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
