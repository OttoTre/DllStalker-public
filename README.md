# DllStalker

DllStalker is a C++20 internal DLL toolkit for Unity games (IL2CPP and Mono). It combines runtime API resolution, preset-based hook installation and optional debug tooling (control-panel GUI + bootstrap diagnostics) for reverse-engineering workflows.

![Gif snippet](DllStalker/docs/Snippet.gif)

## Core Goals

- Resolve Unity runtime APIs dynamically (`il2cpp_*` / `mono_*`)
- Reduce hardcoded offsets by resolving methods/fields at runtime
- Install hooks through a self-registering preset system
- Provide an optional debug workflow for live inspection, scripting, and testing

## Documentation

| Audience | Start here |
|----------|------------|
| **Scripting / modding** | [`docs/examples/Scripting-Guide.md`](docs/examples/Scripting-Guide.md) — Lua API; copy tutorial packages from [`docs/examples/packages/`](docs/examples/packages/) |
| **Manifest fields** | [`docs/examples/Manifest-Schema.md`](docs/examples/Manifest-Schema.md) |

Tutorial packages in this repo are reference copies only. **Copy** a package folder into `stalker_runtime/mods` next to the deployed `version.dll` (same directory as the game `.exe`). DllStalker does not load scripts from `docs/examples/` in the repository.

For basic Safe mods, `manifest.json` is **optional**:

- **Loose script** — any `*.lua` file placed **directly** under `mods/` (e.g. `mods/heal.lua`) is discovered by filename; no manifest needed.
- **Folder package** — a subfolder under `mods/` without `manifest.json` must use **`main.lua`** as the entry file. For another name (e.g. `watcher.lua` inside a folder), add `manifest.json` with `"entry_file"`.

Add `manifest.json` when you need a display name, Curated profile, a non-`main.lua` entry inside a folder, or non-default timing budgets — see [`Manifest-Schema.md`](docs/examples/Manifest-Schema.md).

## Main Components

- **UnityResolver** (`include/unity_resolver.h`, `src/unity_resolver.cpp` → `include/engine/`, `src/engine/`)
  - Detects backend (`GameAssembly.dll` / `mono-2.0-bdwgc.dll`)
  - Resolves runtime exports dynamically
  - Exposes image/class/method/field lookup helpers

- **UnityDumper** (`include/unity_dumper.h`, `src/unity_dumper.cpp` → `include/dumper/`, `src/dumper/`)
  - Catalogs, field/method invoke, collection views, SDK export (`ENABLE_DUMPER` only)

- **Hook Services** (`include/services/`, `src/services/`)
  - MinHook installation helpers (`Hooks::`)
  - Preset selection and installation entry point
  - Main-thread dispatch bridge for safe managed invokes (debug tooling path)

- **Types** (`include/types/`, `src/types/`)
  - POD structs, `memory_guard`, value decode/write, type classification

- **Scripting** (`include/scripting/`, `src/scripting/`)
  - LuaJIT runtime bridge for Safe and Curated script packages under `stalker_runtime/mods`
  - Exposes `ds.*`, host-owned ticks/reload, opaque script handles and a flat Curated `DS_*` ABI

- **Presets** (`include/presets/`, `src/presets/`)
  - Each preset lives in its own `.cpp` and self-registers
  - Installer chooses one preset name and applies all hooks from that preset

- **GUI** (`include/gui/`, `src/gui/`)
  - Win32 + D3D11 + Dear ImGui control panel
  - Assembly/class browse, **Value Search**, field analysis, transform edit, live watch/plot, call logging, SDK export and scripting dock

## Requirements & building

### Prerequisites

| Requirement | Notes |
| --- | --- |
| **OS** | Windows **x64** (build and runtime target) |
| **Visual Studio** | 2022 or later with the **v145** platform toolset and **Desktop development with C++** workload |
| **vcpkg** | Required for third-party dependencies; the project uses **manifest mode** (`VcpkgEnableManifest` in `DllStalker.vcxproj`) |
| **Unity target** | IL2CPP (`GameAssembly.dll`) or Mono (`mono-2.0-bdwgc.dll`) game on Windows |

Install and integrate vcpkg with Visual Studio (or set `VCPKG_ROOT` and use the MSBuild integration). On first build, vcpkg reads **`vcpkg.json`** from the repository and installs the declared packages (MinHook, Dear ImGui, LuaJIT, and their transitive deps) into a local `vcpkg_installed` tree. x64 configurations link those libraries **statically** (`VcpkgUseStatic`).

### Build steps

