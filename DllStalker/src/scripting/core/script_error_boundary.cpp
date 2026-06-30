#include "pch.h"

#ifdef ENABLE_DUMPER

#include "scripting/core/script_error_boundary.h"

namespace Scripting
{
DS_Status RunSehProtected(SehProtectedStatusFn fn, void* context, unsigned long& outSehCode) noexcept {
    if (!fn) {
        outSehCode = 0;
        return DS_Status::DS_ERR_BAD_ARGUMENT;
    }

    __try {
        return fn(context);
    }
    __except (outSehCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
        return DS_Status::DS_ERR_INVALID_POINTER;
    }
}
} // namespace Scripting

#endif // ENABLE_DUMPER
