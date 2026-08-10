#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/views/fields/field_edit_cells.h"

#include "gui/session_state.h"
#include "types/type_classifier.h"
#include "types/value_decoder.h"

#include "imgui.h"

#include <cctype>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

namespace Gui::Views
{
bool IsEditableFieldType(const Engine::FieldInfo& field) {
    // Enums: FieldInfo::isEnum (not GetCategory — dotted enum type names → PTR).
    if (field.isEnum) {
        return true;
    }
    using Cat = Engine::Types::TypeCategory;
    const Cat cat = Engine::Types::GetCategory(field.type);
    switch (cat) {
    case Cat::I1: case Cat::I2: case Cat::I4: case Cat::I8:
    case Cat::U1: case Cat::U2: case Cat::U4: case Cat::U8:
    case Cat::R4: case Cat::R8:
    case Cat::BOOLEAN: case Cat::STRING:
    case Cat::VEC2: case Cat::VEC3: case Cat::VEC4:
    case Cat::QUAT: case Cat::COLOR: case Cat::COLOR32: case Cat::RECT:
        return true;
    default:
        return false;
    }
}

bool TryParseFieldDisplayInt64(const std::string& text, int64_t& out) {
    if (text.empty() || text == "-" || text == "??") {
        return false;
    }
    try {
        size_t idx = 0;
        if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
            out = std::stoll(text, &idx, 16);
        }
        else {
            out = std::stoll(text, &idx, 10);
        }
        return idx == text.size();
    }
    catch (...) {
        return false;
    }
}

namespace {
// Matches value_writer bool tokens (case-insensitive): true/1/yes or false/0/no.
bool TryParseFieldDisplayBool(const std::string& text, bool& out) {
    if (text.empty() || text == "-" || text == "??") {
        return false;
    }
    std::string low = text;
    while (!low.empty() && std::isspace(static_cast<unsigned char>(low.front()))) {
        low.erase(low.begin());
    }
    while (!low.empty() && std::isspace(static_cast<unsigned char>(low.back()))) {
        low.pop_back();
    }
    for (char& c : low) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (low == "true" || low == "1" || low == "yes") {
        out = true;
        return true;
    }
    if (low == "false" || low == "0" || low == "no") {
        out = false;
        return true;
    }
    return false;
}
} // namespace

void RenderScalarFieldEditCell(const Engine::FieldInfo& field,
                               ControlPanelSessionState& state,
                               size_t fieldIndex,
                               char editStatus[],
                               float& editStatusAtSeconds,
                               const std::function<void(bool)>& doRefresh) {
    const uintptr_t key = field.valueAddress;
    auto& buffer = state.editBufferStore.buffers[key];
    if (buffer[0] == '\0') {
        using Cat = Engine::Types::TypeCategory;
        if (Engine::Types::GetCategory(field.type) == Cat::STRING) {
            const std::string initial = Engine::Decode::StripQuotesForFieldEdit(field.valueDisplay);
            strncpy_s(buffer.data(), buffer.size(), initial.c_str(), _TRUNCATE);
        }
        else {
            const char* initialText = !field.valueDisplay.empty() ? field.valueDisplay.c_str() : "0";
            strncpy_s(buffer.data(), buffer.size(), initialText, _TRUNCATE);
        }
    }

    ImGui::PushID(static_cast<int>(fieldIndex) + 10000);
    const float availWidth = ImGui::GetContentRegionAvail().x;
    constexpr float applyBtnWidth = 28.0f;
    constexpr float spacing = 4.0f;
    ImGui::SetNextItemWidth(availWidth - applyBtnWidth - spacing);
    ImGui::InputText("##FieldEdit", buffer.data(), buffer.size());
    ImGui::SameLine(0.0f, spacing);
    if (ImGui::Button("OK", ImVec2(applyBtnWidth, 0))) {
        std::string error;
        if (state.dumper->SetFieldValue(field, buffer.data(), &error)) {
            snprintf(editStatus, 128, "Applied: %s", field.name.c_str());
            editStatusAtSeconds = static_cast<float>(ImGui::GetTime());
            State::FieldAuditPayload audit{};
            audit.fieldName       = field.name;
            audit.fieldType       = field.type;
            audit.newValueDisplay = buffer.data();
            state.RecordFieldAudit(audit);
            doRefresh(false);
        }
        else {
            snprintf(editStatus, 128, "Edit failed: %s", error.empty() ? "Unknown error" : error.c_str());
            editStatusAtSeconds = static_cast<float>(ImGui::GetTime());
        }
    }
    ImGui::PopID();
}