1. Clone the repository and open **`DllStalker.slnx`** at the repository root in Visual Studio.
2. Set the active configuration to **`DebugRelease | x64`** for daily GUI work, or **`Release | x64`** for a hook-only payload without the dumper/GUI.
3. Build the project (**Build → Build DllStalker**).
4. Collect the output DLL from **`DllStalker/x64/<Configuration>/version.dll`** (for example `DllStalker/x64/DebugRelease/version.dll` when the project lives in a `DllStalker/` subfolder).

If package restore fails, confirm vcpkg is on PATH or integrated in VS, then rebuild so manifest dependencies can download and compile.

### Configuration vs. features

`ENABLE_DUMPER` in `include/build_config.h` (included by `pch.h`) is defined when `_DEBUG` or `DEBUGRELEASE`. It gates the GUI, dumper and related `types/` code.

| Config | Dumper GUI |
| --- | --- |
| **DebugRelease \| x64** | Yes — optimized; preferred daily driver |
| **Debug \| x64** | Yes — full GUI + native stepping/breakpoints |
| **Release \| x64** | No — hooks and engine only |

### Release (production hook payload)

Use **Release \| x64** for a lean DLL focused on preset hooks.

- Optimized code generation
- Smaller/faster runtime footprint
- No GUI or dumper paths
- Best choice for stable, repeatable hook deployment

### Debug (development and investigation)

Use **Debug \| x64** when you need the control panel plus native debugging.

- Full GUI and runtime invoke tooling
- Init-screen **System log** for bootstrap/hook feedback
- Easier iteration on hooks and inspector behavior

### DebugRelease (recommended for GUI work)

Use **DebugRelease \| x64** when smoke-testing the control panel.

- Optimized like Release, with all `ENABLE_DUMPER` paths enabled
- Typical build for inspector, **Search**, dock tabs, transform tab, call logger and scripting validation

## Binary Placement

Build output: **version.dll**.

### Deploy

1. Copy `version.dll` to the game root folder (same folder as the game `.exe`).
2. Keep the filename exactly `version.dll`.
3. Run the game and verify startup logs.

### Why `version.dll`?

Windows checks the game executable folder first for imported DLLs.
Placing `version.dll` next to the game `.exe` makes the game load DllStalker at startup.
DllStalker forwards the standard Windows `version.dll` exports to `C:\Windows\System32\version.dll`, so callers of the real version API keep working.

## First run (Debug / DebugRelease)

After a successful deploy of a dumper-enabled build:

