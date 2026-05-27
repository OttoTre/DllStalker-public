#include "pch.h"

#include "presets/hook_preset.h"

#include "unity_resolver.h"

#include <cstring>
#include <vector>

namespace Presets
{
namespace
{
// The registry vector is created the first time Registry() is called.
// This avoids global initialization order issues across preset .cpp files.
// If a preset registers itself during static initialization, the vector is
// guaranteed to exist at that moment.
std::vector<HookPreset>& Registry() {
    static std::vector<HookPreset> presets;
    return presets;
}
} // namespace

void Register(const HookPreset& preset) {
    Registry().push_back(preset);
}

const HookPreset* Find(const char* name) {
    if (!name) {
        return nullptr;
    }
    for (const auto& preset : Registry()) {
        // Comparison is case-sensitive.
        if (preset.name && std::strcmp(preset.name, name) == 0) {
            return &preset;
        }
    }
    return nullptr;
}

void InstallSelected(const char* name) {
    const HookPreset* preset = Find(name);
    if (!preset) {
        printf("[!] Preset not found: %s\n", name ? name : "<null>");
        return;
    }

    void* image = Engine::Unity.FindImage(preset->targetAssembly ? preset->targetAssembly : "");
    if (!image) {
        printf("[!] Failed to find target assembly for preset '%s': %s\n",
               preset->name,
               preset->targetAssembly ? preset->targetAssembly : "<null>");
        return;
    }

    printf("[*] Installing preset: %s (assembly=%s)\n", preset->name, preset->targetAssembly);
    if (preset->install) {
        preset->install(image);
    }
}
} // namespace Presets
