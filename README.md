# DllStalker

DllStalker is a C++20 internal DLL toolkit for Unity games (IL2CPP and Mono). It combines runtime API resolution, preset-based hook installation and optional debug tooling (GUI + console) for reverse-engineering workflows.

## Core Goals

- Resolve Unity runtime APIs dynamically (`il2cpp_*` / `mono_*`)
- Reduce hardcoded offsets by resolving methods/fields at runtime
- Install hooks through a self-registering preset system
- Provide an optional debug workflow for live inspection and testing

## Main Components

- **UnityResolver (`include/engine`, `src/engine`)**
  - Detects backend (`GameAssembly.dll` / `mono-2.0-bdwgc.dll`)
  - Resolves runtime exports dynamically
  - Exposes image/class/method/field lookup helpers

- **Hook Services (`include/services`, `src/services`)**
  - MinHook installation helpers
  - Preset selection and installation entry point
  - Main-thread dispatch bridge for safe managed invokes (debug tooling path)

- **Presets (`include/presets`, `src/presets`)**
  - Each preset lives in its own `.cpp` and self-registers
  - Installer chooses one preset name and applies all hooks from that preset

- **GUI + Dumper (`include/gui`, `src/gui`, `include/dumper`, `src/dumper`)**
  - ImGui control panel and async state loaders
  - Runtime metadata browsing, field analysis, live watch/plot, and method call logging
  - Intended primarily for debug/research sessions

## Build Notes

`ENABLE_DUMPER` in `DllStalker/include/pch.h` controls debug tooling paths.

### Release Build (production hook payload)
Use **Release** when you want a lean DLL focused on your defined hooks.

- Optimized code generation
- Smaller/faster runtime footprint
- No extra debug tooling path by default
- Best choice for stable, repeatable hook deployment

In short: **Release = hook execution, optimized, minimal overhead**.

### Debug Build (development and investigation)
Use **Debug** when you are developing or validating behavior.

- Easier debugging and iteration
- Optional GUI flow and runtime invoke tooling paths
- Console output enabled for live feedback
- Better visibility into initialization and hook state

In short: **Debug = development visibility + tooling**.

### DebugRelease (recommended for GUI work)
Use **DebugRelease | x64** when developing or smoke-testing the control panel with dumper tooling enabled.

- Optimized like Release, with `ENABLE_DUMPER` paths available (see `include/pch.h`)
- Typical day-to-day build for inspector, dock tabs, and call logger validation

## Binary Placement

Build output: **version.dll**.

### Deploy

1. Copy `version.dll` to the game root folder (same folder as the game `.exe`).
2. Keep the filename exactly `version.dll`.
3. Run the game and verify startup logs.

### Why `version.dll`?

Windows checks the game executable folder first for imported DLLs.
Placing `version.dll` next to the game `.exe` makes the game load DllStalker at startup.

## Debug Console

When the bootstrap thread initializes successfully, DllStalker allocates a console window and writes runtime logs to stdout.

Typical console usage includes:

- Startup status (`Unity.Init`, preset install progress)
- Hook creation/enable results and error codes
- Runtime warnings (missing exports, unresolved targets)
- Live operational logs from active hooks

Why this matters:

- Fast validation that hooks were mounted
- Immediate signal when a method/assembly lookup fails
- Easier triage of Debug vs Release behavior differences

## GUI Capabilities

The Debug GUI is designed for runtime exploration, controlled edits, and safe invocation.

**Layout:** left metadata browser; right workspace (**Methods | Fields | Console**); bottom **Global Utilities Dock** (**Bookmarks → History → Watcher → Logger**). Dock tabs restore navigation or show live tooling; they do not replace the main inspector models.

### Global Utilities Dock

| Tab | Purpose |
| --- | ------- |
| **Bookmarks** | Saved inspector locations; restore with shared navigation validation |
| **History** | Navigation timeline and audit rows (read-only; distinct from call log) |
| **Watcher** | Live field watchlist; inner **Watchlist** / **Charts** for numeric plots |
| **Logger** | Active native call hooks and rolling call log (args-only) |

From **Watcher** or **History**, **Jump** restores the workspace to the linked location.

### Browser and filtering

- **Image Selection**
  - Shows loaded assemblies/images with class counts
  - Filter is **case-sensitive** (`strstr` behavior)
