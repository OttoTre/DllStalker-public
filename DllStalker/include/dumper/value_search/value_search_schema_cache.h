#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <memory>
#include <mutex>
#include <unordered_map>

#include "types/value_search_schema.h"
#include "types/value_search_types.h"

namespace Engine
{
class UnityDumper;
}

namespace Engine::Dumper
{
// True when a schema field can match chips + optional name filter.
// Value needle ignored (needs live reads). Deep admits interiors / Follow /
// ARRAY|LIST; unresolved ptr.* admits the class when Deep is on.
bool ClassSchemaMayMatch(const ValueSearchClassSchema& schema,
                         const ValueSearchParams& params);

// Shared chip/name gate for ClassSchemaMayMatch and scanner name-allow build.
// forNameAllowSet: skip ptr.* sentinel (class-admit only).
bool SchemaFieldMayContribute(const ValueSearchFieldSchema& field,
                              const ValueSearchClassSchema& schema,
                              const ValueSearchParams& params,
                              bool forNameAllowSet);

// Result of GetOrBuild. usable=false → do not apply class-skip (build failed
// or field enum was incomplete); fall through to a full instance walk.
// schema is shared so Clear() / map erase does not destroy in-flight scans.
struct ValueSearchSchemaLookup {
    std::shared_ptr<const ValueSearchClassSchema> schema{};
    bool usable = false;
};

// Per-klass field schema cache. Thread-safe GetOrBuild / Clear.
// Keys are klass pointers. Call Clear() when the assembly/image set changes
// (image select / image refresh / history image bind). Incomplete or thrown
// builds are not cached. No domain-unload hook in-tree yet.
class ValueSearchSchemaCache
{
public:
    ValueSearchSchemaLookup GetOrBuild(UnityDumper& dumper, void* klass);
    void Clear();

private:
    struct BuildResult {
        ValueSearchClassSchema schema{};
        bool ok = false; // false: throw or incomplete enum — do not cache / not usable
    };
    BuildResult Build(UnityDumper& dumper, void* klass) const;

    mutable std::mutex m_mutex;
    std::unordered_map<void*, std::shared_ptr<const ValueSearchClassSchema>> m_byKlass;
};
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
