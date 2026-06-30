#pragma once

#ifdef ENABLE_DUMPER

#include <array>
#include <cstddef>
#include <cstdint>

#include "scripting/core/script_context.h"
#include "scripting/core/script_result.h"
#include "scripting/handles/script_handles.h"

namespace Scripting
{
constexpr size_t kDefaultAuditRingCapacity = 1024;

enum class ScriptAuditKind : uint8_t {
    Read = 0,
    Write,
    Invoke,
    SehFault,
    StaleHandle,
    Timeout,
    Cancelled,
};

struct ScriptAuditCounters {
    uint64_t writes = 0;
    uint64_t invokes = 0;
    uint64_t reads = 0;
    uint64_t faults = 0;
    uint64_t staleHandles = 0;
    uint64_t timeouts = 0;
    uint64_t cancellations = 0;
};

struct ScriptAuditRecord {
    uint64_t timestamp = 0;
    ScriptInstanceId scriptInstanceId{};
    ScriptHandle targetHandle = kInvalidScriptHandle;
    uint32_t fieldOffsetOrMethodToken = 0;
    ScriptAuditKind kind = ScriptAuditKind::Read;
    DS_Status status = DS_Status::DS_OK;
    uint64_t payloadValue = 0;
};

struct ScriptAuditSnapshot {
    ScriptAuditCounters counters{};
    ScriptAuditRecord lastRecord{};
    uint64_t firstEventTickMs = 0;
    uint64_t lastEventTickMs = 0;
    size_t recordCount = 0;
    size_t recordCapacity = kDefaultAuditRingCapacity;
};

class IScriptAuditSink {
public:
    virtual ~IScriptAuditSink() = default;
    virtual void RecordAudit(const ScriptAuditRecord& record) noexcept = 0;
};

template <size_t Capacity = kDefaultAuditRingCapacity>
class ScriptAuditRingBuffer {
public:
    void Push(const ScriptAuditRecord& record) noexcept {
        records_[writeIndex_] = record;
        writeIndex_ = (writeIndex_ + 1) % Capacity;
        if (count_ < Capacity) {
            ++count_;
        }
    }

    size_t Count() const noexcept { return count_; }
    size_t CapacityValue() const noexcept { return Capacity; }

private:
    std::array<ScriptAuditRecord, Capacity> records_{};
    size_t writeIndex_ = 0;
    size_t count_ = 0;
};

} // namespace Scripting

#endif // ENABLE_DUMPER
