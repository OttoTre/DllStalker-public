#pragma once

#ifdef ENABLE_DUMPER

#include <vector>

#include "unity_resolver.h"
#include "types/dumper_types.h"

namespace Engine::Dumper
{
class ClassCatalog
{
public:
    explicit ClassCatalog(UnityResolver& resolver);

    std::vector<ImageInfo> GetLoadedImages();
    std::vector<ClassInfo> GetRawClasses(void* image);

private:
    UnityResolver& m_resolver;

    std::vector<ImageInfo> EnumerateLoadedImages() const;
};
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