- **Class Browser**
  - Fuzzy filter over class name + namespace
  - Filter is **case-insensitive**
- **Sorting behavior**
  - Data is rendered from runtime snapshots/caches
  - For Mono image enumeration, duplicates are normalized internally (sorted + deduplicated before display)

### Instance discovery (two methods)

In the Fields tab, **Find Instances** supports two discovery modes:

1. **Static discovery** – scans static-instance candidates
2. **Live API** – uses Unity live object lookup APIs

You can switch source mode, run discovery and pick the active instance from the candidates combo.

### Field analysis and watch

On the **Fields** tab (analysis toolbar):

- **Snapshot** baseline → **Show changes** tints rows when values drift (green/red/yellow).
- **Compare two instances** — class-level A vs B from root instance finds; **Current path** mode for drilled paths and collection element index.
- **`[W]`** — add/remove a field on the **Watcher** dock tab (live reads; plottable numerics can open **Charts**).

### Methods tab (Run and Log)

**Run** flow (invoke from GUI):

The **Run** button is available when prerequisites are met:

- main-thread dispatcher hook is active,
- main thread is captured,
- method signature is supported,
- an instance is selected for instance methods.

For zero-argument methods, execution is immediate. For methods with arguments, the invoke modal is used.

**Log** (call logger):

- Toggle **Log** on supported methods (`*` when active) to install a native MinHook detour (cap **16** hooks).
- Lines appear in **Utilities → Logger → Log** as `[HH:MM:SS] Class::Method(arg: val, …)` (register args only; no return suffix).
- Manage hooks on **Logger → Hooks**; **Clear log** on the **Log** subtab only.
- Eligibility matches the invoke path where possible: known signature, primitive/string/pointer params, ≤3 instance args or ≤4 static register args. Mono methods with unknown params stay disabled.

### Field editing

Simple field editing is supported for primitive categories (numeric + boolean).

- Typical workflow: edit value in-place, press **OK**, auto-refresh verifies the write.
- String/reference/container direct overwrite is intentionally restricted in this path.

### Navigation (Breadcrumbs)

You can navigate through object graphs directly from field values:

- **Pointer fields**: drill into referenced object
- **Array/List fields**: open synthesized collection view (vector-like navigation)
- Breadcrumbs let you jump back to any previous level

This enables step-by-step exploration of nested pointers and collections without leaving the current inspector context.

## Quick Workflows

### Workflow 1: Image → Class → Instance → Run method

1. Open **Image Selection** and choose the target image.
2. Use **Class Browser** filter to find your class.
3. In **Fields**, click **Find Instances** and choose source mode (Static discovery / Live API).
4. Pick an instance from the candidates combo.
5. Go to **Methods** and click **Run**:
   - immediate call for 0-arg methods,
   - arg modal for methods with parameters.

### Workflow 2: Edit a primitive field and verify

1. Select image, class and active instance.
2. In **Fields**, edit a primitive value (numeric/bool).
3. Click **OK**.
4. Use **Refresh Fields** (or auto-refresh) to confirm the new value.

### Workflow 3: Drill into pointers/collections and return via breadcrumbs

1. In **Fields**, click a pointer value to open nested object view.
2. Click array/list value to open collection view.
3. Continue drilling as needed.
4. Use **Breadcrumbs** to jump back to any previous level.

### Workflow 4: Watch and plot a numeric field

1. Select image, class, and instance; open **Fields**.
2. Toggle **`[W]`** on a numeric field.
3. Open **Utilities → Watcher** for live values; use **Plot** on plottable rows to switch to **Charts**.

### Workflow 5: Log native method calls

1. Open **Methods** for the target class.
2. Click **Log** on a supported method (disabled rows show a tooltip why).
3. Trigger the method in-game; open **Utilities → Logger → Log** to tail lines.
4. Use **Hooks** to **Remove** hooks or toggle **Log** off on Methods; **Clear log** clears lines only.

## Creating a New Preset (Example)

Preset system is self-registration based: add a new `.cpp` preset file and it becomes available automatically.

### 1) Create file

Create a new file, for example:

- `DllStalker/src/presets/example/example.cpp`

### 2) Implement hooks + install function

Minimal shape:

