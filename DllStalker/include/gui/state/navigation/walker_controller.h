#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <string>
#include <vector>

#include "types/dumper_types.h"

namespace Gui
{
// One step in the Recursive Memory Walker history. The bottom of the stack
// (index 0) is the root: the instance the user originally picked from the
// candidates combobox. Subsequent entries are nested objects reached by
// clicking pointer-typed fields.
//
// Collection breadcrumbs (isCollection == true) don't have a klass /
// instance pair of their own -- they reuse the parent breadcrumb's
// (klass, instance) plus the captured `sourceField` so a back-jump or an
// auto-refresh can re-resolve the array/list pointer (which may have moved
// after a GC) and re-synthesize the element rows.
struct InspectorBreadcrumb {
    void*       klass    = nullptr;
    void*       instance = nullptr;
    std::string label{};

    bool isCollection = false;
    Engine::FieldInfo sourceField{};
};
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
