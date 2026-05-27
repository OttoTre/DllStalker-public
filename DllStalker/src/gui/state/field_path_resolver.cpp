#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/state/field_path_resolver.h"

#include "unity_dumper.h"

#include "types/memory_guard.h"
#include "types/type_classifier.h"

#include <cstdio>

namespace Gui::State
{
namespace
{
const Engine::FieldInfo* FindFieldByName(const std::vector<Engine::FieldInfo>& fields,
                                         const std::string& name) {
    for (const auto& field : fields) {
        if (field.name == name) {
            return &field;
        }
    }
    return nullptr;
}

bool ReadManagedPointer(uintptr_t address, void*& outInstance) {
    uintptr_t raw = 0;
    if (!Engine::Memory::TryReadValue(address, raw) || raw == 0) {
        return false;
    }
    outInstance = reinterpret_cast<void*>(raw);
    return true;
}
} // namespace

std::vector<FieldPathStep> BuildFieldPathSteps(const std::vector<InspectorBreadcrumb>& stack) {
    std::vector<FieldPathStep> steps;
    if (stack.size() <= 1) {
        return steps;
    }

    steps.reserve(stack.size() - 1);
    for (size_t i = 1; i < stack.size(); ++i) {
        const auto& crumb = stack[i];
        FieldPathStep step{};
        step.isCollection = crumb.isCollection;
        if (crumb.isCollection) {
            step.fieldName  = crumb.sourceField.name;
            step.sourceField = crumb.sourceField;
        }
        else {
            step.fieldName = crumb.label;
        }
        steps.push_back(std::move(step));
    }
    return steps;
}

std::string BuildFieldPathLabel(const std::vector<InspectorBreadcrumb>& stack,
                                int collectionLeafIndex) {
    if (stack.empty()) {
        return {};
    }

    std::string label;
    for (size_t i = 0; i < stack.size(); ++i) {
        if (i > 0) {
            label += " > ";
        }
        label += stack[i].label.empty() ? "<?>"
                                        : stack[i].label;
    }

    if (collectionLeafIndex >= 0
        && !stack.empty()
        && stack.back().isCollection) {
        char indexBuf[32] = {};
        snprintf(indexBuf, sizeof(indexBuf), " > [%d]", collectionLeafIndex);
        label += indexBuf;
    }

    return label;
}

FieldPathCompareResult ResolvePathValue(Engine::UnityDumper& dumper,
                                        void* sidebarKlass,
                                        void* rootInstance,
                                        const std::vector<FieldPathStep>& steps,
                                        int collectionLeafIndex,
                                        const std::string& leafFieldName) {
    FieldPathCompareResult result{};

    if (!rootInstance) {
        result.error = "Root instance is null.";
        return result;
    }

    void* currentInstance = rootInstance;
    void* currentKlass    = sidebarKlass;

    void* probeKlass = nullptr;
    const std::string rootName = dumper.TryGetClassNameFromInstance(rootInstance, &probeKlass);
    if (!rootName.empty() && probeKlass) {
        currentKlass = probeKlass;
    }

    for (size_t i = 0; i < steps.size(); ++i) {
        const auto& step = steps[i];
        const bool isLastStep = (i + 1 == steps.size());

        if (!currentKlass || !currentInstance) {
            result.error = "Navigation context lost (null instance).";
            return result;
        }

        if (step.isCollection) {
            const auto fields = dumper.GetRawFields(currentKlass, currentInstance);
            const Engine::FieldInfo* field = FindFieldByName(fields, step.fieldName);
            if (!field) {
                result.error = "Collection field not found: " + step.fieldName;
                return result;
            }

            auto rows = dumper.GetCollectionView(*field);
            if (rows.empty()) {
                result.error = "Collection is empty or unreadable: " + step.fieldName;
                return result;
            }

            if (isLastStep) {
                if (collectionLeafIndex < 0
                    || collectionLeafIndex >= static_cast<int>(rows.size())) {
                    result.error = "Element index out of range.";
                    return result;
                }
                result.valueDisplay = rows[static_cast<size_t>(collectionLeafIndex)].valueDisplay;
                result.ok             = true;
                return result;
            }

            result.error = "Collection step is only supported as the final path segment in v1.";
            return result;
        }

        const auto fields = dumper.GetRawFields(currentKlass, currentInstance);
        const Engine::FieldInfo* field = FindFieldByName(fields, step.fieldName);
        if (!field || !field->hasValue || field->valueAddress == 0) {
            result.error = "Field not found or unreadable: " + step.fieldName;
            return result;
        }

        const auto fieldCategory = Engine::Types::GetCategory(field->type);
        const bool isPointerField =
            fieldCategory == Engine::Types::TypeCategory::PTR
            || fieldCategory == Engine::Types::TypeCategory::ARRAY
            || fieldCategory == Engine::Types::TypeCategory::LIST;

        if (isLastStep && !isPointerField) {
            result.valueDisplay  = field->valueDisplay;
            result.leafFieldName = field->name;
            result.ok            = true;
            return result;
        }

        if (isLastStep && isPointerField && leafFieldName.empty()) {
            result.error = "Select a member field for the current object.";
            return result;
        }

        void* nestedInstance = nullptr;
        if (!ReadManagedPointer(field->valueAddress, nestedInstance)) {
            result.error = "Field is not a readable managed reference: " + step.fieldName;
            return result;
        }

        void* nestedKlass = nullptr;
        if (dumper.TryGetClassNameFromInstance(nestedInstance, &nestedKlass).empty() || !nestedKlass) {
            result.error = "Nested instance is no longer valid: " + step.fieldName;
            return result;
        }

        currentInstance = nestedInstance;
        currentKlass    = nestedKlass;
    }

    if (!leafFieldName.empty()) {
        const auto fields = dumper.GetRawFields(currentKlass, currentInstance);
        const Engine::FieldInfo* field = FindFieldByName(fields, leafFieldName);
        if (!field) {
            result.error = "Member field not found: " + leafFieldName;
            return result;
        }
        result.valueDisplay  = field->valueDisplay;
        result.leafFieldName = leafFieldName;
        result.ok            = true;
        return result;
    }

    result.error = "Specify a member field or drill to a scalar/pointer leaf.";
    return result;
}
} // namespace Gui::State

#endif // ENABLE_DUMPER
