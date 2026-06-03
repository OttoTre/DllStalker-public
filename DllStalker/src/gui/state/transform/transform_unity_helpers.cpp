#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/state/transform/transform_internal.h"

#include "types/memory_guard.h"
#include "types/value_decoder.h"
#include "unity_resolver.h"

#include <cstring>

namespace Gui::State::TransformDetail
{
bool IsTransformRelatedKlass(Engine::UnityDumper& dumper, void* klass) {
    if (!klass) {
        return false;
    }
    return dumper.IsOrInheritsFrom(klass, "Transform")
        || dumper.IsOrInheritsFrom(klass, "GameObject");
}

bool IsComponentLike(Engine::UnityDumper& dumper, void* klass) {
    if (!klass) {
        return false;
    }
    return dumper.IsOrInheritsFrom(klass, "Component");
}

void* GetCoreModuleImage() {
    return Engine::Unity.FindImage("UnityEngine.CoreModule");
}

void* GetClass(void* image, const char* ns, const char* name) {
    if (!image) {
        return nullptr;
    }
    const auto& exp = Engine::Unity.module.exports;
    if (!exp.fnGetClass) {
        return nullptr;
    }
    return exp.fnGetClass(image, ns, name);
}

void* GetMethodOnClass(void* klass, const char* name, int argc) {
    if (!klass) {
        return nullptr;
    }
    const auto& exp = Engine::Unity.module.exports;
    if (!exp.fnGetMethod) {
        return nullptr;
    }
    return exp.fnGetMethod(klass, name, argc);
}

bool InvokeGetter(void* method, void* instance, void** outRet) {
    if (!method || !instance) {
        return false;
    }
    void* exc = nullptr;
    void* ret = nullptr;
    if (!Engine::Unity.invoker.InvokeWithSEH(method, instance, nullptr, &exc, ret)) {
        return false;
    }
    if (exc) {
        return false;
    }
    *outRet = ret;
    return true;
}

bool TryUnboxVec3(void* boxed, Vec3f& out) {
    if (!boxed) {
        return false;
    }
    if (!Engine::Memory::IsReadablePointer(
            reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(boxed) + kBoxedValueOffset),
            sizeof(Vec3f))) {
        return false;
    }
    std::memcpy(&out, static_cast<uint8_t*>(boxed) + kBoxedValueOffset, sizeof(Vec3f));
    return true;
}

bool TryUnboxBool(void* boxed, bool& out) {
    if (!boxed) {
        return false;
    }
    if (!Engine::Memory::IsReadablePointer(
            reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(boxed) + kBoxedValueOffset),
            sizeof(uint8_t))) {
        return false;
    }
    uint8_t raw = 0;
    std::memcpy(&raw, static_cast<uint8_t*>(boxed) + kBoxedValueOffset, sizeof(uint8_t));
    out = raw != 0;
    return true;
}

void* ResolveTransformForProps(Engine::UnityDumper& dumper, void* instance) {
    if (!instance) {
        return nullptr;
    }
    void* klass = nullptr;
    dumper.TryGetClassNameFromInstance(instance, &klass);
    if (!klass) {
        return nullptr;
    }
    if (dumper.IsOrInheritsFrom(klass, "Transform")) {
        return instance;
    }

    void* image          = GetCoreModuleImage();
    void* componentKlass = GetClass(image, "UnityEngine", "Component");
    void* mTransform     = GetMethodOnClass(componentKlass, "get_transform", 0);
    if (!mTransform) {
        return nullptr;
    }
    void* ret = nullptr;
    if (!InvokeGetter(mTransform, instance, &ret)) {
        return nullptr;
    }
    return ret;
}

void* ResolveGameObjectForActive(void* instance, Engine::UnityDumper& dumper) {
    if (!instance) {
        return nullptr;
    }
    void* klass = nullptr;
    dumper.TryGetClassNameFromInstance(instance, &klass);
    if (!klass) {
        return nullptr;
    }
    if (dumper.IsOrInheritsFrom(klass, "GameObject")) {
        return instance;
    }
    void* image          = GetCoreModuleImage();
    void* componentKlass = GetClass(image, "UnityEngine", "Component");
    void* mGo            = GetMethodOnClass(componentKlass, "get_gameObject", 0);
    if (!mGo) {
        return nullptr;
    }
    void* ret = nullptr;
    if (!InvokeGetter(mGo, instance, &ret)) {
        return nullptr;
    }
    return ret;
}

