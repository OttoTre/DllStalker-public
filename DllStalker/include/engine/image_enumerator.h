#pragma once

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

    // Substring match (`strstr`) with retries — intentional for presets that
    // pass short names (e.g. "Assembly-CSharp"). Footgun: "UnityEngine" can
    // match CoreModule first. Prefer FindImageExact for new callers.
    // maxAttempts: how many probe passes (Sleep 1s between). Clamped to >= 1.
    void* FindImage(const char* assemblyName, int maxAttempts = 10);

    // Exact image name match (`strcmp`) with the same retry budget as FindImage.
    // Returns nullptr if no image name equals assemblyName.
    void* FindImageExact(const char* assemblyName, int maxAttempts = 10);

private:
    UnityModule& m_module;
};
} // namespace Engine
