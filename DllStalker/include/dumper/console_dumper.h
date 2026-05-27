#pragma once

#ifdef ENABLE_DUMPER

#include "unity_resolver.h"

namespace Engine::Dumper
{
class ClassCatalog;

class ConsoleDumper
{
public:
    ConsoleDumper(UnityResolver& resolver, ClassCatalog& classes);

    void DumpClasses(void* image, const char* fileName = nullptr);
    void DumpMethods(void* image, const char* className, const char* ns = GLOBAL_NAMESPACE);
    void DumpMethods(void* klass);
    void DumpFields(void* image, const char* className, const char* ns = GLOBAL_NAMESPACE);
    void DumpFields(void* klass);

private:
    UnityResolver& m_resolver;
    ClassCatalog&  m_classes;
};
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
