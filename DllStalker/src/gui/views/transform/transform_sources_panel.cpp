#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/views/transform/transform_sources_panel.h"

#include "gui/chrome/ui_theme.h"
#include "gui/config.h"
#include "gui/state/transform/transform_model.h"

#include "imgui.h"

#include <algorithm>
#include <cstdio>

namespace Gui::Views
{
namespace
{
const char* SourceKindLabel(Gui::State::TransformSourceKind kind) {
    switch (kind) {
    case Gui::State::TransformSourceKind::Self:
        return "Self";
    case Gui::State::TransformSourceKind::ImplicitTransform:
        return "transform";
    case Gui::State::TransformSourceKind::ImplicitGameObject:
        return "gameObject";
    case Gui::State::TransformSourceKind::Field:
        return "Field";
    }
    return "?";
}
} // namespace

void RenderTransformSourcesPanel(Gui::State::TransformModel& model) {
    ImGui::Text("Sources (%zu)", model.sources.size());

    const float selectColW =
        (std::max)(ImGui::CalcTextSize("Selected").x, ImGui::CalcTextSize("Select").x)
        + ImGui::GetStyle().FramePadding.x * 2.0f
        + ImGui::GetStyle().CellPadding.x * 2.0f;

    if (ImGui::BeginTable("TransformSources", 5,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 72.0f * Gui::Config::GUI_SCALE);
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Ptr", ImGuiTableColumnFlags_WidthFixed, 110.0f * Gui::Config::GUI_SCALE);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, selectColW);
        ImGui::TableHeadersRow();

        for (int i = 0; i < static_cast<int>(model.sources.size()); ++i) {
            const auto& src = model.sources[static_cast<size_t>(i)];
            ImGui::TableNextRow();
            const bool selected = (i == model.selectedIndex);

            ImGui::TableSetColumnIndex(0);
            if (selected) {
                ImGui::TextColored(UiTheme::Tokens().semantic_link, "%s", SourceKindLabel(src.kind));
            }
            else {
                ImGui::TextUnformatted(SourceKindLabel(src.kind));
            }

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(src.label.c_str());

            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(src.typeName.c_str());

            ImGui::TableSetColumnIndex(3);
            char ptrBuf[32]{};
            snprintf(ptrBuf, sizeof(ptrBuf), "0x%llX",
                     static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(src.instancePtr)));
            ImGui::TextUnformatted(ptrBuf);

            ImGui::TableSetColumnIndex(4);
            ImGui::PushID(i);
            if (ImGui::SmallButton(selected ? "Selected" : "Select")) {
                if (!selected) {
                    model.selectedIndex = i;
                    model.InvalidateFetch();
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

} // namespace Gui::Views

#endif // ENABLE_DUMPER
