# Unity-Auto-Resolver

A C++ internal DLL template for Unity games (IL2CPP/Mono) with automatic runtime resolution powered by `UnityResolver`.

## Overview

This project is built around runtime automation:

- automatic assembly discovery,
- automatic method address lookup,
- automatic field offset resolution,
- MinHook-based detours for gameplay hooks.

Unlike manual offset workflows, field data is resolved dynamically at runtime through `UnityResolver`.

---

## Features

- **Automatic Data Resolution (`UnityResolver`)**
  - Detects IL2CPP (`GameAssembly.dll`) or Mono (`mono-2.0-bdwgc.dll`)
  - Resolves engine exports dynamically (`il2cpp_*` / `mono_*`)
  - Finds target assembly images at runtime
  - Resolves method addresses and field offsets without hardcoded field offsets

- **Hooking with MinHook**
  - Centralized hook setup in `src/hooks.cpp`
  - Detour/original pairing with status logging
  - Preset-based routing via `StartHooking(...)`

- **Example Hook (`Hooks::Dane::hkAwake`)**
  - Hooks `SettingsManager::Awake`
  - Resolves `hackMode` offset dynamically
  - Writes the configured value directly to the instance

- **Optional Runtime Dumper (`UnityDumper`)**
  - Class/method/field dump helpers
  - Runtime object inspection utilities

---

## Build Mode Notes

`UnityDumper` is controlled by `ENABLE_DUMPER` in `include/pch.h`.

- **Debug build**: dumper is enabled by default.
- **Release build**: dumper is disabled by default.

If you want dumper/debugger-style runtime inspection, compile in **Debug** mode.

---

## Project Layout

```text
DllStalker/
├── include/
│   ├── hooks.h
│   ├── hooks_definitions.h
│   ├── hooks_definition/
│   │   └── dane.h
│   ├── unity_resolver.h
│   ├── unity_dumper.h
│   └── pch.h
├── src/
│   ├── dllmain.cpp
│   ├── hooks.cpp
│   ├── hooks_definition/
│   │   └── dane.cpp
│   ├── unity_resolver.cpp
│   ├── unity_dumper.cpp
│   └── pch.cpp
└── DllStalker.vcxproj
```

---

## Runtime Flow

1. `DllMain` starts `MainThread`.
2. `UnityResolver::Init()` waits for Unity runtime and resolves exports.
3. Target assembly image is located (default: `Assembly-CSharp`).
4. `StartHooking(...)` initializes MinHook and applies the selected hook preset.

---

## Disclaimer

This repository is intended for reverse-engineering education and runtime instrumentation in environments where you have explicit permission. You are responsible for legal and policy compliance.
