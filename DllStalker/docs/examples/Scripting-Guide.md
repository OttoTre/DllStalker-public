# DllStalker Scripting Guide

This guide covers the runtime Lua scripting surface exposed through the Scripting dock. Scripts live under `stalker_runtime/mods` next to the injected proxy DLL.

## Getting Started

1. Pick a tutorial package under [`packages/`](packages/).
2. Copy the package folder into `stalker_runtime/mods` next to the injected proxy DLL.
3. Edit the constants at the top of `main.lua` (`IMAGE`, `CLASS`, `FIELD`, `METHOD`, offsets) for the target game.
4. Open the Scripting dock, click **Refresh**, select the package, then click **Start**.
5. Watch the script console and audit counters. Use **Stop** or **Reload** for long-running tick scripts.

Loose `.lua` files placed directly under `stalker_runtime/mods` are also discovered — **any file name** works (e.g. `heal.lua`, `watcher.lua`). They run as Safe profile scripts with default timing budgets.

## API Reference

Methods marked **Curated** are available only when the package manifest requests `"profile": "Curated"` (or `"Advanced"`).

### `ds` global (Safe and Curated)

| Method | Returns | Description |
|--------|---------|-------------|
| `ds.log(...)` | — | Print tab-separated values to the script console. |
| `ds.on_tick(fn)` | `boolean` | Register a callback invoked on the host tick interval. |
| `ds.on_unload(fn)` | `boolean` | Register a best-effort cleanup callback on stop/reload/exit. |
| `ds.sleep_ms(ms)` | — | Cancellable sleep (0–60000 ms). Errors if cancelled. |
| `ds.now_ms()` | `number` | Monotonic tick count in milliseconds (`GetTickCount64`). |
| `ds.stop()` | `boolean` | Request cooperative cancellation of the running script. |
| `ds.images()` | `table` \| `nil, err` | Loaded assemblies; each entry has `name`, `class_count`, `handle`. |
| `ds.find_image(imageName)` | `Image` \| `nil, err` | Find an assembly image by name. |
| `ds.find_class(imageName, className, [namespace])` | `Class` \| `nil, err` | Find a class within an image. |
| `ds.find_method(class, signatureOrName, [argCount])` | `Method` \| `nil, err` | Find a method on a class handle. |
| `ds.find_field(class, fieldName)` | `Field` \| `nil, err` | Find a field on a class handle. |
| `ds.find_object(imageName, className, [namespace])` | `Instance` \| `nil, err` | First live instance of the class, or `nil, "not found"`. |
| `ds.find_objects(imageName, className, [namespace])` | `table` \| `nil, err` | All live instances as an array of instance handles. |

### Proxy handles (`Image`, `Class`, `Method`, `Field`, `Instance`)

All proxy userdata values support:

| Method | Profile | Returns | Description |
|--------|---------|---------|-------------|
| `:address_hex()` | Safe + Curated | `string` \| `nil, err` | Display-only native address after fresh handle validation. |
| `:get_raw_address()` | **Curated** | `lightuserdata` \| `nil, err` | Transient raw address for immediate use only; do not cache. |

`Instance` proxies additionally support:

| Method | Returns | Description |
|--------|---------|-------------|
| `:get(fieldName)` | `value` \| `nil, err` | Read a field by name on the live object. |
| `:set(fieldName, value)` | `true` \| `nil, err` | Write a field by name. |
| `:invoke(signature, ...)` | `result` \| `nil, err` | Invoke a method; signature must be explicit (e.g. `"Heal(System.Int32)"`). |

### `ds.types.Instance` (**Curated**)

| Method | Returns | Description |
|--------|---------|-------------|
| `ds.types.Instance.wrap(instance)` | `typed` | Wrap an instance proxy for offset-based typed access. |
| `typed:read_i32(offset)` | `number` \| `nil, err` | Read a 32-bit signed integer at `instance + offset`. |
| `typed:write_i32(offset, value)` | `true` \| `nil, err` | Write a 32-bit signed integer at `instance + offset`. |

### `ds_abi` (**Curated**)

Constants: `ds_abi.OK`, `ds_abi.ABI_VERSION`, `ds_abi.FEATURE_INSTANCE_PRIMITIVES`, `ds_abi.FEATURE_SINGLE_PROXY_DLL`.

