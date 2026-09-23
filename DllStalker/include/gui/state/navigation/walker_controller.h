#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <string>
#include <vector>

#include "types/dumper_types.h"

namespace Engine
{
class UnityDumper;
}

namespace Gui
{
// One step in the Recursive Memory Walker history. The bottom of the stack
// (index 0) is the root.
//
// Collection breadcrumbs (isCollection == true) don't have a klass /
// instance pair of their own -- they reuse the parent breadcrumb's
// (klass, instance) plus the captured `sourceField` so a back-jump or an
// auto-refresh can re-resolve the array/list pointer (which may have moved
// after a GC) and re-synthesize the element rows.
//
// Custom valuetype element crumbs (isValueTypeSlot) keep the same owner
// (klass, instance) and wrapper `sourceField`. Identity is wrapper + index
// only — a raw slot address is not durable across GC / List._items moves.
struct InspectorBreadcrumb {
    void*       klass    = nullptr;
    void*       instance = nullptr;
    std::string label{};

    bool isCollection = false;
    Engine::FieldInfo sourceField{};

    bool   isValueTypeSlot = false;
    void*  valueTypeElementKlass = nullptr;
    size_t valueTypeIndex = 0;
    // True when valueTypeIndex is known: live PushValueTypeSlotCrumb, JSON
    // persist, or a "[i]" / "[i] TypeName" label parse. Default 0 is not
    // "element 0" when this flag is false.
    bool   hasValueTypeIndex = false;
};

// Custom valuetype collection element: klass-stamped, not enum, not an
// allowlisted Unity inline (VEC*/COLOR*/…). Shared by Fields click and Search.
bool IsCustomValueTypeElementRow(const Engine::FieldInfo& field, Engine::UnityDumper& dumper);
} // namespace Gui

namespace Gui::State
{
// UI-thread-only history of the Walker's drill-ins. The cross-model
// orchestration (cache reloads, dumper reads) lives on
// ControlPanelSessionState; this struct just owns the data and the
// trivial reset.
struct WalkerController
{
    std::vector<InspectorBreadcrumb> stack{};

    void Reset() { stack.clear(); }
    bool   IsEmpty() const { return stack.empty(); }
    size_t Size() const { return stack.size(); }
};
} // namespace Gui::State

#endif // ENABLE_DUMPER