```cpp
#include "pch.h"
#include "presets/hook_preset.h"
#include "services/hook_installer.h"
#include "unity_resolver.h"

namespace
{
using _TargetMethod = void(__fastcall*)(void* __this);
_TargetMethod oTargetMethod = nullptr;

void __fastcall hkTargetMethod(void* __this) {
    // custom behavior
    oTargetMethod(__this);
}

void Install(void* assemblyImage) {
    HOOK_FUNCTION(
        Engine::Unity.GetMethodAddress(assemblyImage, "SomeClass", "TargetMethod", 0, "SomeNamespace"),
        hkTargetMethod,
        oTargetMethod
    );
}

const Presets::HookPreset kPreset = { "Example", "Assembly-CSharp", &Install };
const bool kRegistered = (Presets::Register(kPreset), true); // <---- Function call
}
```

### 3) Select it in installer

In `DllStalker/src/services/hook_installer.cpp`, set:

- `kSelectedPresetName = "Example";`

### 4) Build and verify

- Build Debug (for logs) or Release (for target payload)
- Inject/load DLL
- Confirm in console that preset was found and installed

## Runtime Flow

1. `DllMain` starts bootstrap thread.
2. `Engine::Unity.Init()` resolves runtime exports.
3. Console is created (for runtime logs).
4. `Hooks::StartHooking()` initializes MinHook and installs selected preset.
5. If debug tooling is enabled, GUI thread starts and uses dumper modules.

## Repository Layout (high level)

```text
DllStalker/
├── DllStalker/
│   ├── include/
│   │   ├── engine/
│   │   ├── dumper/
│   │   ├── gui/
│   │   ├── presets/
│   │   ├── services/
│   │   └── types/
│   ├── src/
│   │   ├── engine/
│   │   ├── dumper/
│   │   ├── gui/
│   │   ├── presets/
│   │   ├── services/
│   │   └── types/
│   └── DllStalker.vcxproj
└── docs/
```

## Troubleshooting

### "Preset not found"

- Check `kSelectedPresetName` in `DllStalker/src/services/hook_installer.cpp`
- Ensure your preset file is compiled and self-registers via `Presets::Register(...)`

### "Failed to find target assembly"

- Verify the preset assembly name (`Assembly-CSharp`, etc.)
- Confirm target runtime is fully initialized before install

### Hook create/enable failed

- Check console output for MinHook status
- Verify method signature/namespace/arg count used in `GetMethodAddress(...)`
- Re-check Debug vs Release behavior for target function layout differences

### Methods tab: Run button disabled

Common causes shown by tooltip/status:

- runtime invoke hook unavailable
- main thread not captured yet
- no active instance for instance method
- unsupported parameter types in current invoke path
- missing method handle/signature

### Class filter works, image filter does not match

This is expected behavior:

- class filter is **case-insensitive**
- image filter is **case-sensitive**

### Logger: Log disabled or no lines

- **Log** disabled: no native address, unknown Mono params, unsupported arg types, or too many register arguments.
- **No lines:** hook not armed, method not called yet, or hook removed; check **Logger → Hooks**.
- **Cap / install errors:** max **16** hooks; duplicate native target or MinHook install failure shows a status toast.

## ⚠️ Important Disclaimer & Legal Notice

**This project is strictly for educational, research and local development debugging purposes.**
### 1. No Anti-Cheat Bypasses
This tool is a standard user-mode utility. It **does not** contain any kernel-mode bypasses, driver exploits, signature cloaking or thread-hiding mechanisms.
* Running or injecting this tool into games protected by modern kernel-level anti-cheats (such as **Easy Anti-Cheat, BattlEye, Ricochet, Vanguard or similar**) **WILL result in an immediate, automated and permanent ban.**
* If you are a game developer testing your own project with this tool, you **must disable your game's anti-cheat modules** in your compilation configuration before attaching it.

### 2. Stability & Memory Safety Disclaimer
Because this tool performs direct memory queries and interacts natively with runtime objects (IL2CPP/Mono) via pointers:
* Invoking methods or modifying unaligned fields can cause immediate process instability, memory access violations or fatal game crashes.
* Use this tool entirely at your own risk. The author(s) are not responsible for lost game data, corrupted saves or account restrictions.

### 3. Fair Use & Purpose
This tool does not modify game binaries on disk, distribute protected assets or facilitate online matchmaking manipulation. It is designed to help developers and security researchers analyze object structures and runtime behavior in controlled, offline or authorized testing environments.
