#include "pch.h"

#ifdef ENABLE_DUMPER

#include "scripting/core/script_result.h"

namespace Scripting
{
const char* StatusToString(DS_Status status) noexcept {
    switch (status) 
    {
    case DS_Status::DS_OK: return "DS_OK";
    case DS_Status::DS_ERR_STARVATION: return "DS_ERR_STARVATION";
    case DS_Status::DS_ERR_INVALID_POINTER: return "DS_ERR_INVALID_POINTER";
    case DS_Status::DS_ERR_CANCELLED: return "DS_ERR_CANCELLED";
    case DS_Status::DS_ERR_QUEUE_FULL: return "DS_ERR_QUEUE_FULL";
    case DS_Status::DS_ERR_BUFFER_TOO_SMALL: return "DS_ERR_BUFFER_TOO_SMALL";
    case DS_Status::DS_ERR_BAD_ARGUMENT: return "DS_ERR_BAD_ARGUMENT";
    case DS_Status::DS_ERR_TYPE_MISMATCH: return "DS_ERR_TYPE_MISMATCH";
    case DS_Status::DS_ERR_STALE_OBJECT: return "DS_ERR_STALE_OBJECT";
    case DS_Status::DS_ERR_HANDLE_INVALID: return "DS_ERR_HANDLE_INVALID";
    case DS_Status::DS_ERR_HANDLE_KIND_MISMATCH: return "DS_ERR_HANDLE_KIND_MISMATCH";
    case DS_Status::DS_ERR_MAIN_THREAD_NOT_CAPTURED: return "DS_ERR_MAIN_THREAD_NOT_CAPTURED";
    case DS_Status::DS_ERR_DISPATCHER_UNAVAILABLE: return "DS_ERR_DISPATCHER_UNAVAILABLE";
    case DS_Status::DS_ERR_TIMEOUT: return "DS_ERR_TIMEOUT";
    case DS_Status::DS_ERR_UNSUPPORTED_ABI_VERSION: return "DS_ERR_UNSUPPORTED_ABI_VERSION";
    case DS_Status::DS_ERR_METHOD_NOT_FOUND: return "DS_ERR_METHOD_NOT_FOUND";
    case DS_Status::DS_ERR_ARG_COUNT_MISMATCH: return "DS_ERR_ARG_COUNT_MISMATCH";
    case DS_Status::DS_ERR_ARG_TYPE_MISMATCH: return "DS_ERR_ARG_TYPE_MISMATCH";
    case DS_Status::DS_ERR_UNSUPPORTED_TYPE: return "DS_ERR_UNSUPPORTED_TYPE";
    case DS_Status::DS_ERR_MANAGED_EXCEPTION: return "DS_ERR_MANAGED_EXCEPTION";
    case DS_Status::DS_ERR_STRING_TOO_LONG: return "DS_ERR_STRING_TOO_LONG";
    default: return "DS_ERR_UNKNOWN";
    }
}
} // namespace Scripting

#endif // ENABLE_DUMPER