void RenderBoolFieldEditCell(const Engine::FieldInfo& field,
                             ControlPanelSessionState& state,
                             size_t fieldIndex,
                             char editStatus[],
                             float& editStatusAtSeconds,
                             const std::function<void(bool)>& doRefresh) {
    bool checked = false;
    const bool readable = TryParseFieldDisplayBool(field.valueDisplay, checked);
    // Unreadable / "??" / unknown tokens: unchecked + disabled (no accidental write).

    ImGui::PushID(static_cast<int>(fieldIndex) + 10000);
    if (!readable) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Checkbox("##BoolField", &checked)) {
        const char* payload = checked ? "true" : "false";
        std::string error;
        if (state.dumper->SetFieldValue(field, payload, &error)) {
            snprintf(editStatus, 128, "Applied: %s", field.name.c_str());
            editStatusAtSeconds = static_cast<float>(ImGui::GetTime());
            State::FieldAuditPayload audit{};
            audit.fieldName       = field.name;
            audit.fieldType       = field.type;
            audit.newValueDisplay = payload;
            state.RecordFieldAudit(audit);
            doRefresh(false);
        }
        else {
            snprintf(editStatus, 128, "Edit failed: %s", error.empty() ? "Unknown error" : error.c_str());
            editStatusAtSeconds = static_cast<float>(ImGui::GetTime());
        }
    }
    if (!readable) {
        ImGui::EndDisabled();
    }
    ImGui::PopID();
}

void RenderEnumFieldEditCell(const Engine::FieldInfo& field,
                             ControlPanelSessionState& state,
                             size_t fieldIndex,
                             char editStatus[],
                             float& editStatusAtSeconds,
                             const std::function<void(bool)>& doRefresh) {
    static constexpr const char* kCustomIntLabel = "(Custom int...)";

    auto& lits = state.enumLiteralCache.byKlass[field.enumKlass];
    if (lits.empty() && state.dumper && field.enumKlass) {
        lits = state.dumper->GetEnumLiterals(field.enumKlass);
    }

    int64_t currentValue = 0;
    const bool hasParsedValue = TryParseFieldDisplayInt64(field.valueDisplay, currentValue);

    int matchIndex = -1;
    if (hasParsedValue) {
        for (size_t i = 0; i < lits.size(); ++i) {
            if (lits[i].value == currentValue) {
                matchIndex = static_cast<int>(i);
                break;
            }
        }
    }

    if (!lits.empty() && hasParsedValue && matchIndex < 0) {
        state.enumLiteralCache.customModeKeys.insert(field.valueAddress);
    }

    const bool useCustom = lits.empty()
                        || state.enumLiteralCache.customModeKeys.count(field.valueAddress) != 0;

    if (useCustom) {
        RenderScalarFieldEditCell(field, state, fieldIndex, editStatus, editStatusAtSeconds, doRefresh);
        return;
    }

    std::vector<const char*> labels;
    labels.reserve(lits.size() + 1);
    for (const auto& lit : lits) {
        labels.push_back(lit.name.c_str());
    }
    labels.push_back(kCustomIntLabel);

    int currentItem = matchIndex >= 0 ? matchIndex : static_cast<int>(lits.size());
    const int previousItem = currentItem;

    ImGui::PushID(static_cast<int>(fieldIndex) + 10000);
    const float availWidth = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(availWidth);
    if (ImGui::Combo("##EnumField", &currentItem, labels.data(), static_cast<int>(labels.size()))) {
        if (currentItem == static_cast<int>(lits.size())) {
            state.enumLiteralCache.customModeKeys.insert(field.valueAddress);
        }
        else if (currentItem >= 0 && currentItem < static_cast<int>(lits.size())
                 && currentItem != previousItem) {
            state.enumLiteralCache.customModeKeys.erase(field.valueAddress);
            std::string error;
            if (state.dumper->SetFieldValue(field, std::to_string(lits[static_cast<size_t>(currentItem)].value), &error)) {
                snprintf(editStatus, 128, "Applied: %s", field.name.c_str());
                editStatusAtSeconds = static_cast<float>(ImGui::GetTime());
                State::FieldAuditPayload audit{};
                audit.fieldName       = field.name;
                audit.fieldType       = field.type;
                audit.newValueDisplay = lits[static_cast<size_t>(currentItem)].name;
                state.RecordFieldAudit(audit);
                doRefresh(false);
            }
            else {
                snprintf(editStatus, 128, "Edit failed: %s", error.empty() ? "Unknown error" : error.c_str());
                editStatusAtSeconds = static_cast<float>(ImGui::GetTime());
            }
        }
    }
    ImGui::PopID();
}
} // namespace Gui::Views

#endif // ENABLE_DUMPER