bool InstanceReadable(void* instance) {
    if (!instance) {
        return false;
    }
    uintptr_t probe = 0;
    return Engine::Memory::TryReadValue(reinterpret_cast<uintptr_t>(instance), probe);
}

void AddSourceIfNew(std::vector<TransformSource>& sources, TransformSource row) {
    if (!row.instancePtr) {
        return;
    }
    for (const auto& existing : sources) {
        if (existing.instancePtr == row.instancePtr
            && existing.kind == row.kind
            && existing.label == row.label) {
            return;
        }
    }
    sources.push_back(std::move(row));
}

void PopulateCacheFromSource(TransformModel& model,
                             const TransformSource& source,
                             const std::shared_ptr<Engine::UnityDumper>& dumper) {
    if (!dumper || !InstanceReadable(source.instancePtr)) {
        model.stale.store(true);
        model.fetched.store(false);
        model.pendingFetch.store(false);
        return;
    }

    void* transformPtr = ResolveTransformForProps(*dumper, source.instancePtr);
    void* goPtr        = source.isGameObject
        ? source.instancePtr
        : ResolveGameObjectForActive(source.instancePtr, *dumper);

    Vec3f pos{};
    Vec3f euler{};
    Vec3f scale{};
    Vec3f world{};
    bool  active    = false;
    bool  hasActive = false;
    std::string objName{};
    void* parentInst = nullptr;
    void* parentKl   = nullptr;
    std::string parentNm{};

    void* image          = GetCoreModuleImage();
    void* transformKlass = GetClass(image, "UnityEngine", "Transform");
    void* objectKlass    = GetClass(image, "UnityEngine", "Object");
    void* goKlass        = GetClass(image, "UnityEngine", "GameObject");

    if (transformPtr && transformKlass) {
        void* ret = nullptr;
        if (InvokeGetter(GetMethodOnClass(transformKlass, "get_localPosition", 0), transformPtr, &ret)) {
            TryUnboxVec3(ret, pos);
        }
        ret = nullptr;
        if (InvokeGetter(GetMethodOnClass(transformKlass, "get_localEulerAngles", 0), transformPtr, &ret)) {
            TryUnboxVec3(ret, euler);
        }
        ret = nullptr;
        if (InvokeGetter(GetMethodOnClass(transformKlass, "get_localScale", 0), transformPtr, &ret)) {
            TryUnboxVec3(ret, scale);
        }
        ret = nullptr;
        if (InvokeGetter(GetMethodOnClass(transformKlass, "get_position", 0), transformPtr, &ret)) {
            TryUnboxVec3(ret, world);
        }
        ret = nullptr;
        if (InvokeGetter(GetMethodOnClass(transformKlass, "get_parent", 0), transformPtr, &ret)) {
            parentInst = ret;
            if (parentInst) {
                parentNm = dumper->TryGetClassNameFromInstance(parentInst, &parentKl);
            }
        }
    }

    if (objectKlass) {
        void* ret = nullptr;
        if (InvokeGetter(GetMethodOnClass(objectKlass, "get_name", 0), source.instancePtr, &ret) && ret) {
            std::string decoded = Engine::Decode::DecodeManagedStringFromObject(
                reinterpret_cast<uintptr_t>(ret));
            if (!decoded.empty() && decoded.front() == '"' && decoded.back() == '"') {
                objName = decoded.substr(1, decoded.size() - 2);
            }
            else {
                objName = std::move(decoded);
            }
        }
    }

    if (goPtr && goKlass) {
        void* ret = nullptr;
        if (InvokeGetter(GetMethodOnClass(goKlass, "get_activeSelf", 0), goPtr, &ret)) {
            hasActive = TryUnboxBool(ret, active);
        }
    }

    {
        std::lock_guard<std::mutex> lock(model.cacheMutex);
        model.name          = std::move(objName);
        model.localPosition = pos;
        model.localEuler    = euler;
        model.localScale    = scale;
        model.worldPosition = world;
        model.parentPtr     = parentInst;
        model.parentKlass   = parentKl;
        model.parentName    = std::move(parentNm);
        model.activeSelf    = active;
        model.hasActiveSelf = hasActive;
    }
    model.cacheGeneration.fetch_add(1, std::memory_order_relaxed);
    model.stale.store(false);
    model.fetched.store(true);
    model.pendingFetch.store(false);
}
} // namespace Gui::State::TransformDetail

#endif // ENABLE_DUMPER
