#pragma once

#include "engine/image_enumerator.h"
#include "engine/reflection.h"
#include "engine/runtime_invoke.h"
#include "engine/unity_module.h"

// Top-level facade that aggregates the engine layer and exposes a process-
// wide singleton (`Engine::Unity`). Existing callers reach the sub-services
// either through their named members (`Unity.module`, `Unity.images`, ...)
// or through the convenience forwarders below for the most common one-line
// patterns (`Unity.FindImage`, `Unity.GetMethodAddress`, ...).
namespace Engine
{
namespace
{
const char* GLOBAL_NAMESPACE = "";
constexpr int ANY_AMOUNT = -1;
} // namespace

class UnityResolver
{
public:
    static UnityResolver& Instance();

    UnityModule       module;
    ImageEnumerator   images;
    Reflection        reflection;
    RuntimeInvoker    invoker;

    // Bootstraps the engine layer in order: module find -> exports resolve
    // -> domain wait -> thread attach. Returns false if any release-required
    // step fails so the caller can short-circuit (the dumper-only exports
    // are best-effort and don't fail this call).
    bool Init();

    // ---- Convenience forwarders ----
    // These keep the historical `Engine::Unity.X` call sites in dllmain,
    // hooks, and presets working without each one having to reach through
    // the sub-service member explicitly.
    void* FindImage(const char* assemblyName) {
        return images.FindImage(assemblyName);
    }
    uintptr_t GetMethodAddress(void* image, const char* className, const char* methodName,
                               int args = ANY_AMOUNT, const char* ns = GLOBAL_NAMESPACE) const {
        return reflection.GetMethodAddress(image, className, methodName, args, ns);
    }
    uintptr_t GetFieldOffset(void* image, const char* className, const char* fieldName,
                             const char* ns = GLOBAL_NAMESPACE) const {
        return reflection.GetFieldOffset(image, className, fieldName, ns);
    }
    void* GetStaticFieldAddr(void* image, const char* className, const char* fieldName,
                             const char* ns = GLOBAL_NAMESPACE) const {
        return reflection.GetStaticFieldAddr(image, className, fieldName, ns);
    }

private:
    UnityResolver();
    UnityResolver(const UnityResolver&) = delete;
    UnityResolver& operator=(const UnityResolver&) = delete;
    UnityResolver(UnityResolver&&) = delete;
    UnityResolver& operator=(UnityResolver&&) = delete;
};

extern UnityResolver& Unity;

} // namespace Engine
