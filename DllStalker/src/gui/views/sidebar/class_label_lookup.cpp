#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/views/sidebar/class_label_lookup.h"

#include "gui/session_state.h"

namespace Gui::Views
{
std::string LookupClassDisplayName(const ControlPanelSessionState& state, void* klassPtr)
{
    if (!klassPtr) {
        return {};
    }
    for (const auto& cl : *state.GetClassCacheSnapshot()) {
        if (cl.klassPtr == klassPtr) {
            if (!cl.ns.empty()) {
                return cl.ns + "::" + cl.name;
            }
            return cl.name;
        }
    }
    return {};
}
} // namespace Gui::Views

#endif // ENABLE_DUMPER
