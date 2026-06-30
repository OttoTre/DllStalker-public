# Script Manifest Schema

Script packages live under `stalker_runtime/mods` next to the injected DllStalker proxy DLL.

The Scripting dock supports three package shapes:

- A **directory** with `main.lua` in the folder root. `manifest.json` is optional; omit it for basic Safe scripts (folder name + defaults, **`main.lua` entry only**).
- A **directory** with `manifest.json` when you need Curated profile, custom `entry_file`, display metadata, or timing budgets.
- A **loose** root-level `.lua` file directly under `mods/` (any file name). Runs as Safe with default timing budgets.

## Directory Package Example

Minimal Safe package (recommended starter):

```json
{
  "schema_version": 1,
  "name": "Example Script"
}
```

Curated package:

```json
{
  "schema_version": 1,
  "name": "Curated Example",
  "profile": "Curated"
}
```

Include optional fields only when they differ from defaults. A full field listing for reference:

```json
{
  "schema_version": 1,
  "name": "Example Script",
  "version": "0.1.0",
  "author": "YourName",
  "entry_file": "main.lua",
  "profile": "Safe",
  "tick_interval_ms": 16,
  "command_timeout_ms": 1500,
  "soft_timeout_ms": 2000,
  "hard_quarantine_ms": 5000
}
```

Omit `entry_file` when using `main.lua`, `profile` when using Safe, and timing fields when using parser defaults.

## Fields


| Field                | Type    | Required | Default             | Notes                                                                                                      |
| -------------------- | ------- | -------- | ------------------- | ---------------------------------------------------------------------------------------------------------- |
| `schema_version`     | integer | Yes      | —                   | Must be `1` when `manifest.json` exists.                                                                   |
| `name`               | string  | No       | Package folder name | Display name in the Scripting dock.                                                                        |
| `version`            | string  | No       | empty               | Display metadata only.                                                                                     |
| `author`             | string  | No       | empty               | Display metadata only.                                                                                     |
| `entry_file`         | string  | No       | `main.lua`          | Must be a file name in the package root, not a nested path.                                                |
| `profile`            | string  | No       | `Safe`              | `Safe`, `Curated`, or `Advanced`. `Advanced` runs as Curated. Unknown profiles run as Safe with a warning. |
| `tick_interval_ms`   | integer | No       | `16`                | Clamped to `10` through `1000`.                                                                            |
| `command_timeout_ms` | integer | No       | `1500`              | Clamped to `100` through `10000`.                                                                          |
| `soft_timeout_ms`    | integer | No       | `2000`              | Clamped to `100` through `10000`.                                                                          |
| `hard_quarantine_ms` | integer | No       | `5000`              | Clamped to `500` through `30000`.                                                                          |




## Entry File Rules

`entry_file` must be only a file name in the package root:

- Good: `main.lua`
- Good: `watcher.lua`
- Bad: `scripts/main.lua`
- Bad: `../main.lua`
- Bad: `C:\\mods\\main.lua`

If `entry_file` is missing or empty, the dock uses `main.lua`.

## Profiles

`Safe` is the default and should be used for most scripts. It exposes high-level `ds.*` lookup, field, invoke, tick, sleep, log, and lifecycle helpers.

`Curated` adds host-injected typed helpers:

- `ds_abi`
- `ds.types.Instance`
- Curated-only `get_raw_address()`

`Advanced` is accepted as an alias for Curated. Unknown profile names run as Safe and the package row shows a warning.

`Unrestricted` is reserved for future local-trusted raw FFI work. It is not available today.

## Timing Budgets

`tick_interval_ms` controls the host-owned `ds.on_tick` interval. Long work inside a tick delays the next tick and can trip the watchdog.

`command_timeout_ms` bounds Unity-touching reflection/field/invoke commands.

`soft_timeout_ms` marks long ticks as unresponsive in the Scripting dock.

`hard_quarantine_ms` quarantines a wedged script without terminating the thread.

## Parser Notes

The current manifest reader is intentionally narrow:

- JSON comments are not supported.
- Use decimal integers for numeric fields.
- Known fields are scanned by name; unknown fields are ignored.
- Optional numeric fields are clamped into the ranges above instead of rejecting out-of-range values.
- `permissions` is not parsed or enforced.
- Nested `configuration_defaults` is not supported by the current parser; keep timing fields top-level.

Expected manifest failures show in the package row, such as `missing schema_version`, `unsupported manifest schema`, `entry_file must be a file name in the package root`, or `entry file not found`.