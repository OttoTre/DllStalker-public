#pragma once

#include "pch.h"

#include <functional>
#include <string>

namespace Engine
{
struct FieldInfo;
}

namespace Gui
{
struct ControlPanelSessionState;
}

namespace Gui::Views
{
bool IsEditableFieldType(const Engine::FieldInfo& field);
bool TryParseFieldDisplayInt64(const std::string& text, int64_t& out);

void RenderScalarFieldEditCell(const Engine::FieldInfo& field,
                               ControlPanelSessionState& state,
                               size_t fieldIndex,
                               char editStatus[],
                               float& editStatusAtSeconds,
                               const std::function<void(bool)>& doRefresh);

void RenderEnumFieldEditCell(const Engine::FieldInfo& field,
                             ControlPanelSessionState& state,
                             size_t fieldIndex,
                             char editStatus[],
                             float& editStatusAtSeconds,
                             const std::function<void(bool)>& doRefresh);
} // namespace Gui::Views
