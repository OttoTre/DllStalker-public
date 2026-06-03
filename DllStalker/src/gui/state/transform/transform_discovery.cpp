#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/state/transform/transform_internal.h"
#include "gui/state/transform/transform_model.h"

#include "gui/session_state.h"
#include "services/main_thread_dispatcher.h"
#include "types/memory_guard.h"

#include <algorithm>

namespace Gui::State
{
using namespace TransformDetail;

void TransformModel::EnqueueImplicitDiscovery(ControlPanelSessionState& state, void* componentInstance) {
    (void)state;
    if (!componentInstance || implicitPending.load()) {
        return;
    }
    implicitPending.store(true);
    implicitReady.store(false);
    implicitTransformPtr  = nullptr;
    implicitGameObjectPtr = nullptr;

    Engine::Services::MainThreadDispatcher::Enqueue([this, componentInstance]() {
        void* image          = GetCoreModuleImage();
        void* componentKlass = GetClass(image, "UnityEngine", "Component");
        void* mTransform     = GetMethodOnClass(componentKlass, "get_transform", 0);
        void* mGameObject    = GetMethodOnClass(componentKlass, "get_gameObject", 0);

        void* transformRet  = nullptr;
        void* gameObjectRet = nullptr;
        if (mTransform) {
            InvokeGetter(mTransform, componentInstance, &transformRet);
        }
        if (mGameObject) {
            InvokeGetter(mGameObject, componentInstance, &gameObjectRet);
        }

        implicitTransformPtr  = transformRet;
        implicitGameObjectPtr = gameObjectRet;
        implicitPending.store(false);
        implicitReady.store(true);
    });
}

void TransformModel::MergeImplicitSources() {
    if (!implicitReady.load()) {
        return;
    }
    if (implicitTransformPtr) {
        TransformSource row{};
        row.kind         = TransformSourceKind::ImplicitTransform;
        row.label        = "transform";
        row.typeName     = "Transform";
        row.instancePtr  = implicitTransformPtr;
        row.isGameObject = false;
        AddSourceIfNew(sources, std::move(row));
    }
    if (implicitGameObjectPtr) {
        TransformSource row{};
        row.kind         = TransformSourceKind::ImplicitGameObject;
        row.label        = "gameObject";
        row.typeName     = "GameObject";
        row.instancePtr  = implicitGameObjectPtr;
        row.isGameObject = true;
        AddSourceIfNew(sources, std::move(row));
    }
}

void TransformModel::Discover(ControlPanelSessionState& state, const InspectorCache& snapshot) {
    void* instance = snapshot.activeInstancePtr;
    void* klass    = snapshot.activeClassPtr;

    if (instance != lastActiveInstance || klass != lastActiveClass) {
        sources.clear();
        selectedIndex            = 0;
        lastActiveInstance       = instance;
        lastActiveClass          = klass;
        lastFieldScanForInstance = nullptr;
        lastFieldScanFieldCount  = 0;
        ResetImplicitState();
        InvalidateFetch();
    }

    if (!instance || !klass || !state.dumper) {
        sources.clear();
        lastFieldScanForInstance = nullptr;
        lastFieldScanFieldCount  = 0;
        return;
    }

    if (!InstanceReadable(instance)) {
        stale.store(true);
        return;
    }
    stale.store(false);

    const bool isTransform  = state.dumper->IsOrInheritsFrom(klass, "Transform");
    const bool isGameObject = state.dumper->IsOrInheritsFrom(klass, "GameObject");

    if (sources.empty()) {
        if (isTransform || isGameObject) {
            std::string className = state.dumper->TryGetClassNameFromInstance(instance);
            TransformSource row{};
            row.kind         = TransformSourceKind::Self;
            row.label        = "Self (" + (className.empty() ? "?" : className) + ")";
            row.typeName     = className.empty() ? "?" : className;
            row.instancePtr  = instance;
            row.isGameObject = isGameObject;
            sources.push_back(std::move(row));
        }

        if (IsComponentLike(*state.dumper, klass) && !isTransform && !isGameObject
            && !implicitPending.load() && !implicitReady.load()) {
            EnqueueImplicitDiscovery(state, instance);
        }
    }

    // Field rows: only when inspector fields are loaded for this instance (not every frame).
    const bool fieldsReady    = (snapshot.fieldsLoadedForInstance == instance);
    const bool needsFieldScan = fieldsReady
        && (lastFieldScanForInstance != instance
            || lastFieldScanFieldCount != snapshot.fields.size());
    if (needsFieldScan) {
        sources.erase(std::remove_if(sources.begin(),
                                     sources.end(),
                                     [](const TransformSource& s) {
                                         return s.kind == TransformSourceKind::Field;
                                     }),
                      sources.end());

        for (const auto& field : snapshot.fields) {
            if (!field.hasValue || field.valueAddress == 0) {
                continue;
            }
            void* ptr = nullptr;
            if (!Engine::Memory::TryReadValue(field.valueAddress, ptr) || !ptr) {
                continue;
            }
            void* fieldKlass = nullptr;
            state.dumper->TryGetClassNameFromInstance(ptr, &fieldKlass);
            if (!IsTransformRelatedKlass(*state.dumper, fieldKlass)) {
                continue;
            }
            std::string typeName = state.dumper->TryGetClassNameFromInstance(ptr);
            TransformSource row{};
            row.kind         = TransformSourceKind::Field;
            row.label        = field.name;
            row.typeName     = typeName.empty() ? field.type : typeName;
            row.instancePtr  = ptr;
            row.isGameObject = state.dumper->IsOrInheritsFrom(fieldKlass, "GameObject");
            AddSourceIfNew(sources, std::move(row));
        }

        lastFieldScanForInstance = instance;
        lastFieldScanFieldCount  = snapshot.fields.size();
    }

    MergeImplicitSources();

    if (pendingFocusInstance != nullptr) {
        for (size_t i = 0; i < sources.size(); ++i) {
            if (sources[i].instancePtr == pendingFocusInstance) {
                selectedIndex = static_cast<int>(i);
                InvalidateFetch();
                break;
            }
        }
        pendingFocusInstance = nullptr;
    }

    if (selectedIndex < 0 || selectedIndex >= static_cast<int>(sources.size())) {
        selectedIndex = 0;
    }
}

} // namespace Gui::State

#endif // ENABLE_DUMPER
