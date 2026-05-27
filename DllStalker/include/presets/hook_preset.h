#pragma once

namespace Presets
{
// A "preset" packages one game-specific hook bundle. Each translation unit
// that defines a preset registers it from a static initializer using
// `Register`. The `services/hook_installer` then asks the registry to
// install the chosen preset by name. The struct is intentionally a POD of
// function pointers + literals so it stays trivially constexpr-friendly
// and keeps the link graph from `services/hook_installer` to the preset
// implementations cut at runtime (registry lookup) instead of at compile
// time (no #include of every preset header).
struct HookPreset {
    const char* name;
    const char* targetAssembly;
    void      (*install)(void* assemblyImage);
};

void Register(const HookPreset& preset);
const HookPreset* Find(const char* name);
void InstallSelected(const char* name);
} // namespace Presets
