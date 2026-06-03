#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/state/navigation/history_validation.h"

#include "unity_dumper.h"

namespace Gui::State::HistoryValidation
{
bool ValidateInstance(void* instance,
                      const std::shared_ptr<Engine::UnityDumper>& dumper,
                      void** outKlass) {
    if (!instance || !dumper) {
        return false;
    }
    void* klass = nullptr;
    const std::string name = dumper->TryGetClassNameFromInstance(instance, &klass);
    if (name.empty() || !klass) {
        return false;
    }
    if (outKlass) {
        *outKlass = klass;
    }
    return true;
}

BreadcrumbValidationResult ValidateBreadcrumbStack(
    const std::vector<InspectorBreadcrumb>& breadcrumbs,
    const std::shared_ptr<Engine::UnityDumper>& dumper,
    size_t* outFailedIndex) {
    if (!dumper) {
        if (outFailedIndex) {
            *outFailedIndex = 0;
        }
        return BreadcrumbValidationResult::StaleInstance;
    }

    for (size_t i = 0; i < breadcrumbs.size(); ++i) {
        const auto& step = breadcrumbs[i];
        if (!step.instance) {
            if (outFailedIndex) {
                *outFailedIndex = i;
            }
            return BreadcrumbValidationResult::StaleInstance;
        }
        void* probeKlass = nullptr;
        const std::string probeName = dumper->TryGetClassNameFromInstance(step.instance, &probeKlass);
        if (probeName.empty() || !probeKlass) {
            if (outFailedIndex) {
                *outFailedIndex = i;
            }
            return (i == 0) ? BreadcrumbValidationResult::StaleInstance
                              : BreadcrumbValidationResult::StaleBreadcrumb;
        }
    }

    return BreadcrumbValidationResult::Valid;
}
} // namespace Gui::State::HistoryValidation

#endif // ENABLE_DUMPER
