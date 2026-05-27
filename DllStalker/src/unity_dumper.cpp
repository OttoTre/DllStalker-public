#include "pch.h"

#ifdef ENABLE_DUMPER

#include "unity_dumper.h"

namespace Engine
{
UnityDumper::UnityDumper(UnityResolver& resolver)
    : m_resolver(resolver)
    , m_identity(resolver)
    , m_classes(resolver)
    , m_methods(resolver)
    , m_fields(resolver)
    , m_collection(resolver, m_identity)
    , m_staticFinder(resolver, m_fields)
    , m_liveFinder(resolver)
    , m_invoker(resolver, m_identity)
    , m_diagnostics(resolver, m_identity)
    , m_console(resolver, m_classes)
{
    if (!m_liveFinder.InitDumperExports()) {
        printf("[!] Warning: Some dumper exports failed to initialize.\n");
    }
}
} // namespace Engine

#endif // ENABLE_DUMPER