1. **Control panel** — a separate desktop window titled **DllStalker - Control Panel** opens automatically. It is **not** an in-game overlay; alt-tab to it like any other Win32 app.
2. On the **Init** screen, review the **System log** (in-memory bootstrap ring — hook/engine status). There is **no** Win32 debug console in dumper builds.
3. Click **Init Dumper Engine** before browsing assemblies (see [GUI Capabilities](#gui-capabilities)).

**Release \| x64** builds do not spawn the control panel. Bootstrap lines append to `stalker_runtime/dllstalker.log` next to the proxy DLL when the file sink is available.

## Bootstrap diagnostics

Dumper builds (`Debug` / `DebugRelease`) keep startup diagnostics in an in-memory **BootstrapLog** ring and show them on the Init screen **System log** (auto-scroll). Closing the control panel does **not** exit the game.

Typical lines include:

- Startup status (`Unity.Init`, preset install progress)
- Hook creation/enable results and error codes
- Runtime warnings (missing exports, unresolved targets)

**Release \| x64** writes the same class of messages to `stalker_runtime/dllstalker.log` (no GUI).

Why this matters:

- Fast validation that hooks were mounted
- Immediate signal when a method/assembly lookup fails
- Diagnose issues without connecting a debugger

## GUI Capabilities

The debug GUI is for runtime exploration, controlled edits and safe invocation.

**First step:** click **Init Dumper Engine** in the control panel before browsing assemblies.

**Layout:** left sidebar (**Classes \| Search**); right workspace (**Methods | Fields | Transform**); bottom **Utilities Dock** (**Bookmarks → History → Watcher → Logger → Exporter → Scripting**). Dock tabs restore navigation or host live tooling; they do not replace the main inspector models.

### Utilities Dock

| Tab | Purpose |
| --- | --- |
| **Bookmarks** | Saved inspector locations; restore with shared navigation validation |
| **History** | Navigation timeline and audit rows (read-only; distinct from call log) |
| **Watcher** | Live field watchlist (cap 32); configurable **Poll** interval; **Default chart** mode; inner **Watchlist** / **Charts** for numeric plots |
| **Logger** | Inner **Hooks** / **Log**; native call hooks and rolling args-only call log |
| **Exporter** | Export field offsets (optional methods/enums) to C++ `.h` or C# `.cs` beside the injected DLL |
| **Scripting** | Discover Lua packages, Start/Stop/Reload scripts, show console output and audit counters |

From **Watcher** or **History**, **Jump** restores the workspace to the linked location.

### Browser and filtering

- **Image Selection**
  - Shows loaded assemblies/images with class counts
  - Filter is **case-sensitive** (`strstr` behavior)
- **Class Browser** (sidebar **Classes**)
  - Fuzzy filter over class name + namespace
  - Filter is **case-insensitive**
  - Long names scroll horizontally in the list
- **Sorting behavior**
  - Data is rendered from runtime snapshots/caches
  - For Mono image enumeration, duplicates are normalized internally (sorted + deduplicated before display)

### Search tab (Value Search)

Sidebar **Search** finds live instance fields by **name** and/or **value** within the **selected image** (not a Cheat Engine–style heap scan). Explicit **Search** runs a background walk; **Drill** re-checks the current hit list when values change; **Stop** cancels an in-flight run.

**Filters**

- **Field name** / **Value** boxes — empty value + name is name-only Search; Drill requires a value
- **`=/~`** toggles (left of each box) — **Strict** / **Fuzzy** match mode (name default fuzzy; value default strict). String values compare after stripping wrapping quotes (case-insensitive)
- **Type:** **Number · String · Ptr** chips
- **Deep** (tree icon, next to Drill) — search inside Array/List element fields and **one** PTR hop (e.g. `codeLibrary.code[0].name`). Deep off → no interiors / no Follow

**Hits**

- Rows look like `Class::instance::field = value` (long rows scroll horizontally)
- Click a hit to load that instance in the inspector
- Cap **500** hits; status shows instance/field visit counts

### Instance discovery (two methods)

In the **Fields** tab, **Find Instances** supports two discovery modes:

1. **Static discovery** — scans static-instance candidates
2. **Live API** — uses Unity live object lookup APIs

Switch source mode, run discovery and pick the active instance from the candidates combo.

### Field analysis and watch

On the **Fields** tab (analysis toolbar):

- **Snapshot** baseline → **Show changes** tints rows when values drift (green/red/yellow).
- **Compare two instances** — class-level A vs B from root instance finds; **Current path** mode for drilled paths and collection element index.
- **`[W]`** — add/remove a field on the **Watcher** dock tab (live reads; plottable numerics can open **Charts**).
- **`[T]`** — focus **Transform** on a Transform/GameObject pointer field.
- **Enum fields** — decoded literals; editable via combo when supported.

**Watcher** dock (**Utilities → Watcher**):

- **Poll** — how often watched values are read: `100ms`, `250ms`, `500ms`, `1s`, `2s`, or `5s` (default `100ms`). This is separate from **Fields** auto-refresh, which reloads inspector metadata.
- **Default chart** — `Every sample` (ring buffer of recent points) or `On change` (plot only when the decoded value changes; better for slow-updating fields). New watches inherit this default.
- **Watchlist** — live value column, **Jump** (restore navigation), **Remove**, **Plot** (focus **Charts** for plottable int/float fields).
- **Charts** — select a series; **Mode** per series: `Default` (follow toolbar default), `Every sample`, or `On change`. Charts use evenly spaced points (no wall-clock axis).

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

### Transform tab

Inspect and edit Transform/GameObject state via Unity API invokers (not raw offset reads):

- **Sources** — Self, implicit transform/gameObject and field pointers
- **Live** — periodic refetch; **Edit lock** — pause live refresh while editing
- **Fetch / apply** — main-thread getters/setters for local/world position, rotation, scale, activeSelf
- Requires the same main-thread capture as **Run** on Methods

### Scripting dock

Runtime-authored Lua scripts live under `stalker_runtime/mods` next to the injected `version.dll`.

- **Safe** (default): `ds.*` lookup, instance field get/set, explicit-signature invoke, ticks, sleep, logging, and cooperative stop/reload.
- **Curated**: adds `ds_abi` and `ds.types.Instance` helpers backed by DllStalker's `DS_*` ABI on the proxy DLL. Raw user `ffi` stays blocked.
- Script-visible identity is an opaque handle, not a native pointer. Address helpers are for logs only; Curated raw addresses are transient.
- **Getting started:** copy a folder from [`docs/examples/packages/`](docs/examples/packages/), edit the constants at the top of `main.lua`, then **Refresh** and **Start** in the dock. Full API: [`docs/examples/Scripting-Guide.md`](docs/examples/Scripting-Guide.md).

### Field editing

Simple field editing is supported for scalars, booleans (checkbox), enums (combo), strings (quoted display; edit strips quotes), and allowlisted Unity inline structs where decode/write are honest.

- Typical workflow: edit value in-place, press **OK**, auto-refresh verifies the write.
- Opaque pointer / collection **container** overwrite stays restricted (navigate elements instead).

### Navigation (Breadcrumbs)

Navigate object graphs directly from field values:

- **Pointer fields** — drill into referenced object
- **Array/List fields** — open synthesized collection view (vector-like navigation)
- **Breadcrumbs** — jump back to any previous level

## Quick Workflows

### Workflow 1: Image → Class → Instance → Run method

1. Click **Init Dumper Engine**.
2. Open **Image Selection** and choose the target image.
3. Use sidebar **Classes** filter to find your class.
4. In **Fields**, click **Find Instances** and choose source mode (Static discovery / Live API).
5. Pick an instance from the candidates combo.
6. Go to **Methods** and click **Run**:
   - immediate call for 0-arg methods,
   - arg modal for methods with parameters.

### Workflow 1b: Search fields by name or value

1. Click **Init Dumper Engine** and select an image.
2. Open sidebar **Search**.
3. Enter a **Field name** and/or **Value** (e.g. value `SpoonBender`, or name `cheats`).
4. Enable **Deep** when the value sits inside an Array/List or behind a PTR field.
5. Click **Search** (magnifier). Click a hit to open that instance in **Fields**.
6. Optional: change the live value in-game or in Fields, then **Drill** to keep only hits that still match the value box.

### Workflow 2: Edit a primitive field and verify

1. Select image, class and active instance.
2. In **Fields**, edit a primitive or enum value.
3. Click **OK**.
4. Use **Refresh Fields** (or auto-refresh) to confirm the new value.

### Workflow 3: Drill into pointers/collections and return via breadcrumbs

1. In **Fields**, click a pointer value to open nested object view.
2. Click array/list value to open collection view.
3. Continue drilling as needed.
4. Use **Breadcrumbs** to jump back to any previous level.

### Workflow 4: Watch and plot a numeric field

1. Select image, class and instance; open **Fields**.
2. Toggle **`[W]`** on a numeric field.
3. Open **Utilities → Watcher** for live values.
4. Optional: set **Poll** (e.g. `2s` for slow fields) and **Default chart** → `On change` before or after adding watches.
5. Use **Plot** on a plottable row to open **Charts**, or pick a series there; override **Mode** per series if needed (e.g. one field every sample, another on change only).

### Workflow 5: Log native method calls

1. Open **Methods** for the target class.
2. Click **Log** on a supported method (disabled rows show a tooltip why).
3. Trigger the method in-game; open **Utilities → Logger → Log** to tail lines.
4. Use **Hooks** to **Remove** hooks or toggle **Log** off on Methods; **Clear log** clears lines only.

### Workflow 6: Edit transform via Transform tab

1. Select image, class and instance.
2. Open **Transform** or press **`[T]`** on a Transform/GameObject field in **Fields**.
3. Select a source, wait for fetch (main thread must be captured).
4. Edit values and apply; use **Live** for periodic refresh.

### Workflow 7: Export SDK offsets

1. Browse to the desired scope (active class, entire image or bookmarked classes).
2. Open **Utilities → Exporter**.
3. Choose format (C++ or C#), options and filename; export.
4. Output is written next to the injected `version.dll`.

### Workflow 8: Run a Lua script package

1. Copy a tutorial folder from [`docs/examples/packages/`](docs/examples/packages/) into `stalker_runtime/mods` beside `version.dll` (or author your own folder with `main.lua`; `manifest.json` is optional for basic Safe scripts).
2. Edit `IMAGE`, `CLASS`, and other constants at the top of `main.lua` for the target game.
3. Open **Utilities → Scripting** and click **Refresh**.
4. Select the package, then click **Start**.
5. Use **Stop** or **Reload** while iterating in an external editor; tick scripts keep running until stopped.

## Creating a New Preset (Example)

Presets self-register at static init: add a new `.cpp` under `src/presets/` and list it in `DllStalker.vcxproj`.

### 1) Create file

Example path:

- `src/presets/example/example.cpp`

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
const bool kRegistered = (Presets::Register(kPreset), true); // <---- Function call registers the plugin
}
```

### 3) Select it in installer

In `src/services/hook_installer.cpp`, set:

- `kSelectedPresetName = "Example";` — install that preset's hooks, case sensitive
- `kSelectedPresetName = "-";` — **no preset hooks** (default; avoids colliding with GUI call-logger hooks)

### 4) Build and verify

- Build **DebugRelease \| x64** or **Debug \| x64** for Init-screen bootstrap log + GUI
- Build **Release \| x64** for hook-only payload
- Deploy `version.dll` and confirm preset install messages in the Init **System log** (or `stalker_runtime/dllstalker.log` on Release)

## Runtime Flow

1. `DllMain` starts bootstrap thread (and GUI thread when dumper is enabled).
2. `Engine::Unity.Init()` resolves runtime exports (diagnostics via `BootstrapLog`).
3. `MainThreadDispatcher::InstallRuntimeInvokeHook()` (dumper builds) — non-fatal if it fails.
4. `Hooks::StartHooking()` initializes MinHook and installs the selected preset.
5. GUI thread runs the control panel; user clicks **Init Dumper Engine** to create `UnityDumper` and load images.

## Repository Layout (high level)

```text
DllStalker/
├── docs/
│   └── examples/
│       └── packages/
├── include/
│   ├── engine/
│   ├── dumper/
│   ├── gui/
│   │   ├── app/
│   │   ├── infra/
│   │   ├── state/
│   │   │   ├── core/
│   │   │   ├── navigation/
│   │   │   ├── history/
│   │   │   ├── fields/
│   │   │   ├── runtime/
│   │   │   └── transform/
│   │   └── views/
│   │       ├── fields/
│   │       ├── transform/
│   │       └── dock/
│   ├── presets/
│   ├── scripting/
│   │   ├── abi/
│   │   ├── bridge/
│   │   ├── core/
│   │   ├── handles/
│   │   └── runtime/
│   ├── services/
│   └── types/
└── src/
    ├── engine/
    ├── dumper/
    ├── gui/
    │   ├── app/
    │   ├── infra/
    │   ├── state/
    │   │   ├── core/
    │   │   ├── navigation/
    │   │   ├── history/
    │   │   ├── fields/
    │   │   ├── runtime/
    │   │   └── transform/
    │   └── views/
    │       ├── fields/
    │       ├── transform/
    │       └── dock/
    ├── presets/
    ├── scripting/
    │   ├── abi/
    │   ├── bridge/
    │   ├── core/
    │   ├── handles/
    │   └── runtime/
    ├── services/
    └── types/
```

## Troubleshooting

### "Preset not found"

- Check `kSelectedPresetName` in `src/services/hook_installer.cpp` (must match a registered name, not `"-"`)
- Ensure the preset `.cpp` is in `DllStalker.vcxproj` and calls `Presets::Register(...)`

### "Failed to find target assembly"

- Verify the preset assembly name (`Assembly-CSharp`, etc.)
- Confirm target runtime is fully initialized before install

### Hook create/enable failed

- Check the Init **System log** (or Release `dllstalker.log`) for MinHook status
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

### Search: no hits or unexpected misses

- Select an **image** first; Search walks classes in that assembly only.
- Enable **Deep** for values inside Array/List elements or one PTR hop (e.g. nested library → `cheats[i].name`).
- String values: type the text without relying on exact quotes (Search normalizes wrapping `"`).
- Hit cap is **500** — narrow name/value or Drill after editing.

### Logger: Log disabled or no lines

- **Log** disabled: no native address, unknown Mono params, unsupported arg types or too many register arguments.
- **No lines:** hook not armed, method not called yet or hook removed; check **Logger → Hooks**.
- **Cap / install errors:** max **16** hooks; duplicate native target or MinHook install failure shows a status toast.

### Scripting: package does not appear or Start fails

- Deploy under `stalker_runtime/mods` next to `version.dll`, not from the repo `docs/examples/` path.
- Directory packages need a root-level entry file. Without `manifest.json`, the entry must be **`main.lua`**. With `manifest.json`, set `"entry_file"` for another root-level name.
- Loose `.lua` files directly under `mods` run as Safe scripts with default timing budgets.
- `entry_file` must be a file name in the package root, not a nested or absolute path.
- `IMAGE` strings must match runtime assembly names (often `Assembly-CSharp.dll`, not always the short name).
- Curated helpers require `"profile": "Curated"` (or `"Advanced"`) in `manifest.json`; unknown profiles run as Safe with a warning.
- See [`docs/examples/Scripting-Guide.md`](docs/examples/Scripting-Guide.md) and [`docs/examples/Manifest-Schema.md`](docs/examples/Manifest-Schema.md).

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
