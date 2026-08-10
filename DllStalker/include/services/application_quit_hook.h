#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

namespace Engine::Services::ApplicationQuitHook
{
// GUI registers a notify fn (no gui/ includes here). Cleared on panel teardown.
using QuitNotifyFn = void (*)();

void SetQuitNotify(QuitNotifyFn fn);

// Resolve UnityEngine.Application::Quit + MinHook install. Idempotent; non-fatal on failure.
bool Install();
} // namespace Engine::Services::ApplicationQuitHook

#endif // ENABLE_DUMPER
