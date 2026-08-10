#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/session_state.h"

#include "dumper/value_search/value_search_element_leaves.h"
#include "dumper/value_search/value_search_match.h"
#include "dumper/value_search/value_search_nested_leaves.h"
#include "dumper/value_search/value_search_ptr_follow.h"
#include "types/memory_guard.h"
#include "types/type_classifier.h"

namespace Gui
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

bool AssignRootBreadcrumb(ControlPanelSessionState& state, void* klass, void* instance) {
    if (!klass || !instance) {
        return false;
    }
    std::string rootLabel;
    if (state.dumper) {
        rootLabel = state.dumper->TryGetClassNameFromInstance(instance, nullptr);
    }
    if (rootLabel.empty()) {
        rootLabel = "<root>";
    }
    InspectorBreadcrumb root{};
    root.klass = klass;
    root.instance = instance;
    root.label = std::move(rootLabel);
    state.walker.stack.assign({ std::move(root) });
    state.selectedClass = klass;
    return true;
}

// Push pointer crumb without load/history (same resolve as NavigateIntoPointer).
bool PushPointerCrumb(ControlPanelSessionState& state,
                      uintptr_t fieldValueAddress,
                      const std::string& fieldLabel) {
    if (!state.dumper || fieldValueAddress == 0) {
        return false;
    }
    uintptr_t nestedInstanceRaw = 0;
    if (!Engine::Memory::TryReadValue<uintptr_t>(fieldValueAddress, nestedInstanceRaw)
        || nestedInstanceRaw == 0) {
        return false;
    }
    void* nestedInstance = reinterpret_cast<void*>(nestedInstanceRaw);
    void* nestedKlass = nullptr;
    const std::string nestedClassName =
        state.dumper->TryGetClassNameFromInstance(nestedInstance, &nestedKlass);
    if (nestedClassName.empty() || !nestedKlass) {
        return false;
    }
    const std::string drillLabel = fieldLabel.empty() ? nestedClassName : fieldLabel;
    InspectorBreadcrumb step{};
    step.klass = nestedKlass;
    step.instance = nestedInstance;
    step.label = drillLabel;
    state.walker.stack.push_back(std::move(step));
    state.selectedClass = nestedKlass;
    return true;
}

// Push collection crumb without load/history (same probe as NavigateIntoCollection).
bool PushCollectionCrumb(ControlPanelSessionState& state,
                         const Engine::FieldInfo& field,
                         void* parentKlass,
                         void* parentInstance) {
    if (!state.dumper || !field.hasValue || field.valueAddress == 0) {
        return false;
    }
    const auto category = Engine::Types::GetCategory(field.type);
    if (category != Engine::Types::TypeCategory::ARRAY
        && category != Engine::Types::TypeCategory::LIST) {
        return false;
    }
    auto previewRows = state.dumper->GetCollectionView(field);
    if (previewRows.empty()) {
        return false;
    }
    InspectorBreadcrumb step{};
    step.klass = parentKlass;
    step.instance = parentInstance;
    step.label = field.name + " " + (field.valueDisplay.empty() ? std::string("[]") : field.valueDisplay);
    step.isCollection = true;
    step.sourceField = field;
    state.walker.stack.push_back(std::move(step));
    return true;
}

bool TryPushCollectionElementObject(ControlPanelSessionState& state,
                                    const Engine::FieldInfo& elementSlot) {
    using Cat = Engine::Types::TypeCategory;
    const auto category = Engine::Types::GetCategory(elementSlot.type);
    if (category != Cat::PTR || elementSlot.isEnum) {
        return false;
    }
    if (!elementSlot.hasValue || elementSlot.valueAddress == 0) {
        return false;
    }
    if (elementSlot.valueDisplay == "null"
        || elementSlot.valueDisplay == "[null]"
        || elementSlot.valueDisplay == "??") {
        return false;
    }
    return PushPointerCrumb(state, elementSlot.valueAddress, elementSlot.name);
}

