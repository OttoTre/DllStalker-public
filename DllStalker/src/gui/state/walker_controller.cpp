#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/session_state.h"

#include "types/memory_guard.h"
#include "types/type_classifier.h"

namespace Gui
{
// ==== Recursive Memory Walker ========================================
//
// The Walker treats inspector views as a stack: the bottom is the root
// instance the user picked from the candidates combobox, and each push is a
// pointer-typed field click. The current view is always the top of the
// stack. NavigateBackTo truncates; ResetNavigationStack clears.

bool ControlPanelSessionState::NavigateIntoPointer(uintptr_t fieldValueAddress, std::string fieldLabel) {
    if (!dumper || fieldValueAddress == 0) {
        return false;
    }

    // Read the field's stored value (= a pointer to a managed object).
    // TryReadValue handles unreadable pages without faulting.
    uintptr_t nestedInstanceRaw = 0;
    if (!Engine::Memory::TryReadValue<uintptr_t>(fieldValueAddress, nestedInstanceRaw)) {
        return false;
    }
    if (nestedInstanceRaw == 0) {
        return false;
    }

    void* nestedInstance = reinterpret_cast<void*>(nestedInstanceRaw);
    void* nestedKlass = nullptr;

    // Single source of truth for "is this a real managed object?". A null
    // class name means either the pointer didn't decode to anything, or
    // the engine doesn't recognise the klass header (e.g. a mis-classified
    // enum field whose integer happens to look like a pointer).
    const std::string nestedClassName = dumper->TryGetClassNameFromInstance(nestedInstance, &nestedKlass);
    if (nestedClassName.empty() || !nestedKlass) {
        return false;
    }

    // Children are labelled with the field name that led there; the root
    // (seeded by SelectInstanceByIndex) is labelled with the class name.
    const std::string drillLabel = fieldLabel.empty() ? nestedClassName : fieldLabel;
    InspectorBreadcrumb step{};
    step.klass = nestedKlass;
    step.instance = nestedInstance;
    step.label = drillLabel;
    navigationStack.push_back(std::move(step));

    selectedClass = nestedKlass;
    StartInspectorLoadAtInstance(dumper, nestedKlass, nestedInstance);
    RecordNavigationEvent(("Drill: " + drillLabel).c_str());
    return true;
}

bool ControlPanelSessionState::NavigateIntoCollection(const Engine::FieldInfo& field) {
    if (!dumper || !field.hasValue || field.valueAddress == 0) {
        return false;
    }

    const auto category = Engine::Types::GetCategory(field.type);
    if (category != Engine::Types::TypeCategory::ARRAY
        && category != Engine::Types::TypeCategory::LIST) {
        return false;
    }

    // Probe by actually synthesizing the view; if the dumper returns an
    // empty vector the header was unreadable / the pointer was null and
    // we don't push anything (same shape as NavigateIntoPointer's check).
    auto previewRows = dumper->GetCollectionView(field);
    if (previewRows.empty()) {
        return false;
    }

    // Inherit (klass, instance) from the topmost non-collection breadcrumb
    // so a back-jump knows where the collection lives. If the stack is
    // empty (shouldn't normally happen — EnsureRootBreadcrumb runs every
    // frame) we still push a self-contained breadcrumb so back navigation
    // doesn't crash; it just won't have a parent to fall back to.
    void* parentKlass = selectedClass;
    void* parentInstance = nullptr;
    {
        std::lock_guard<std::mutex> lock(inspectorCacheMutex);
        parentInstance = inspectorCache.activeInstancePtr;
    }

    InspectorBreadcrumb step{};
    step.klass        = parentKlass;
    step.instance     = parentInstance;
    step.label        = field.name + " " + (field.valueDisplay.empty() ? std::string("[]") : field.valueDisplay);
    step.isCollection = true;
    step.sourceField  = field;
    navigationStack.push_back(std::move(step));

    StartCollectionLoad(dumper, field);
    RecordNavigationEvent(("Collection: " + field.name).c_str());
    return true;
}

void ControlPanelSessionState::NavigateBackTo(size_t breadcrumbIndex) {
    if (breadcrumbIndex >= navigationStack.size()) {
        return;
    }
    if (breadcrumbIndex + 1 == navigationStack.size()) {
        return; // already at this level
    }

    navigationStack.resize(breadcrumbIndex + 1);

    // Defensive re-validation: by the time the user clicks back, the target
    // instance might have been freed by the runtime. Pop any tail levels
    // whose instance no longer decodes to a valid managed object so the
    // user lands on a still-live ancestor instead of a dangling pointer.
    //
    // Collection breadcrumbs share their parent's instance pointer, so the
    // same liveness probe works for both shapes — the only difference is
    // that we then re-load via StartCollectionLoad rather than
    // StartInspectorLoadAtInstance.
    if (dumper) {
        while (!navigationStack.empty()) {
            void* probeKlass = nullptr;
            const std::string probeName = dumper->TryGetClassNameFromInstance(navigationStack.back().instance, &probeKlass);
            if (!probeName.empty() && probeKlass) {
                break;
            }
            navigationStack.pop_back();
        }
    }

    if (navigationStack.empty()) {
        // Every breadcrumb on the way back was stale. Best we can do is
        // drop the inspector entirely; the user will need to re-pick a
        // root instance from the candidates combobox.
        selectedClass = nullptr;
        ClearInspectorCache();
        return;
    }

    const InspectorBreadcrumb& live = navigationStack.back();
    selectedClass = live.klass;
    if (live.isCollection) {
        StartCollectionLoad(dumper, live.sourceField);
    }
    else {
        StartInspectorLoadAtInstance(dumper, live.klass, live.instance);
    }

    RecordNavigationEvent("Breadcrumb back");
}

void ControlPanelSessionState::ResetNavigationStack() {
    navigationStack.clear();
}

void ControlPanelSessionState::EnsureRootBreadcrumb() {
    if (!navigationStack.empty()) {
        return;
    }

    // Snapshot the active (klass, instance) pair under the inspector mutex,
    // then drop the lock before resolving the class name.
    void* activeInstance = nullptr;
    void* activeKlass = nullptr;
    {
        std::lock_guard<std::mutex> lock(inspectorCacheMutex);
        activeInstance = inspectorCache.activeInstancePtr;
        activeKlass = inspectorCache.activeClassPtr;
    }

    if (!activeInstance || !activeKlass) {
        return;
    }

    std::string label;
    if (dumper) {
        label = dumper->TryGetClassNameFromInstance(activeInstance, nullptr);
    }
    if (label.empty()) {
        label = "<root>";
    }

    InspectorBreadcrumb root{};
    root.klass = activeKlass;
    root.instance = activeInstance;
    root.label = std::move(label);
    navigationStack.push_back(std::move(root));
}

void ControlPanelSessionState::SelectInstanceByIndex(int index) {
    void* pickedInstance = nullptr;
    void* pickedKlass = nullptr;

    {
        std::lock_guard<std::mutex> lock(inspectorCacheMutex);
        // Switching the active instance invalidates per-field input buffers
        // because their value addresses are derived from the previous
        // instance pointer.
        editBuffers.clear();

        if (index < 0 || index >= static_cast<int>(inspectorCache.instanceCandidates.size())) {
            selectedInstanceIndex = -1;
            inspectorCache.activeInstancePtr = nullptr;
        }
        else {
            selectedInstanceIndex = index;
            inspectorCache.activeInstancePtr = inspectorCache.instanceCandidates[index];
            pickedInstance = inspectorCache.activeInstancePtr;
            pickedKlass = inspectorCache.activeClassPtr;
        }
    }

    if (!pickedInstance || !pickedKlass) {
        ResetNavigationStack();
        return;
    }

    // Resolve the root label from the live instance header so the breadcrumb
    // shows the concrete runtime type even if the candidate list reported a
    // base class.
    std::string rootLabel;
    if (dumper) {
        rootLabel = dumper->TryGetClassNameFromInstance(pickedInstance, nullptr);
    }
    if (rootLabel.empty()) {
        rootLabel = "<root>";
    }

    InspectorBreadcrumb root{};
    root.klass = pickedKlass;
    root.instance = pickedInstance;
    root.label = std::move(rootLabel);
    navigationStack.assign({ std::move(root) });
    RecordNavigationEvent("Select instance");
}
} // namespace Gui

#endif // ENABLE_DUMPER
