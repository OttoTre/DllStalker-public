#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/views/dock/exporter_tab.h"

#include "dumper/sdk_exporter.h"
#include "gui/session_state.h"

#include "imgui.h"

#include <shellapi.h>

#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>

namespace Gui::Views
{
namespace
{
enum class ExportScope {
	ActiveClass = 0,
	EntireImage,
	BookmarkedClasses,
};

std::wstring GetDllDirectoryW()
{
	HMODULE module = nullptr;
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
		                        | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                        reinterpret_cast<LPCWSTR>(&GetDllDirectoryW),
	                        &module)
	    || !module) {
		return L".";
	}

	wchar_t path[MAX_PATH]{};
	const DWORD len = GetModuleFileNameW(module, path, MAX_PATH);
	if (len == 0 || len >= MAX_PATH) {
		return L".";
	}

	std::wstring dir(path, len);
	const size_t slash = dir.find_last_of(L"\\/");
	if (slash != std::wstring::npos) {
		dir.resize(slash);
	}
	return dir;
}

std::string WideToUtf8(const std::wstring& wide)
{
	if (wide.empty()) {
		return {};
	}
	const int needed = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
	if (needed <= 0) {
		return {};
	}
	std::string out(static_cast<size_t>(needed - 1), '\0');
	WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, out.data(), needed, nullptr, nullptr);
	return out;
}

std::wstring Utf8ToWide(const std::string& utf8)
{
	if (utf8.empty()) {
		return {};
	}
	const int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
	if (needed <= 0) {
		return {};
	}
	std::wstring out(static_cast<size_t>(needed - 1), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, out.data(), needed);
	return out;
}

std::vector<Engine::ClassInfo> BuildClassList(ControlPanelSessionState& state, ExportScope scope)
{
	std::vector<Engine::ClassInfo> classes;

	if (!state.dumper) {
		return classes;
	}

	switch (scope) {
	case ExportScope::EntireImage:
		if (state.selectedImage) {
			classes = state.dumper->GetRawClasses(state.selectedImage);
		}
		break;

	case ExportScope::ActiveClass:
		if (!state.selectedClass) {
			break;
		}
		{
			bool found = false;
			for (const auto& c : state.classCache.data) {
				if (c.klassPtr == state.selectedClass) {
					classes.push_back(c);
					found = true;
					break;
				}
			}
			if (!found) {
				Engine::ClassInfo row{};
				row.name     = "Class";
				row.klassPtr = state.selectedClass;
				classes.push_back(std::move(row));
			}
		}
		break;

	case ExportScope::BookmarkedClasses:
		{
			std::unordered_set<void*> seen;
			for (const auto& bm : state.bookmarks.bookmarks) {
				void* klassPtr = bm.snapshot.classPtr;
				if (!klassPtr || seen.count(klassPtr) != 0) {
					continue;
				}
				seen.insert(klassPtr);
				Engine::ClassInfo row{};
				row.name     = bm.snapshot.className.empty() ? "Class" : bm.snapshot.className;
				row.klassPtr = klassPtr;
				classes.push_back(std::move(row));
			}
		}
		break;
	}

	return classes;
}

std::string ComposeOutputPath(const char* baseName, Engine::Dumper::SdkExportFormat format)
{
	const std::wstring dir = GetDllDirectoryW();
	const char*      ext   = (format == Engine::Dumper::SdkExportFormat::CppHeader) ? ".h" : ".cs";

	std::string name = (baseName && baseName[0] != '\0') ? baseName : "offsets";
	for (char& c : name) {
		if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>'
		    || c == '|') {
			c = '_';
		}
	}

	const std::string filePart = name + ext;
	return WideToUtf8(dir) + "\\" + filePart;
}
} // namespace

