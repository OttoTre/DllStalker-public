#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <string>

struct ControlPanelSessionState;

namespace Gui::Views
{
// Scan the class cache for klassPtr and format as "ns::name" (or bare name).
// Returns empty string when klassPtr is null or not present in the cache.
std::string LookupClassDisplayName(const ControlPanelSessionState& state, void* klassPtr);
} // namespace Gui::Views

#endif // ENABLE_DUMPER
