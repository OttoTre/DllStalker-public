#include "pch.h"

#include "presets/hook_preset.h"

#include "services/bootstrap_log.h"
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
    // "-" (and empty/null) is the intentional "no hooks" selection used by
    // hook_installer — not a missing preset.
    if (!name || name[0] == '\0' || std::strcmp(name, "-") == 0) {
        Engine::Services::BootstrapLog::Write(
            "[*] No preset selected; skipping hooks.\n");
        return;
    }

    const HookPreset* preset = Find(name);
    if (!preset) {
        Engine::Services::BootstrapLog::Write(
            "[!] Preset not found: %s\n", name);
        return;
    }

    void* image = Engine::Unity.FindImage(preset->targetAssembly ? preset->targetAssembly : "");
    if (!image) {
        Engine::Services::BootstrapLog::Write(
            "[!] Failed to find target assembly for preset '%s': %s\n",
            preset->name,
            preset->targetAssembly ? preset->targetAssembly : "<null>");
        return;
    }

    Engine::Services::BootstrapLog::Write(
        "[*] Installing preset: %s (assembly=%s)\n",
        preset->name,
        preset->targetAssembly);
    if (preset->install) {
        preset->install(image);
    }
}
} // namespace Presets
