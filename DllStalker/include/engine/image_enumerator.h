#pragma once

#include "pch.h"

#include <functional>

#include "engine/unity_module.h"

// Iterates loaded assemblies / images on either runtime and looks them up
// by name. The iteration shape is unified across IL2CPP (domain_get_assemblies
// + image_get_image) and Mono (assembly_foreach trampoline) so callers don't
// have to branch on `module.isIL2CPP`.
namespace Engine
{
class ImageEnumerator
{
public:
    // Visitor returns false to stop iteration, true to continue.
    using ImageVisitor = std::function<bool(void* image, const char* name)>;

    explicit ImageEnumerator(UnityModule& module);

    void ForEachImage(const ImageVisitor& visitor) const;

    // Retries up to ~10 seconds because some assemblies load asynchronously
    // a few frames after process attach. Returns nullptr on timeout.
    void* FindImage(const char* assemblyName);

private:
    UnityModule& m_module;
};
} // namespace Engine
