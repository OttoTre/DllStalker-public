#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <cstdint>

namespace Scripting
{
// Canonical status codes for scripting, curated C ABI, and script results.
enum class DS_Status : int32_t {
    DS_OK = 0,
    DS_ERR_STARVATION,
    DS_ERR_INVALID_POINTER,
    DS_ERR_CANCELLED,
    DS_ERR_QUEUE_FULL,
    DS_ERR_BUFFER_TOO_SMALL,
    DS_ERR_BAD_ARGUMENT,
    DS_ERR_TYPE_MISMATCH,
    DS_ERR_STALE_OBJECT,
    DS_ERR_HANDLE_INVALID,
    DS_ERR_HANDLE_KIND_MISMATCH,
    DS_ERR_MAIN_THREAD_NOT_CAPTURED,
    DS_ERR_DISPATCHER_UNAVAILABLE,
    DS_ERR_TIMEOUT,
    DS_ERR_UNSUPPORTED_ABI_VERSION,
    DS_ERR_METHOD_NOT_FOUND,
    DS_ERR_ARG_COUNT_MISMATCH,
    DS_ERR_ARG_TYPE_MISMATCH,
    DS_ERR_UNSUPPORTED_TYPE,
    DS_ERR_MANAGED_EXCEPTION,
    DS_ERR_STRING_TOO_LONG,
};

inline bool IsOk(DS_Status status) noexcept {
    return status == DS_Status::DS_OK;
}

const char* StatusToString(DS_Status status) noexcept;

struct ScriptResult {
    DS_Status status = DS_Status::DS_ERR_BAD_ARGUMENT;

    static ScriptResult Ok() noexcept { return {DS_Status::DS_OK}; }
    static ScriptResult Fail(DS_Status code) noexcept { return {code}; }
};

} // namespace Scripting

#endif // ENABLE_DUMPER