bool TryOpenCollectionElementPath(ControlPanelSessionState& state,
                                  void* ownerKlass,
                                  void* ownerInstance,
                                  const std::vector<Engine::FieldInfo>& rawFields,
                                  const std::string& containerName,
                                  size_t index,
                                  bool preferElementObject) {
    const Engine::FieldInfo* container = FindFieldByName(rawFields, containerName);
    if (!container
        || !Engine::Dumper::IsValueSearchCollectionCategory(
            Engine::Types::GetCategory(container->type))) {
        return false;
    }
    if (!PushCollectionCrumb(state, *container, ownerKlass, ownerInstance)) {
        return false;
    }
    std::vector<Engine::FieldInfo> elements;
    try {
        elements = state.dumper->GetCollectionView(*container);
    }
    catch (...) {
        return false;
    }
    if (index >= elements.size()) {
        return false;
    }
    if (preferElementObject) {
        (void)TryPushCollectionElementObject(state, elements[index]);
    }
    return true;
}

// Drill-aligned path walk at (klass, instance). Returns false if unresolved.
bool ApplySearchHitPath(ControlPanelSessionState& state,
                        void* klass,
                        void* instance,
                        const std::string& path) {
    if (!state.dumper || !klass || !instance || path.empty()) {
        return false;
    }

    std::vector<Engine::FieldInfo> rawFields;
    try {
        rawFields = state.dumper->GetRawFields(klass, instance);
    }
    catch (...) {
        return false;
    }

    Engine::FieldInfo leafStorage{};
    // 1) Top-level field or allowlisted Parent.Child leaf → stay here.
    if (Engine::Dumper::ResolveFieldOrNestedLeaf(rawFields, path, leafStorage)) {
        return true;
    }

    // 2) Follow PTR: ptr.nested / ptr.arr[i] / ptr.arr[i].member
    //    (only when first segment is a real followable PTR field name).
    {
        std::string ptrName;
        std::string nestedPath;
        if (Engine::Dumper::ParsePtrFollowName(path, ptrName, nestedPath)
            && nestedPath != "*") {
            const Engine::FieldInfo* ptrField = FindFieldByName(rawFields, ptrName);
            if (ptrField && Engine::Dumper::IsFollowablePtrField(*ptrField)) {
                if (!PushPointerCrumb(state, ptrField->valueAddress, ptrField->name)) {
                    return false;
                }
                const InspectorBreadcrumb& top = state.walker.stack.back();
                return ApplySearchHitPath(state, top.klass, top.instance, nestedPath);
            }
        }
    }

    // 3) Deep interior: container[i].member
    {
        std::string containerName;
        size_t index = 0;
        std::string memberName;
        if (Engine::Dumper::ParseCollectionElementInteriorName(
                path, containerName, index, memberName)) {
            (void)memberName;
            // Prefer landing on the element object when it is a managed ref
            // so member fields (e.g. hp) appear in the Fields tab.
            return TryOpenCollectionElementPath(
                state, klass, instance, rawFields, containerName, index,
                /*preferElementObject=*/true);
        }
    }

    // 4) Collection element slot: container[i]
    {
        std::string containerName;
        size_t index = 0;
        if (Engine::Dumper::ParseCollectionElementName(path, containerName, index)) {
            return TryOpenCollectionElementPath(
                state, klass, instance, rawFields, containerName, index,
                /*preferElementObject=*/true);
        }
    }

    return false;
}

void LoadWalkerStackTop(ControlPanelSessionState& state) {
    if (state.walker.stack.empty() || !state.dumper) {
        return;
    }
    const InspectorBreadcrumb& top = state.walker.stack.back();
    state.selectedClass = top.klass;
    if (top.isCollection) {
        state.StartCollectionLoad(state.dumper, top.sourceField, top.klass, top.instance);
    }
    else {
        state.StartInspectorLoadAtInstance(state.dumper, top.klass, top.instance);
    }
}
} // namespace

// ==== Recursive Memory Walker ========================================
// Stack of inspector views: root instance at bottom, pointer field clicks push.
// NavigateBackTo truncates; ResetNavigationStack clears.

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
    walker.stack.push_back(std::move(step));

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
        std::lock_guard<std::mutex> lock(inspector.mutex);
        parentInstance = inspector.cache.activeInstancePtr;
    }

    InspectorBreadcrumb step{};
    step.klass        = parentKlass;
    step.instance     = parentInstance;
    step.label        = field.name + " " + (field.valueDisplay.empty() ? std::string("[]") : field.valueDisplay);
    step.isCollection = true;
    step.sourceField  = field;
    walker.stack.push_back(std::move(step));

    StartCollectionLoad(dumper, field, parentKlass, parentInstance);
    RecordNavigationEvent(("Collection: " + field.name).c_str());
    return true;
}

