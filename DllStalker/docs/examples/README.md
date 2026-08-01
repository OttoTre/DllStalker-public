# Script Package Examples

Starter manifests and modder references for packages under `stalker_runtime/mods` beside the injected `version.dll`.

## Reference

| File | Purpose |
|------|---------|
| [`Scripting-Guide.md`](Scripting-Guide.md) | Lua API reference, profiles, handles, Curated ABI, inline examples |
| [`Manifest-Schema.md`](Manifest-Schema.md) | Parsed manifest fields, defaults, and parser limits |
| [`manifest.template.json`](manifest.template.json) | Safe-profile starter manifest |
| [`manifest.curated.template.json`](manifest.curated.template.json) | Curated-profile starter manifest |

## Getting Started

1. Create a folder under `stalker_runtime/mods` (or drop a loose `.lua` file there for a Safe one-shot).
2. Copy [`manifest.template.json`](manifest.template.json) or [`manifest.curated.template.json`](manifest.curated.template.json) as `manifest.json` when you need a display name, Curated profile, custom `entry_file`, or non-default timing budgets. Omit the manifest for a basic Safe folder that uses `main.lua`.
3. Write `main.lua` using the API and snippets in [`Scripting-Guide.md`](Scripting-Guide.md). Edit game-specific constants (`IMAGE`, `CLASS`, `FIELD`, and so on) for the target title.
4. In the Scripting dock: **Refresh** → select package → **Start**.

## Manifest Quick Rules

- `schema_version` must be `1` when `manifest.json` is present.
- Minimal package: `schema_version` + `name` only; `entry_file` defaults to `main.lua`, `profile` defaults to Safe.
- Add `"profile": "Curated"` only for packages that use `ds.types` / `ds_abi`.
- `permissions` and nested `configuration_defaults` are not supported today.
