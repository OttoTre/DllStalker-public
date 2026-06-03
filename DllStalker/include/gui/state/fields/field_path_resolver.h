#pragma once

#include "pch.h"

#ifdef ENABLE_DUMPER

#include <string>
#include <vector>

#include "gui/state/navigation/walker_controller.h"

namespace Engine
{
class UnityDumper;
}

namespace Gui::State
{
struct FieldPathStep {
    std::string fieldName{};
    bool        isCollection = false;
    Engine::FieldInfo sourceField{};
};

struct FieldPathCompareResult {
    bool        ok = false;
    std::string error{};
    std::string valueDisplay{};
    std::string pathLabel{};
    std::string leafFieldName{};
};

// Steps from navigationStack[1..] (skip root). collectionLeafIndex applies when
// the top breadcrumb is a collection view.
std::vector<FieldPathStep> BuildFieldPathSteps(const std::vector<InspectorBreadcrumb>& stack);

std::string BuildFieldPathLabel(const std::vector<InspectorBreadcrumb>& stack,
                                int collectionLeafIndex);

FieldPathCompareResult ResolvePathValue(Engine::UnityDumper& dumper,
                                        void* sidebarKlass,
                                        void* rootInstance,
                                        const std::vector<FieldPathStep>& steps,
                                        int collectionLeafIndex,
                                        const std::string& leafFieldName);
} // namespace Gui::State

#endif // ENABLE_DUMPER
