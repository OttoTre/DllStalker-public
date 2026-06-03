#pragma once

#ifdef ENABLE_DUMPER

namespace Gui::Views
{
// Renders main-thread dispatcher state for Method Invoker / Transform tabs.
void RenderDispatchStatus(const char* featureLabel);
} // namespace Gui::Views

#endif // ENABLE_DUMPER
