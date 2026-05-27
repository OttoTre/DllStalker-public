#pragma once

#ifdef ENABLE_DUMPER

#include "unity_resolver.h"

namespace Engine::Dumper
{
class ObjectIdentity;

// One-shot diagnostic helpers for ad-hoc inspection of a managed object.
// Not used by the main GUI flow today, but kept around for console/devx
// scenarios. Overall this class is pretty dead code.
class Diagnostics
{
public:
    Diagnostics(UnityResolver& resolver, const ObjectIdentity& identity);

    void IdentifyObject(void* instance);

    // Walks an instance's bytes looking for a 32-bit integer matching
    // `searchValue` and replaces with `newValue`. Skips obvious pointer
    // slots to avoid corrupting headers.
    void SmartSearchAndReplace(void* instance, int searchValue, int newValue);

    static void SafeHexDump(void* instance, size_t size);

private:
    UnityResolver& m_resolver;
    const ObjectIdentity& m_identity;
};
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
