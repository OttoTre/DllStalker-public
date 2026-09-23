#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/session_state.h"

#include "dumper/instances/collection_view.h"
#include "dumper/value_search/value_search_element_leaves.h"
#include "dumper/value_search/value_search_match.h"
#include "dumper/value_search/value_search_nested_leaves.h"
#include "dumper/value_search/value_search_ptr_follow.h"
#include "gui/state/navigation/history_steady_time.h"
#include "gui/state/navigation/inspector_navigation_snapshot.h"
#include "types/memory_guard.h"
#include "types/type_classifier.h"
#include "unity_resolver.h"

#include <charconv>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace Gui
{
bool IsCustomValueTypeElementRow(const Engine::FieldInfo& field, Engine::UnityDumper& dumper) {
    if (!field.elementKlass || field.isEnum) {
        return false;
    }
    if (!dumper.IsValueTypeKlass(field.elementKlass)) {
        return false;
    }
    return !Engine::Types::IsInlineValueStruct(Engine::Types::GetCategory(field.type));
}

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

bool IsSynthesizedFieldsElementName(const std::string& name) {
    size_t index = 0;
    return Engine::Dumper::ParseSynthesizedFieldsElementIndex(name, index);
}

// Labels are "[i]" or "[i] TypeName". ParseSynthesizedFieldsElementIndex
// only accepts exact "[i]" (back()==']'). Stop at the first ']'.
bool ParseLabelBracketIndexPrefix(const std::string& label, size_t& outIndex) {
    if (label.size() < 3 || label.front() != '[') {
        return false;
    }
    const size_t close = label.find(']');
    if (close == std::string::npos || close < 2) {
        return false;
    }
    const char* begin = label.data() + 1;
    const char* end = label.data() + close;
    size_t index = 0;
    auto [ptr, ec] = std::from_chars(begin, end, index);
    if (ec != std::errc{} || ptr != end) {
        return false;
    }
    outIndex = index;
    return true;
}

void SplitClassName(const std::string& display, std::string& ns, std::string& name) {
    const size_t pos = display.rfind("::");
    if (pos == std::string::npos) {
        ns.clear();
        name = display;
        return;
    }
    ns = display.substr(0, pos);
    name = display.substr(pos + 2);
}

void SetReplayStatus(ControlPanelSessionState& state, const char* message) {
    state.navigationFeedback.MarkStatus(message, State::HistorySteadyNowSeconds());
}

void* ResolveImagePtr(const ControlPanelSessionState& state, const std::string& imageName) {
    if (imageName.empty() || imageName == "<image>") {
        return nullptr;
    }
    for (const auto& img : *state.GetImageCacheSnapshot()) {
        if (img.name == imageName) {
            return img.imagePtr;
        }
    }
    return nullptr;
}

std::string LookupImageName(const ControlPanelSessionState& state, void* imagePtr) {
    if (!imagePtr) {
        return {};
    }
    for (const auto& img : *state.GetImageCacheSnapshot()) {
        if (img.imagePtr == imagePtr) {
            return img.name;
        }
    }
    return {};
}

void SyncImageComboCaption(ControlPanelSessionState& state,
                           const std::string& imageName,
                           void* imagePtr) {
    std::string name = imageName;
    if (name.empty() || name == "<image>") {
        name = LookupImageName(state, imagePtr);
    }
    if (name.empty() || name == "<image>") {
        return;
    }
    strncpy_s(state.imgSearchBuffer, sizeof(state.imgSearchBuffer), name.c_str(), _TRUNCATE);
}

void* ResolveClassPtr(const ControlPanelSessionState& state,
                      void* imagePtr,
                      const std::string& ns,
                      const std::string& name) {
    if (!imagePtr || name.empty() || name == "<class>") {
        return nullptr;
    }

    const auto& exp = Engine::Unity.module.exports;
    if (exp.fnGetClass) {
        if (void* klass = exp.fnGetClass(imagePtr, ns.c_str(), name.c_str())) {
            return klass;
        }
    }

    if (state.selectedImage != imagePtr) {
        return nullptr;
    }
    const std::string display = ns.empty() ? name : (ns + "::" + name);
    for (const auto& cl : *state.GetClassCacheSnapshot()) {
        if (cl.ns == ns && cl.name == name) {
            return cl.klassPtr;
        }
        const std::string clDisplay = cl.ns.empty() ? cl.name : (cl.ns + "::" + cl.name);
        if (clDisplay == display) {
            return cl.klassPtr;
        }
    }
    return nullptr;
}

bool BookmarkRecipeHopsAreReplayable(const State::NavigationSnapshot& snap) {
    if (snap.breadcrumbs.size() <= 1) {
        return true;
    }
    for (size_t i = 1; i < snap.breadcrumbs.size(); ++i) {
        const InspectorBreadcrumb& hop = snap.breadcrumbs[i];
        const InspectorBreadcrumb& parent = snap.breadcrumbs[i - 1];
        if (hop.isCollection) {
            if (hop.sourceField.name.empty()
                || IsSynthesizedFieldsElementName(hop.sourceField.name)) {
                return false;
            }
            continue;
        }
        if (hop.isValueTypeSlot) {
            if (!parent.isCollection) {
                return false;
            }
            if (hop.hasValueTypeIndex) {
                continue;
            }
            size_t index = 0;
            if (ParseLabelBracketIndexPrefix(hop.label, index)) {
                continue;
            }
            return false;
        }
        if (parent.isCollection) {
            size_t index = 0;
            if (Engine::Dumper::ParseSynthesizedFieldsElementIndex(hop.label, index)) {
                continue;
            }
            if (hop.hasValueTypeIndex) {
                continue;
            }
            return false;
        }
        if (hop.sourceField.name.empty()
            || IsSynthesizedFieldsElementName(hop.sourceField.name)) {
            return false;
        }
    }
    return true;
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
    bool emptyButReadable = false;
    auto previewRows = state.dumper->GetCollectionView(field, 0, &emptyButReadable);
    if (previewRows.empty() && !emptyButReadable) {
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

bool PushValueTypeSlotCrumb(ControlPanelSessionState& state,
                            const Engine::FieldInfo& wrapperField,
                            void* elementKlass,
                            size_t index,
                            const std::string& elementTypeName) {
    if (state.walker.stack.empty()) {
        return false;
    }
    const InspectorBreadcrumb& parent = state.walker.stack.back();
    InspectorBreadcrumb step{};
    step.klass = parent.klass;
    step.instance = parent.instance;
    step.label = "[" + std::to_string(index) + "]";
    if (!elementTypeName.empty()) {
        step.label += " " + elementTypeName;
    }
    step.isCollection = false;
    step.sourceField = wrapperField;
    step.isValueTypeSlot = true;
    step.valueTypeElementKlass = elementKlass;
    step.valueTypeIndex = index;
    step.hasValueTypeIndex = true;
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
    if (preferElementObject && state.dumper) {
        Engine::FieldInfo element = elements[index];
        if (!element.elementKlass) {
            element.elementKlass = state.dumper->TryResolveCollectionElementKlass(*container);
        }
        if (IsCustomValueTypeElementRow(element, *state.dumper)) {
            (void)PushValueTypeSlotCrumb(state, *container, element.elementKlass, index, element.type);
        }
        else {
            (void)TryPushCollectionElementObject(state, element);
        }
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

bool LoadOwnerFields(ControlPanelSessionState& state,
                     void*& ownerKlass,
                     void*& ownerInstance,
                     std::vector<Engine::FieldInfo>& fields) {
    if (state.walker.stack.empty() || !state.dumper) {
        return false;
    }
    const InspectorBreadcrumb& top = state.walker.stack.back();
    try {
        if (top.isValueTypeSlot) {
            auto rows = state.dumper->GetCollectionView(top.sourceField, top.valueTypeIndex + 1);
            if (top.valueTypeIndex >= rows.size()) {
                return false;
            }
            const Engine::FieldInfo& liveRow = rows[top.valueTypeIndex];
            if (!liveRow.hasValue || liveRow.valueAddress == 0) {
                return false;
            }
            ownerKlass = liveRow.elementKlass ? liveRow.elementKlass : top.valueTypeElementKlass;
            ownerInstance = reinterpret_cast<void*>(liveRow.valueAddress);
            if (!ownerKlass || !ownerInstance) {
                return false;
            }
            fields = state.dumper->GetRawFields(ownerKlass, ownerInstance);
            return true;
        }
        ownerKlass = top.klass;
        ownerInstance = top.instance;
        if (!ownerKlass || !ownerInstance) {
            return false;
        }
        fields = state.dumper->GetRawFields(ownerKlass, ownerInstance);
        return true;
    }
    catch (...) {
        return false;
    }
}

bool ReplayOneHop(ControlPanelSessionState& state, const InspectorBreadcrumb& hop) {
    if (state.walker.stack.empty() || !state.dumper) {
        return false;
    }
    const InspectorBreadcrumb parent = state.walker.stack.back();

    if (hop.isCollection) {
        if (hop.sourceField.name.empty()
            || IsSynthesizedFieldsElementName(hop.sourceField.name)) {
            return false;
        }
        void* fieldKlass = nullptr;
        void* fieldInstance = nullptr;
        std::vector<Engine::FieldInfo> fields;
        if (!LoadOwnerFields(state, fieldKlass, fieldInstance, fields)) {
            return false;
        }
        const Engine::FieldInfo* live = FindFieldByName(fields, hop.sourceField.name);
        if (!live) {
            return false;
        }
        return PushCollectionCrumb(state, *live, parent.klass, parent.instance);
    }

    if (hop.isValueTypeSlot) {
        if (!parent.isCollection) {
            return false;
        }
        size_t index = 0;
        if (hop.hasValueTypeIndex) {
            index = hop.valueTypeIndex;
        }
        else if (!ParseLabelBracketIndexPrefix(hop.label, index)) {
            return false;
        }
        std::vector<Engine::FieldInfo> rows;
        try {
            rows = state.dumper->GetCollectionView(parent.sourceField, index + 1);
        }
        catch (...) {
            return false;
        }
        if (index >= rows.size()) {
            return false;
        }
        const Engine::FieldInfo& row = rows[index];
        void* elementKlass = row.elementKlass ? row.elementKlass : hop.valueTypeElementKlass;
        return PushValueTypeSlotCrumb(state, parent.sourceField, elementKlass,
                                      index, row.type);
    }

    if (parent.isCollection) {
        size_t index = 0;
        bool haveIndex = Engine::Dumper::ParseSynthesizedFieldsElementIndex(hop.label, index);
        if (!haveIndex && hop.hasValueTypeIndex) {
            index = hop.valueTypeIndex;
            haveIndex = true;
        }
        if (!haveIndex) {
            return false;
        }
        std::vector<Engine::FieldInfo> rows;
        try {
            rows = state.dumper->GetCollectionView(parent.sourceField);
        }
        catch (...) {
            return false;
        }
        if (index >= rows.size()) {
            return false;
        }
        return PushPointerCrumb(state, rows[index].valueAddress, rows[index].name);
    }

    if (hop.sourceField.name.empty()
        || IsSynthesizedFieldsElementName(hop.sourceField.name)) {
        return false;
    }
    void* fieldKlass = nullptr;
    void* fieldInstance = nullptr;
    std::vector<Engine::FieldInfo> fields;
    if (!LoadOwnerFields(state, fieldKlass, fieldInstance, fields)) {
        return false;
    }
    const Engine::FieldInfo* live = FindFieldByName(fields, hop.sourceField.name);
    if (!live || !live->hasValue || live->valueAddress == 0) {
        return false;
    }
    return PushPointerCrumb(state, live->valueAddress, live->name);
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
    else if (top.isValueTypeSlot) {
        state.StartValueTypeSlotLoad(state.dumper, top.sourceField, top.klass, top.instance,
                                     top.valueTypeElementKlass, top.valueTypeIndex);
    }
    else {
        state.StartInspectorLoadAtInstance(state.dumper, top.klass, top.instance);
    }
}
} // namespace

State::HistoryRestoreResult ControlPanelSessionState::TryReplayBookmarkRecipe(
    const State::NavigationSnapshot& snap) {
    if (!dumper) {
        SetReplayStatus(*this, "Dumper not initialized.");
        return State::HistoryRestoreResult::DumperUnavailable;
    }

    std::vector<void*> previousCompare;
    void* previousCompareKlass = nullptr;
    {
        std::lock_guard<std::mutex> lock(inspector.mutex);
        previousCompare = inspector.rootInstanceCandidates;
        previousCompareKlass = inspector.rootInstanceClassPtr;
        inspector.rootInstanceCandidates.clear();
        inspector.rootInstanceClassPtr = nullptr;
        inspector.NoteCacheMutated();
    }

    auto restoreCompare = [&]() {
        std::lock_guard<std::mutex> lock(inspector.mutex);
        inspector.rootInstanceCandidates = previousCompare;
        inspector.rootInstanceClassPtr = previousCompareKlass;
        inspector.NoteCacheMutated();
    };

    if (snap.breadcrumbs.size() > 1 && snap.rootClassName.empty()) {
        restoreCompare();
        SetReplayStatus(*this,
            "Bookmark path cannot be replayed — nested hops are missing the root class.");
        return State::HistoryRestoreResult::StaleBreadcrumb;
    }
    if (!BookmarkRecipeHopsAreReplayable(snap)) {
        restoreCompare();
        SetReplayStatus(*this,
            "Bookmark path cannot be replayed — nested hops are missing field names.");
        return State::HistoryRestoreResult::StaleBreadcrumb;
    }

    void* imagePtr = snap.imagePtr;
    if (!imagePtr) {
        imagePtr = ResolveImagePtr(*this, snap.imageName);
    }

    void* rootKlass = nullptr;
    if (!snap.rootClassName.empty()) {
        std::string ns;
        std::string name;
        SplitClassName(snap.rootClassName, ns, name);
        rootKlass = ResolveClassPtr(*this, imagePtr, ns, name);
    }
    else if (snap.breadcrumbs.size() <= 1) {
        if (snap.classPtr) {
            rootKlass = snap.classPtr;
        }
        else {
            std::string ns;
            std::string name;
            SplitClassName(snap.className, ns, name);
            rootKlass = ResolveClassPtr(*this, imagePtr, ns, name);
        }
    }
    else {
        restoreCompare();
        SetReplayStatus(*this,
            "Bookmark path cannot be replayed — nested hops are missing the root class.");
        return State::HistoryRestoreResult::StaleBreadcrumb;
    }

    if (!rootKlass) {
        restoreCompare();
        SetReplayStatus(*this, "Bookmarked root class could not be resolved.");
        return State::HistoryRestoreResult::StaleBreadcrumb;
    }

    const std::vector<void*> live = dumper->GetLiveInstances(rootKlass);
    if (live.empty() || live[0] == nullptr) {
        restoreCompare();
        std::string msg = "No live instance found to refresh this bookmark";
        if (!snap.rootClassName.empty()) {
            msg += " (";
            msg += snap.rootClassName;
            msg += ")";
        }
        else if (!snap.className.empty()) {
            msg += " (";
            msg += snap.className;
            msg += ")";
        }
        msg += ".";
        SetReplayStatus(*this, msg.c_str());
        return State::HistoryRestoreResult::StaleInstance;
    }

    const auto previousStack = walker.stack;
    void* const previousClass = selectedClass;
    void* const previousImage = selectedImage;

    if (imagePtr) {
        if (selectedImage != imagePtr) {
            dumper->ClearValueSearchSchemas();
        }
        selectedImage = imagePtr;
    }

    if (!AssignRootBreadcrumb(*this, rootKlass, live[0])) {
        walker.stack = previousStack;
        selectedClass = previousClass;
        selectedImage = previousImage;
        restoreCompare();
        SetReplayStatus(*this, "No live instance found to refresh this bookmark.");
        return State::HistoryRestoreResult::StaleInstance;
    }

    for (size_t i = 1; i < snap.breadcrumbs.size(); ++i) {
        if (!ReplayOneHop(*this, snap.breadcrumbs[i])) {
            walker.stack = previousStack;
            selectedClass = previousClass;
            selectedImage = previousImage;
            restoreCompare();
            SetReplayStatus(*this,
                "Breadcrumb target is no longer valid — nested object may have been collected.");
            return State::HistoryRestoreResult::StaleBreadcrumb;
        }
    }

    {
        std::lock_guard<std::mutex> lock(inspector.mutex);
        editBufferStore.Clear();
        enumLiteralCache.Clear();
        inspector.cache.methods.clear();
        inspector.cache.methodsCatalogLoaded = false;
        inspector.selectedInstanceIndex = -1;
        inspector.rootInstanceCandidates = live;
        inspector.rootInstanceClassPtr = rootKlass;
        inspector.NoteCacheMutated();
    }

    LoadWalkerStackTop(*this);
    SyncImageComboCaption(*this, snap.imageName, imagePtr);
    navigationFeedback.lastAsyncRecordedInstance =
        walker.stack.empty() ? nullptr : walker.stack.back().instance;
    return State::HistoryRestoreResult::AppliedLiveRefind;
}

// ==== Recursive Memory Walker ========================================
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

    // Probe by actually synthesizing the view. Empty rows are OK when the
    // header is readable and logical length is 0 (empty-success). Still
    // refuse null wrapper / null _items / unreadable header.
    bool emptyButReadable = false;
    auto previewRows = dumper->GetCollectionView(field, 0, &emptyButReadable);
    if (previewRows.empty() && !emptyButReadable) {
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

bool ControlPanelSessionState::NavigateIntoValueTypeSlot(const Engine::FieldInfo& elementRow) {
    if (!dumper || walker.stack.empty() || !walker.stack.back().isCollection) {
        return false;
    }
    size_t index = 0;
    if (!Engine::Dumper::ParseSynthesizedFieldsElementIndex(elementRow.name, index)) {
        return false;
    }

    const InspectorBreadcrumb& parent = walker.stack.back();
    if (!PushValueTypeSlotCrumb(*this, parent.sourceField, elementRow.elementKlass, index, elementRow.type)) {
        return false;
    }

    const InspectorBreadcrumb& top = walker.stack.back();
    StartValueTypeSlotLoad(dumper, top.sourceField, top.klass, top.instance,
                           top.valueTypeElementKlass, top.valueTypeIndex);
    RecordNavigationEvent(("Drill: " + top.label).c_str());
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
    // Collection crumbs and value-type slot crumbs share the owner instance —
    // same liveness probe. Do not treat a raw slot address as identity.
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
    else if (live.isValueTypeSlot) {
        StartValueTypeSlotLoad(dumper, live.sourceField, live.klass, live.instance,
                               live.valueTypeElementKlass, live.valueTypeIndex);
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
