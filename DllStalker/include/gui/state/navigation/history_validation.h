#pragma once

#include "pch.h"

#include <cstddef>
#include <memory>
#include <vector>

#include "gui/state/navigation/inspector_navigation_snapshot.h"

namespace Engine
{
class UnityDumper;
} // namespace Engine

namespace Gui::State::HistoryValidation
{
bool ValidateInstance(void* instance,
                      const std::shared_ptr<Engine::UnityDumper>& dumper,
                      void** outKlass);

BreadcrumbValidationResult ValidateBreadcrumbStack(
    const std::vector<InspectorBreadcrumb>& breadcrumbs,
    const std::shared_ptr<Engine::UnityDumper>& dumper,
    size_t* outFailedIndex = nullptr);
} // namespace Gui::State::HistoryValidation
