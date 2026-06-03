#pragma once

#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/state/transform/transform_model.h"
#include "unity_dumper.h"

#include <chrono>
#include <memory>
#include <vector>

namespace Gui::State::TransformDetail
{
constexpr size_t kBoxedValueOffset = 2 * sizeof(void*);
constexpr auto kLivePollInterval   = std::chrono::milliseconds(1000); // 1 Hz

bool IsTransformRelatedKlass(Engine::UnityDumper& dumper, void* klass);
bool IsComponentLike(Engine::UnityDumper& dumper, void* klass);

void* GetCoreModuleImage();
void* GetClass(void* image, const char* ns, const char* name);
void* GetMethodOnClass(void* klass, const char* name, int argc);

bool InvokeGetter(void* method, void* instance, void** outRet);
bool TryUnboxVec3(void* boxed, Vec3f& out);
bool TryUnboxBool(void* boxed, bool& out);

void* ResolveTransformForProps(Engine::UnityDumper& dumper, void* instance);
void* ResolveGameObjectForActive(void* instance, Engine::UnityDumper& dumper);

bool InstanceReadable(void* instance);
void AddSourceIfNew(std::vector<TransformSource>& sources, TransformSource row);

void PopulateCacheFromSource(TransformModel& model,
                             const TransformSource& source,
                             const std::shared_ptr<Engine::UnityDumper>& dumper);
} // namespace Gui::State::TransformDetail

#endif // ENABLE_DUMPER
