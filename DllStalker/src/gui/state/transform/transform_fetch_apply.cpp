#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/state/transform/transform_internal.h"
#include "gui/state/transform/transform_model.h"

#include "gui/session_state.h"
#include "services/main_thread_dispatcher.h"
#include "unity_resolver.h"

namespace Gui::State
{
using namespace TransformDetail;

void TransformModel::EnqueueFetch(ControlPanelSessionState& state, int sourceIndex) {
    if (!state.dumper || sourceIndex < 0 || sourceIndex >= static_cast<int>(sources.size())) {
        return;
    }
    if (pendingFetch.load()) {
        return;
    }

    if (!Engine::Services::MainThreadDispatcher::IsDispatchAvailable()
        || !Engine::Services::MainThreadDispatcher::IsMainThreadCaptured()) {
        return;
    }

    const TransformSource source = sources[static_cast<size_t>(sourceIndex)];
    if (!InstanceReadable(source.instancePtr)) {
        stale.store(true);
        fetched.store(false);
        return;
    }

    pendingFetch.store(true);

    const bool enqueued = Engine::Services::MainThreadDispatcher::Enqueue(
        [this, source, dumper = state.dumper]() {
            PopulateCacheFromSource(*this, source, dumper);
        });
    if (!enqueued) {
        pendingFetch.store(false);
    }
}

void TransformModel::EnqueueApplyLocalPosition(ControlPanelSessionState& state,
                                               int sourceIndex,
                                               Vec3f value) {
    if (!state.dumper || sourceIndex < 0 || sourceIndex >= static_cast<int>(sources.size())) {
        return;
    }
    const TransformSource source = sources[static_cast<size_t>(sourceIndex)];
    Engine::Services::MainThreadDispatcher::Enqueue([this, source, value, dumper = state.dumper]() {
        void* transformPtr = ResolveTransformForProps(*dumper, source.instancePtr);
        if (!transformPtr) {
            return;
        }
        void* klass = GetClass(GetCoreModuleImage(), "UnityEngine", "Transform");
        void* mSet  = GetMethodOnClass(klass, "set_localPosition", 1);
        if (!mSet) {
            return;
        }
        Vec3f arg       = value;
        void* args[1]   = { &arg };
        void* exc       = nullptr;
        void* ret       = nullptr;
        Engine::Unity.invoker.InvokeWithSEH(mSet, transformPtr, args, &exc, ret);
        PopulateCacheFromSource(*this, source, dumper);
    });
}

void TransformModel::EnqueueApplyLocalEuler(ControlPanelSessionState& state,
                                            int sourceIndex,
                                            Vec3f value) {
    if (!state.dumper || sourceIndex < 0 || sourceIndex >= static_cast<int>(sources.size())) {
        return;
    }
    const TransformSource source = sources[static_cast<size_t>(sourceIndex)];
    Engine::Services::MainThreadDispatcher::Enqueue([this, source, value, dumper = state.dumper]() {
        void* transformPtr = ResolveTransformForProps(*dumper, source.instancePtr);
        if (!transformPtr) {
            return;
        }
        void* klass = GetClass(GetCoreModuleImage(), "UnityEngine", "Transform");
        void* mSet  = GetMethodOnClass(klass, "set_localEulerAngles", 1);
        if (!mSet) {
            return;
        }
        Vec3f arg       = value;
        void* args[1]   = { &arg };
        void* exc       = nullptr;
        void* ret       = nullptr;
        Engine::Unity.invoker.InvokeWithSEH(mSet, transformPtr, args, &exc, ret);
        PopulateCacheFromSource(*this, source, dumper);
    });
}

void TransformModel::EnqueueApplyLocalScale(ControlPanelSessionState& state,
                                            int sourceIndex,
                                            Vec3f value) {
    if (!state.dumper || sourceIndex < 0 || sourceIndex >= static_cast<int>(sources.size())) {
        return;
    }
    const TransformSource source = sources[static_cast<size_t>(sourceIndex)];
    Engine::Services::MainThreadDispatcher::Enqueue([this, source, value, dumper = state.dumper]() {
        void* transformPtr = ResolveTransformForProps(*dumper, source.instancePtr);
        if (!transformPtr) {
            return;
        }
        void* klass = GetClass(GetCoreModuleImage(), "UnityEngine", "Transform");
        void* mSet  = GetMethodOnClass(klass, "set_localScale", 1);
        if (!mSet) {
            return;
        }
        Vec3f arg       = value;
        void* args[1]   = { &arg };
        void* exc       = nullptr;
        void* ret       = nullptr;
        Engine::Unity.invoker.InvokeWithSEH(mSet, transformPtr, args, &exc, ret);
        PopulateCacheFromSource(*this, source, dumper);
    });
}

void TransformModel::EnqueueApplyActive(ControlPanelSessionState& state, int sourceIndex, bool active) {
    if (!state.dumper || sourceIndex < 0 || sourceIndex >= static_cast<int>(sources.size())) {
        return;
    }
    const TransformSource source = sources[static_cast<size_t>(sourceIndex)];
    Engine::Services::MainThreadDispatcher::Enqueue([this, source, active, dumper = state.dumper]() {
        void* goPtr = source.isGameObject
            ? source.instancePtr
            : ResolveGameObjectForActive(source.instancePtr, *dumper);
        if (!goPtr) {
            return;
        }
        void* klass     = GetClass(GetCoreModuleImage(), "UnityEngine", "GameObject");
        void* mSet      = GetMethodOnClass(klass, "SetActive", 1);
        if (!mSet) {
            return;
        }
        uint8_t argByte = active ? 1 : 0;
        void* args[1]   = { &argByte };
        void* exc       = nullptr;
        void* ret       = nullptr;
        Engine::Unity.invoker.InvokeWithSEH(mSet, goPtr, args, &exc, ret);
        PopulateCacheFromSource(*this, source, dumper);
    });
}

} // namespace Gui::State

#endif // ENABLE_DUMPER
