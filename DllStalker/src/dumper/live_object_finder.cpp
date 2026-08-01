#include "pch.h"

#ifdef ENABLE_DUMPER

#include "dumper/live_object_finder.h"

#include <string>

#include "types/memory_guard.h"
#include "types/unity_array_layout.h"

namespace Engine::Dumper
{
namespace
{
static_assert(sizeof(void*) == 8, "GetLiveInstances assumes the x64 Unity array header layout");

constexpr size_t kMaxFindObjectsResultLength = 1'000'000;
} // namespace

LiveObjectFinder::LiveObjectFinder(UnityResolver& resolver)
    : m_resolver(resolver)
{
}

bool LiveObjectFinder::InitDumperExports() {
    using E = UnityExports;
    auto& exp = m_resolver.module.exports;
    const std::string prefix = m_resolver.module.isIL2CPP ? "il2cpp_" : "mono_";

    auto Resolve = [&](const char* name) {
        const std::string fullName = prefix + name;
        return GetProcAddress(m_resolver.module.hModule, fullName.c_str());
    };

    // --- Image (engine-specific) ---
    if (m_resolver.module.isIL2CPP) {
        exp.fnImageGetClassCount = (E::t_ImageGetClassCount)Resolve("image_get_class_count");
    }
    else {
        exp.fnImageGetTableInfo = (E::t_ImageGetTableInfo)Resolve("image_get_table_info");
        exp.fnTableInfoGetRows  = (E::t_TableInfoGetRows)Resolve("table_info_get_rows");
    }

    // --- Class ---
    exp.fnGetMethods              = (E::t_ClassGetMethods)Resolve("class_get_methods");
    exp.fnClassGetFields          = (E::t_ClassGetFields)Resolve("class_get_fields");
    exp.fnGetSize                 = (E::t_ClassGetInstanceSize)Resolve("class_instance_size");
    exp.fnClassGetStaticFieldsPtr = (E::t_GetStaticFieldsPtr)Resolve("class_get_static_field_data");

    // --- Method ---
    exp.fnMethodGetName       = (E::t_MethodGetName)Resolve("method_get_name");
    exp.fnMethodGetParamCount = (E::t_MethodGetParamCount)Resolve("method_get_param_count");

    // --- Field ---
    exp.fnFieldGetName  = (E::t_FieldGetName)Resolve("field_get_name");
    exp.fnFieldGetFlags = (E::t_FieldGetFlags)Resolve("field_get_flags");
    exp.fnFieldGetType  = (E::t_FieldGetType)Resolve("field_get_type");
    exp.fnTypeGetName   = (E::t_TypeGetName)Resolve("type_get_name");

    // fnMethodGetParamCount is IL2CPP-only: Unity's mono-2.0-bdwgc.dll does
    // not export "method_get_param_count". Mono reconstructs the count via 
    // the signature accessors instead (fnMonoMethodSignature + ...ParamCount).
    const bool coreValid = (exp.fnMethodGetName && exp.fnGetParent
        && exp.fnFieldGetName && exp.fnFieldGetType && exp.fnTypeGetName);
    const bool engineSpecificValid = m_resolver.module.isIL2CPP
        ? (exp.fnImageGetClassCount && exp.fnGetMethods && exp.fnMethodGetParamCount)
        : (exp.fnImageGetTableInfo && exp.fnTableInfoGetRows);

    return coreValid && engineSpecificValid;
}

void* LiveObjectFinder::GetSystemTypeForClass(void* klass) {
    if (!klass || !m_resolver.module.exports.fnClassGetType) return nullptr;

    m_resolver.module.EnsureThreadAttached();

    void* engineType = m_resolver.module.exports.fnClassGetType(klass);
    if (!engineType) return nullptr;

    void* systemType = nullptr;
    if (m_resolver.module.isIL2CPP) {
        if (!m_resolver.module.exports.fnTypeGetObject) {
            return nullptr;
        }
        systemType = m_resolver.module.exports.fnTypeGetObject(engineType);
    }
    else {
        if (!m_resolver.module.exports.fnMonoTypeGetObject || !m_resolver.module.domain) {
            return nullptr;
        }
        systemType = m_resolver.module.exports.fnMonoTypeGetObject(m_resolver.module.domain, engineType);
    }

    if (!systemType) return nullptr;

    return systemType;
}

std::vector<void*> LiveObjectFinder::GetLiveInstances(void* klass) {
    std::vector<void*> instances;
    if (!klass || !m_resolver.module.exports.fnRuntimeInvoke) {
        return instances;
    }

    m_resolver.module.EnsureThreadAttached();

    void* systemType = GetSystemTypeForClass(klass);
    if (!systemType) {
        return instances;
    }

    if (!EnsureLiveInstanceApiCached()) {
        return instances;
    }

    // Safe read after call_once initialization.
    // call_once guarantees that cached values are fully written before this read.
    void* const findMethod = m_cachedFindMethod;
    const LiveFindMode mode = m_cachedFindMode;
    if (!findMethod) {
        return instances;
    }

    void* arrayResult = nullptr;
    void* exc         = nullptr;

    if (mode == LiveFindMode::Type1) {
        void* params[1] = { systemType };
        arrayResult = m_resolver.module.exports.fnRuntimeInvoke(findMethod, nullptr, params, &exc);
    }
    else if (mode == LiveFindMode::Type2) {
        bool  inactive  = false;
        void* params[2] = { systemType, &inactive };
        arrayResult = m_resolver.module.exports.fnRuntimeInvoke(findMethod, nullptr, params, &exc);
    }
    else {
        int   inactive  = 0;
        int   sortMode  = 0;
        void* params[3] = { systemType, &inactive, &sortMode };
        arrayResult = m_resolver.module.exports.fnRuntimeInvoke(findMethod, nullptr, params, &exc);
    }

    if (exc || !arrayResult) {
        return instances;
    }

    const uintptr_t arrayBase = reinterpret_cast<uintptr_t>(arrayResult);
    size_t arraySize = 0;
    if (!Memory::TryReadValue(arrayBase + Engine::UnityArrayLayout::LengthOffset, arraySize)) {
        return instances;
    }
    if (arraySize == 0 || arraySize > kMaxFindObjectsResultLength) {
        return instances;
    }

    // FindObjectsOfType<T> always returns T[] of UnityEngine.Object subclasses,
    // i.e. an array of reference pointers at ElementsOffset.
    const uintptr_t elementsBase = arrayBase + Engine::UnityArrayLayout::ElementsOffset;
    if (!Memory::IsReadablePointer(reinterpret_cast<void*>(elementsBase),
                                   arraySize * sizeof(void*))) {
        return instances;
    }

    instances.reserve(arraySize);
    for (size_t i = 0; i < arraySize; ++i) {
        void* item = nullptr;
        if (!Memory::TryReadValue(elementsBase + i * sizeof(void*), item) || !item) {
            continue;
        }
        instances.push_back(item);
    }

    return instances;
}

bool LiveObjectFinder::EnsureLiveInstanceApiCached() {
    std::call_once(m_liveApiCacheOnce, [this] {
        void* coreModuleImage = m_resolver.FindImage("UnityEngine.CoreModule");
        if (!coreModuleImage) return;

        void* unityObjectClass = m_resolver.module.exports.fnGetClass(coreModuleImage, "UnityEngine", "Object");
        if (!unityObjectClass) return;

        struct Probe { const char* name; int args; LiveFindMode mode; };
        constexpr Probe probes[] = {
            { "FindObjectsByType", 3, LiveFindMode::ByType3 }, // 2022.2+
            { "FindObjectsOfType", 2, LiveFindMode::Type2   }, // 2020.1 - 2022.1
            { "FindObjectsOfType", 1, LiveFindMode::Type1   }, // Legacy (pre-2020.1)
        };

        for (const auto& p : probes) {
            if (void* method = m_resolver.module.exports.fnGetMethod(unityObjectClass, p.name, p.args)) {
                m_cachedFindMethod  = method;
                m_cachedFindMode    = p.mode;
                m_liveApiCacheReady = true;
                return;
            }
        }
    });

    return m_liveApiCacheReady;
}
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