void RenderExporterTab(ControlPanelSessionState& state) {
	static int   scopeIndex     = static_cast<int>(ExportScope::ActiveClass);
	static int   formatIndex    = 0;
	static bool  includeMethods = false;
	static bool  includeEnums   = false;
	static char  fileBase[128]  = "offsets";
	static std::string lastOutputPath{};
	static std::string lastStatus{};
	static int         lastClassCount = 0;
	static int         lastFieldCount = 0;

	const char* scopeLabels[]  = { "Active class", "Entire image", "Bookmarked classes" };
	const char* formatLabels[] = { "C++ (.h)", "C# (.cs)" };

	ImGui::Combo("Scope", &scopeIndex, scopeLabels, IM_ARRAYSIZE(scopeLabels));
	ImGui::SameLine();
	const bool fieldsAlways = true;
	ImGui::BeginDisabled(fieldsAlways);
	bool fieldsChecked = true;
	ImGui::Checkbox("Fields", &fieldsChecked);
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::Checkbox("Methods (RVA)", &includeMethods);
	ImGui::SameLine();
	ImGui::Checkbox("Enums", &includeEnums);

	ImGui::Combo("Format", &formatIndex, formatLabels, IM_ARRAYSIZE(formatLabels));
	ImGui::SameLine();
	ImGui::SetNextItemWidth(180.0f);
	ImGui::InputText("File", fileBase, sizeof(fileBase));
	ImGui::SameLine();
	const char* extHint = (formatIndex == 0) ? ".h" : ".cs";
	ImGui::TextDisabled("%s", extHint);

	if (ImGui::Button("Export")) {
		lastStatus.clear();
		if (!state.dumper) {
			lastStatus = "Dumper not initialized.";
		}
		else {
			const auto scope   = static_cast<ExportScope>(scopeIndex);
			const auto classes = BuildClassList(state, scope);
			if (classes.empty()) {
				lastStatus = "No classes to export for the selected scope.";
			}
			else {
				Engine::Dumper::SdkExportOptions opts{};
				opts.format         = (formatIndex == 0) ? Engine::Dumper::SdkExportFormat::CppHeader
				                                         : Engine::Dumper::SdkExportFormat::CsharpStubs;
				opts.includeMethods = includeMethods;
				opts.includeEnums   = includeEnums;

				lastOutputPath = ComposeOutputPath(fileBase, opts.format);
				static std::string pathStorage;
				pathStorage    = lastOutputPath;
				opts.outputPath = pathStorage.c_str();

				const Engine::Dumper::SdkExportResult result =
				    state.dumper->ExportSdk(classes, opts);
				lastClassCount = result.classCount;
				lastFieldCount = result.fieldCount;
				if (result.success) {
					lastStatus = "Export complete.";
				}
				else {
					lastStatus = "Export failed (see console).";
				}
			}
		}
	}

	if (!lastOutputPath.empty()) {
		ImGui::SameLine();
		if (ImGui::SmallButton("Open folder")) {
			const std::wstring dir = GetDllDirectoryW();
			ShellExecuteW(nullptr, L"explore", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		}
	}

	if (!lastStatus.empty()) {
		ImGui::TextUnformatted(lastStatus.c_str());
	}
	if (!lastOutputPath.empty()) {
		ImGui::TextWrapped("-> %s", lastOutputPath.c_str());
		if (lastClassCount > 0 || lastFieldCount > 0) {
			ImGui::TextDisabled("%d classes, %d fields", lastClassCount, lastFieldCount);
		}
	}

	if (!state.dumper) {
		ImGui::TextDisabled("Initialize the dumper from the main window first.");
	}
	else if (scopeIndex == static_cast<int>(ExportScope::EntireImage) && !state.selectedImage) {
		ImGui::TextDisabled("Select an assembly in the sidebar for image export.");
	}
	else if (scopeIndex == static_cast<int>(ExportScope::ActiveClass) && !state.selectedClass) {
		ImGui::TextDisabled("Select a class in the sidebar for active-class export.");
	}
	else if (scopeIndex == static_cast<int>(ExportScope::BookmarkedClasses) && state.bookmarks.Size() == 0) {
		ImGui::TextDisabled("No bookmarks saved yet.");
	}
}
} // namespace Gui::Views

#endif // ENABLE_DUMPER
