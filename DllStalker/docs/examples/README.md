# Script Package Examples

Copyable tutorial packages for `stalker_runtime/mods` beside the injected `version.dll`.

## Reference

| File | Purpose |
|------|---------|
| [`Scripting-Guide.md`](Scripting-Guide.md) | Lua API reference, profiles, handles, Curated ABI |
| [`Manifest-Schema.md`](Manifest-Schema.md) | Parsed manifest fields, defaults, and parser limits |
| [`manifest.template.json`](manifest.template.json) | Safe-profile starter manifest |
| [`manifest.curated.template.json`](manifest.curated.template.json) | Curated-profile starter manifest |

## Tutorial Packages

1. Copy one folder from [`packages/`](packages/) into `stalker_runtime/mods`.
2. Edit the constants at the top of `main.lua` (`IMAGE`, `CLASS`, `FIELD`, and so on).
3. In the Scripting dock: **Refresh** → select package → **Start**.

| Package | Profile | Teaches |
|---------|---------|---------|
| [`metadata-lookup`](packages/metadata-lookup/) | Safe | Images, classes, fields, methods; `address_hex()` for logs |
| [`heal-below-threshold`](packages/heal-below-threshold/) | Safe | `find_object`, `get`, conditional `set` |
| [`list-instances`](packages/list-instances/) | Safe | `find_objects` and bounded logging |
| [`invoke-method`](packages/invoke-method/) | Safe | `invoke` with an explicit signature string |
| [`field-watcher`](packages/field-watcher/) | Safe | `ds.on_tick` / `ds.on_unload` polling |
| [`curated-offset-read`](packages/curated-offset-read/) | Curated | `ds.types.Instance` offset read (optional write) |

## Manifest Quick Rules

- `schema_version` must be `1` when `manifest.json` is present.
- Minimal package: `schema_version` + `name` only; `entry_file` defaults to `main.lua`, `profile` defaults to Safe.
- Add `"profile": "Curated"` only for packages that use `ds.types` / `ds_abi`.
- `permissions` and nested `configuration_defaults` are not supported today.