| Method | Returns | Description |
|--------|---------|-------------|
| `ds_abi.abi_version()` | `number` | Current Curated ABI version (`100`). |
| `ds_abi.feature_flags()` | `number` | Bitmask of supported ABI features. |
| `ds_abi.status_to_error(status)` | `string` | Map a `DS_Status` code to a name (e.g. `"DS_ERR_STALE_OBJECT"`). |
| `ds_abi.instance_read_i32(handle, offset)` | `value` \| `nil, status, err` | Low-level read via opaque handle or typed instance. |
| `ds_abi.instance_write_i32(handle, offset, value)` | `true` \| `false, status, err` | Low-level write via opaque handle or typed instance. |

### Exported C ABI (proxy DLL, **Curated**)

Flat `extern "C"` symbols for tool-internal or advanced bindings. All Unity-touching calls accept `uint64_t ScriptHandle`, not raw addresses.

| Symbol | Description |
|--------|-------------|
| `DS_Core_GetAbiVersion()` | ABI version number. |
| `DS_Core_GetFeatureFlags()` | Feature bitmask. |
| `DS_Core_GetBuildId(buf, size)` | Caller-owned build id string; `DS_ERR_BUFFER_TOO_SMALL` on overflow. |
| `DS_Instance_ReadI32(handle, offset, out)` | Read `int32` at `instance + offset`. |
| `DS_Instance_WriteI32(handle, offset, value)` | Write `int32` at `instance + offset`. |

## Profiles

DllStalker uses LuaJIT with one isolated `lua_State` per running script.

### Safe

Safe is the default profile. It exposes the high-level `ds.*` API for lookup, field access, method invoke, ticks, sleeps, logging and stop/reload behavior.

Safe scripts do not get raw LuaJIT FFI:

- no user-visible `require("ffi")`
- no `ffi.cdef`
- no `ffi.load`
- no `package.loadlib`
- no broad `io`, `os`, `debug`, or raw `jit` control

### Curated

Curated is the normal advanced profile. It adds host-injected typed helpers backed by DllStalker's stable `DS_*` ABI while keeping raw user FFI blocked.

Curated scripts can use:

- `ds_abi`
- `ds.types.*`
- Curated-only raw address escape hatches where explicitly exposed

Curated mutations still route through DllStalker's safety path: dispatcher, handle validation, cancellation, SEH-safe memory guards and audit counters.

### Unrestricted

Unrestricted is reserved for future explicitly gated local-trusted work. It is not part of the shipped Curated profile.

## Script Packages

Directory packages are a folder under `mods/` with a root-level entry file. **`manifest.json` is optional** for basic Safe scripts — without it, the folder name is the display name, the profile is Safe, and the entry file defaults to **`main.lua` only**. Add `manifest.json` when you need Curated mode, a different `entry_file`, display metadata, or timing budgets. See [`Manifest-Schema.md`](Manifest-Schema.md) for parsed fields, defaults, clamp ranges and parser notes.

Loose `.lua` files placed **directly** under `mods/` (not inside a subfolder) are also discovered — any `*.lua` file name works — as Safe packages with default timing budgets.

A minimal Safe manifest (when you want one) needs only `schema_version` and a display `name` (the folder name is used when `name` is omitted):

```json
{
  "schema_version": 1,
  "name": "Safe Example"
}
```

Curated mode requires an explicit profile:

```json
{
  "schema_version": 1,
  "name": "Curated Example",
  "profile": "Curated"
}
```

`"Advanced"` is accepted as an alias for Curated. Unknown profiles run as Safe.

Optional fields: `version`, `author` (tooltip metadata only), `entry_file` when not `main.lua`, and top-level timing budgets when intentionally non-default. See [`Manifest-Schema.md`](Manifest-Schema.md) for defaults and clamp ranges.

```json
{
  "tick_interval_ms": 250
}
```

Do not use JSON comments, nested `configuration_defaults`, or `permissions` as supported manifest contracts.

## Tutorial Packages

Tutorial packages live under [`packages/`](packages/) and are intended to be copied into `stalker_runtime/mods`.