bool ControlPanelSessionState::NavigateToValueSearchHit(const Engine::ValueSearchHit& hit) {
    if (!dumper || !hit.klass || !hit.instance) {
        return false;
    }

    auto loadRootFallback = [this, &hit]() {
        AssignRootBreadcrumb(*this, hit.klass, hit.instance);
        StartInspectorLoadAtInstance(dumper, hit.klass, hit.instance);
        const std::string label = hit.fieldName.empty()
            ? std::string("Search hit")
            : ("Search: " + hit.fieldName);
        RecordNavigationEvent(label.c_str());
    };

    // Always replace the walker stack so a prior Fields walk does not linger.
    if (!AssignRootBreadcrumb(*this, hit.klass, hit.instance)) {
        return false;
    }

    if (hit.fieldName.empty()) {
        LoadWalkerStackTop(*this);
        RecordNavigationEvent("Search hit");
        return true;
    }

    if (!ApplySearchHitPath(*this, hit.klass, hit.instance, hit.fieldName)) {
        loadRootFallback();
        return true;
    }

    LoadWalkerStackTop(*this);
    RecordNavigationEvent(("Search: " + hit.fieldName).c_str());
    return true;
}

void ControlPanelSessionState::NavigateBackTo(size_t breadcrumbIndex) {
    if (breadcrumbIndex >= walker.stack.size()) {
        return;
    }
    if (breadcrumbIndex + 1 == walker.stack.size()) {
        return; // already at this level
    }

    walker.stack.resize(breadcrumbIndex + 1);

    // Pop tail levels whose instance no longer decodes (GC / unload).
    // Collection crumbs share the parent instance — same liveness probe.
    if (dumper) {
        while (!walker.stack.empty()) {
            void* probeKlass = nullptr;
            const std::string probeName = dumper->TryGetClassNameFromInstance(walker.stack.back().instance, &probeKlass);
            if (!probeName.empty() && probeKlass) {
                break;
            }
            walker.stack.pop_back();
        }
    }

    if (walker.stack.empty()) {
        // Every breadcrumb on the way back was stale. Best we can do is
        // drop the inspector entirely; the user will need to re-pick a
        // root instance from the candidates combobox.
        selectedClass = nullptr;
        ClearInspectorCache();
        return;
    }

    const InspectorBreadcrumb& live = walker.stack.back();
    selectedClass = live.klass;
    if (live.isCollection) {
        StartCollectionLoad(dumper, live.sourceField, live.klass, live.instance);
    }
    else {
        StartInspectorLoadAtInstance(dumper, live.klass, live.instance);
    }

    RecordNavigationEvent("Breadcrumb back");
}

void ControlPanelSessionState::ResetNavigationStack() {
    walker.stack.clear();
}

void ControlPanelSessionState::EnsureRootBreadcrumb() {
    if (!walker.stack.empty()) {
        return;
    }

    // Snapshot the active (klass, instance) pair under the inspector mutex,
    // then drop the lock before resolving the class name.
    void* activeInstance = nullptr;
    void* activeKlass = nullptr;
    {
        std::lock_guard<std::mutex> lock(inspector.mutex);
        activeInstance = inspector.cache.activeInstancePtr;
        activeKlass = inspector.cache.activeClassPtr;
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
    walker.stack.push_back(std::move(root));
}

void ControlPanelSessionState::SelectInstanceByIndex(int index) {
    void* pickedInstance = nullptr;
    void* pickedKlass = nullptr;

    {
        std::lock_guard<std::mutex> lock(inspector.mutex);
        // Switching the active instance invalidates per-field input buffers
        // because their value addresses are derived from the previous
        // instance pointer.
        editBufferStore.Clear();
        enumLiteralCache.Clear();

        if (index < 0 || index >= static_cast<int>(inspector.cache.instanceCandidates.size())) {
            inspector.selectedInstanceIndex = -1;
            inspector.cache.activeInstancePtr = nullptr;
        }
        else {
            inspector.selectedInstanceIndex = index;
            inspector.cache.activeInstancePtr = inspector.cache.instanceCandidates[index];
            pickedInstance = inspector.cache.activeInstancePtr;
            pickedKlass = inspector.cache.activeClassPtr;
        }
        inspector.NoteCacheMutated();
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
    walker.stack.assign({ std::move(root) });
    RecordNavigationEvent("Select instance");
}
} // namespace Gui

#endif // ENABLE_DUMPER
