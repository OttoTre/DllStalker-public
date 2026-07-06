#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include "imgui.h"

namespace Gui::Infra
{
class ImGuiContextRAII {
private:
    ImGuiContext* context = nullptr;
    bool ownsContext = false;

public:
    ImGuiContextRAII() = default;

    ~ImGuiContextRAII() {
        if (ownsContext && context != nullptr) {
            ImGui::SetCurrentContext(context);
            ImGui::DestroyContext();
        }
    }

    ImGuiContextRAII(const ImGuiContextRAII&) = delete;
    ImGuiContextRAII& operator=(const ImGuiContextRAII&) = delete;

    ImGuiContextRAII(ImGuiContextRAII&& other) noexcept
        : context(other.context), ownsContext(other.ownsContext) {
        other.context = nullptr;
        other.ownsContext = false;
    }

    ImGuiContextRAII& operator=(ImGuiContextRAII&& other) noexcept {
        if (this != &other) {
            if (ownsContext && context != nullptr) {
                ImGui::SetCurrentContext(context);
                ImGui::DestroyContext();
            }

            context = other.context;
            ownsContext = other.ownsContext;

            other.context = nullptr;
            other.ownsContext = false;
        }
        return *this;
    }

    void Create() {
        IMGUI_CHECKVERSION();
        context = ImGui::CreateContext();
        ownsContext = true;
        ImGui::SetCurrentContext(context);
    }

    bool IsValid() const {
        return context != nullptr && ownsContext;
    }
};

} // namespace Gui::Infra

#endif // ENABLE_DUMPER