| Package | Profile | Demonstrates |
|---------|---------|--------------|
| [`metadata-lookup`](packages/metadata-lookup/) | Safe | Loaded image listing, class/field/method lookup, `address_hex()` diagnostics. |
| [`heal-below-threshold`](packages/heal-below-threshold/) | Safe | Instance lookup, field read/write, cooperative `ds.stop()`. |
| [`list-instances`](packages/list-instances/) | Safe | `find_objects`, bounded logging, diagnostic addresses. |
| [`invoke-method`](packages/invoke-method/) | Safe | `instance:invoke(signature, ...)` with explicit signatures and arguments. |
| [`field-watcher`](packages/field-watcher/) | Safe | `ds.on_tick`, field polling, `ds.on_unload`. |
| [`curated-offset-read`](packages/curated-offset-read/) | Curated | `ds.types.Instance.wrap`, `read_i32`, optional guarded `write_i32`. |

The packages use placeholder class names and fields. Expect to edit the constants before running them in a specific game.

## Handles

Script-visible object identity is always an opaque `ScriptHandle`, stored inside Lua proxy userdata. Handles are not native pointers.

Internally, handles are generational:

```text
ScriptHandle = (generation << 32) | registryIndex
```

Every operation resolves the handle at execution time. Scripts should not cache native addresses or treat printed addresses as identity.

## Safe API Example

```lua
local player = ds.find_object("Assembly-CSharp", "PlayerController")
if not player then
    ds.log("player not found")
    return
end

local hp, err = player:get("health")
if hp == nil then
    ds.log("read failed", err)
    return
end

if hp < 20 then
    local ok, write_err = player:set("health", 100)
    if not ok then
        ds.log("write failed", write_err)
    end
end
```

## Address Helpers

### `address_hex()`

`address_hex()` returns a display-only string after fresh handle validation.

```lua
local player = ds.find_object("Assembly-CSharp", "PlayerController")
ds.log(player:address_hex())
```

Use it for logs, diagnostics and correlating DllStalker output with external tools. It is not a pointer and should not be parsed back into an identity.

### `get_raw_address()`

`get_raw_address()` is Curated-only. It returns a transient raw address value for immediate advanced use.

Rules:

- It validates the handle before returning.
- The returned address is not stable identity.
- Do not cache it across ticks, reloads, scene changes, or queued work.
- Prefer handle-based `DS_*` helpers whenever possible.

The normal Curated ABI accepts `ScriptHandle`, not raw addresses.

## Curated ABI

DllStalker uses a single-DLL model: `DS_*` symbols are exported by the DllStalker proxy DLL itself. There is no separate support DLL to deploy.

Current exported ABI:

```cpp
uint32_t DS_Core_GetAbiVersion();
uint32_t DS_Core_GetFeatureFlags();
int32_t DS_Core_GetBuildId(char* outBuffer, uint32_t bufferSize);
int32_t DS_Instance_ReadI32(uint64_t scriptHandle, uint32_t offset, int32_t* outValue);
int32_t DS_Instance_WriteI32(uint64_t scriptHandle, uint32_t offset, int32_t value);
```

ABI rules:

- flat `extern "C"` symbols
- primitive types only
- `int32_t` status returns
- caller-owned output buffers
- no C++ classes, STL, exceptions, templates, or ownership crossing the ABI

## Curated Lua Example

```lua
local player = ds.find_object("Assembly-CSharp", "PlayerController")
if not player then
    ds.log("player not found")
    return
end

ds.log("player address", player:address_hex())

local typed = ds.types.Instance.wrap(player)
local hp, err = typed:read_i32(0x24)
if hp == nil then
    ds.log("read_i32 failed", err)
    return
end

if hp < 20 then
    local ok, write_err = typed:write_i32(0x24, 100)
    if not ok then
        ds.log("write_i32 failed", write_err)
    end
end
```

Low-level wrappers are also available:

```lua
local value, status, err = ds_abi.instance_read_i32(player, 0x24)
if value == nil then
    ds.log(status, err)
end
```

## Error Handling

Expected runtime failures return `nil, err` or `false, status, err` depending on the helper shape. API misuse, such as wrong argument types, raises a Lua error.

`DS_Status` names are available through:

```lua
ds_abi.status_to_error(status)
```

Common failures include stale handles, dispatcher unavailability, main-thread capture not being ready, timeout/starvation, unsupported types and invalid pointers.

Current limitations:

- `:get` and `:set` are instance-only helpers.
- `:invoke` returns display strings today, not fully typed Lua return values.
- Object-reference field writes are rejected.
- `ds.find_method` returns a lookup/diagnostic handle; invocation runs through `instance:invoke(signature, ...)`.
- Static field mutation, permissions enforcement, nested budget-object semantics and raw user FFI are not shipped.

