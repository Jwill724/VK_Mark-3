#include "pch.h"

#include "ShaderEditorPanel.h"
#include <imgui_internal.h>
#include "EditorImgui.h"
#include "renderer/Renderer.h"
#include "renderer/backend/shaders/ShaderCache.h"
#include "renderer/backend/shaders/ShaderHotReload.h"

namespace
{
	constexpr size_t MAX_LOG_LINES = 120u;
	constexpr float  TREE_WIDTH = 250.0f;
	constexpr float  LOG_HEIGHT = 150.0f;
	constexpr float  MIN_MAIN = 220.0f;

	constexpr float LINE_NUMBER_GUTTER_WIDTH = 52.0f;

	float GetEditorScrollY(ImGuiID editorId)
	{
		ImGuiWindow* parent = ImGui::GetCurrentWindow();

		for (ImGuiWindow* child : parent->DC.ChildWindows)
		{
			if (child->ChildId == editorId)
				return child->Scroll.y;
		}

		return 0.0f;
	}

	void DrawLineNumberGutter(
		const std::string& source,
		ImGuiID editorId,
		const ImVec2& gutterMin,
		const ImVec2& gutterMax)
	{
		ImDrawList* drawList = ImGui::GetWindowDrawList();

		const float lineHeight = ImGui::GetTextLineHeight();
		const float scrollY = GetEditorScrollY(editorId);
		const float paddingY = ImGui::GetStyle().FramePadding.y;

		const int lineCount =
			1 + static_cast<int>(std::count(source.begin(), source.end(), '\n'));

		const int firstLine = std::max(
			0,
			static_cast<int>(scrollY / lineHeight));

		const int visibleLineCount =
			static_cast<int>((gutterMax.y - gutterMin.y) / lineHeight) + 2;

		const int lastLine = std::min(
			lineCount,
			firstLine + visibleLineCount);

		const ImU32 background =
			ImGui::GetColorU32(ImGuiCol_FrameBg);

		const ImU32 textColor =
			ImGui::GetColorU32(ImGuiCol_TextDisabled);

		const ImU32 separatorColor =
			ImGui::GetColorU32(ImGuiCol_Border);

		drawList->PushClipRect(gutterMin, gutterMax, true);

		drawList->AddRectFilled(
			gutterMin,
			gutterMax,
			background);

		for (int line = firstLine; line < lastLine; ++line)
		{
			char number[16];
			std::snprintf(number, sizeof(number), "%d", line + 1);

			const ImVec2 numberSize = ImGui::CalcTextSize(number);

			const float x =
				gutterMax.x - 8.0f - numberSize.x;

			const float y =
				gutterMin.y +
				paddingY +
				static_cast<float>(line) * lineHeight -
				scrollY;

			drawList->AddText(
				ImVec2(x, y),
				textColor,
				number);
		}

		drawList->AddLine(
			ImVec2(gutterMax.x - 1.0f, gutterMin.y),
			ImVec2(gutterMax.x - 1.0f, gutterMax.y),
			separatorColor);

		drawList->PopClipRect();
	}

	int TextResize(ImGuiInputTextCallbackData* data)
	{
		if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
		{
			auto* str = static_cast<std::string*>(data->UserData);
			str->resize(static_cast<size_t>(data->BufTextLen));
			data->Buf = str->data();
		}
		return 0;
	}

	ImVec4 StatusColor(ShaderHotReload::Status status)
	{
		switch (status)
		{
		case ShaderHotReload::Status::Queued:    return ImVec4(1.00f, 0.75f, 0.30f, 1.0f);
		case ShaderHotReload::Status::Compiling: return ImVec4(0.40f, 0.80f, 1.00f, 1.0f);
		case ShaderHotReload::Status::Failed:    return ImVec4(1.00f, 0.40f, 0.40f, 1.0f);
		default:                                 return ImVec4(0.82f, 0.82f, 0.82f, 1.0f);
		}
	}

	std::string FolderOf(const std::string& spvPath)
	{
		const size_t slash = spvPath.find_last_of('/');
		return slash == std::string::npos ? std::string("<root>") : spvPath.substr(0, slash);
	}

	std::string LeafOf(const std::string& spvPath)
	{
		const size_t slash = spvPath.find_last_of('/');
		return slash == std::string::npos ? spvPath : spvPath.substr(slash + 1u);
	}

	bool ContainsNoCase(const std::string& haystack, const char* needle)
	{
		if (needle == nullptr || needle[0] == '\0') return true;

		const auto it = std::search(
			haystack.begin(), haystack.end(),
			needle, needle + strlen(needle),
			[](char a, char b)
			{
				return std::tolower(static_cast<unsigned char>(a)) ==
					std::tolower(static_cast<unsigned char>(b));
			});

		return it != haystack.end();
	}
}

ShaderEditorPanel& ShaderEditorPanel::Get()
{
	static ShaderEditorPanel panel;
	return panel;
}

void ShaderEditorPanel::RebuildIndex(Renderer& renderer)
{
	const auto& records = renderer.GetShaderCache().Records();
	if (records.size() == m_indexedCount && !m_folders.empty()) return;

	m_folders.clear();
	m_indexedCount = records.size();

	for (uint32_t i = 0; i < static_cast<uint32_t>(records.size()); ++i)
	{
		const std::string folder = FolderOf(records[i].spvPath);

		auto it = std::find_if(m_folders.begin(), m_folders.end(),
			[&](const Folder& f) { return f.name == folder; });

		if (it == m_folders.end())
		{
			m_folders.push_back({ folder, { i } });
			continue;
		}

		it->records.push_back(i);
	}

	std::sort(m_folders.begin(), m_folders.end(),
		[](const Folder& a, const Folder& b) { return a.name < b.name; });

	for (auto& folder : m_folders)
	{
		std::sort(folder.records.begin(), folder.records.end(),
			[&](uint32_t a, uint32_t b) { return records[a].spvPath < records[b].spvPath; });
	}
}

void ShaderEditorPanel::Select(Renderer& renderer, const std::string& spvPath)
{
	const ShaderRecord* record = renderer.GetShaderCache().Find(spvPath);
	if (record == nullptr) return;

	m_selected = spvPath;
	m_buffer = ShaderCache::ReadSource(record->sourcePath);
	m_buffer.reserve(m_buffer.size() + 4096u);
	m_dirty = false;
	m_liveApplied = false;
}

void ShaderEditorPanel::DrawToolbar(Renderer& renderer)
{
	auto& hot = renderer.GetShaderHotReload();

	bool watch = hot.IsWatchEnabled();
	if (ImGui::Checkbox("Watch files##shader", &watch)) hot.SetWatchEnabled(watch);
	ImGui::SetItemTooltip(
		"Polls shader sources and their includes every 250 ms and rebuilds what changed.");

	ImGui::SameLine();
	if (ImGui::Button("Rebuild all##shader")) hot.QueueAll();

	ImGui::SameLine();
	const uint32_t pending = hot.PendingCount();
	if (pending > 0u)
		ImGui::TextColored(ImVec4(0.40f, 0.80f, 1.00f, 1.0f), "%u pending", pending);
	else
		ImGui::TextDisabled("idle");

	ImGui::SameLine();
	ImGui::SetNextItemWidth(-1.0f);
	ImGui::InputTextWithHint("##shaderfilter", "filter", m_filter.data(), m_filter.size());
}

void ShaderEditorPanel::DrawTree(Renderer& renderer, float height)
{
	const auto& records = renderer.GetShaderCache().Records();
	auto& hot = renderer.GetShaderHotReload();

	ImGui::BeginChild("ShaderTree", ImVec2(TREE_WIDTH, height), true);

	const bool filtering = m_filter[0] != '\0';

	for (const Folder& folder : m_folders)
	{
		std::vector<uint32_t> visible;
		visible.reserve(folder.records.size());

		for (uint32_t index : folder.records)
			if (ContainsNoCase(records[index].spvPath, m_filter.data()))
				visible.push_back(index);

		if (visible.empty()) continue;

		if (filtering) ImGui::SetNextItemOpen(true, ImGuiCond_Always);

		const std::string label = fmt::format("{} ({})##folder", folder.name, visible.size());
		if (!ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth)) continue;

		for (uint32_t index : visible)
		{
			const ShaderRecord& record = records[index];
			const auto status = hot.GetStatus(record.spvPath);

			ImGui::PushStyleColor(ImGuiCol_Text, StatusColor(status));
			const bool selected = (record.spvPath == m_selected);

			if (ImGui::Selectable(LeafOf(record.spvPath).c_str(), selected))
				Select(renderer, record.spvPath);

			ImGui::PopStyleColor();

			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", record.spvPath.c_str());
		}

		ImGui::TreePop();
	}

	ImGui::EndChild();
}

void ShaderEditorPanel::DrawSource(Renderer& renderer, float height)
{
	auto& hot = renderer.GetShaderHotReload();

	ImGui::BeginChild("ShaderSource", ImVec2(0.0f, height), false);

	if (m_selected.empty())
	{
		ImGui::TextDisabled("Select a shader from the tree.");
		ImGui::EndChild();
		return;
	}

	const ShaderRecord* record = renderer.GetShaderCache().Find(m_selected);
	if (record == nullptr)
	{
		ImGui::EndChild();
		return;
	}

	const bool isInclude = record->isInclude;

	ImGui::TextUnformatted(record->sourcePath.c_str());

	if (isInclude)
	{
		ImGui::TextDisabled("include (read-only)  |  %u shader(s) depend on this",
			renderer.GetShaderCache().CountIncludeDependents(record->sourcePath));
	}
	else
	{
		ImGui::TextDisabled("%zu pipeline(s)  |  %zu include(s)",
			record->dependents.size(), record->includes.size());

		if (ImGui::Button("Apply (live)"))
		{
			hot.Queue(m_selected, m_buffer);
			m_liveApplied = true;
		}
		ImGui::SetItemTooltip(
			"Compiles the editor buffer and swaps the pipelines in.\n"
			"Nothing is written to disk, so this is lost on restart.");

		ImGui::SameLine();
		if (ImGui::Button("Save + apply"))
		{
			if (ShaderCache::WriteSource(record->sourcePath, m_buffer))
			{
				hot.Queue(m_selected);
				m_dirty = false;
				m_liveApplied = false;
			}
		}
		ImGui::SetItemTooltip(
			"Writes the file, then compiles from disk.\n"
			"The .spv and manifest are only updated once every dependent pipeline builds.");

		ImGui::SameLine();
		if (ImGui::Button("Reload from disk")) Select(renderer, m_selected);

		if (m_dirty)
		{
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "unsaved");
		}

		if (m_liveApplied)
		{
			ImGui::TextColored(ImVec4(0.40f, 0.80f, 1.00f, 1.0f),
				"Running a live build. Disk cache still holds the previous version.");
		}
	}

	ImGui::Separator();

	if (m_buffer.capacity() < m_buffer.size() + 1u)
		m_buffer.reserve(m_buffer.size() + 1u);

	ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.09f, 0.09f, 0.11f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.86f, 0.88f, 1.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 8.0f));

	if (isInclude) ImGui::BeginDisabled(true);
	if (Editor::GetMonoFont()) ImGui::PushFont(Editor::GetMonoFont());

	const ImVec2 sourceStart = ImGui::GetCursorScreenPos();
	const ImVec2 sourceAvail = ImGui::GetContentRegionAvail();

	const ImGuiID editorId = ImGui::GetID("##shadersrc");

	const float editorWidth =
		std::max(1.0f, sourceAvail.x - LINE_NUMBER_GUTTER_WIDTH);

	// Leave room on the left for the line-number gutter.
	ImGui::SetCursorScreenPos(
		ImVec2(
			sourceStart.x + LINE_NUMBER_GUTTER_WIDTH,
			sourceStart.y));

	if (ImGui::InputTextMultiline(
		"##shadersrc",
		m_buffer.data(),
		m_buffer.capacity() + 1u,
		ImVec2(editorWidth, sourceAvail.y),
		ImGuiInputTextFlags_AllowTabInput |
		ImGuiInputTextFlags_CallbackResize,
		&TextResize,
		&m_buffer))
	{
		m_dirty = true;
	}

	const ImVec2 editorMin = ImGui::GetItemRectMin();
	const ImVec2 editorMax = ImGui::GetItemRectMax();

	// Draw the gutter after the editor so its scroll state for this frame
	// is already available.
	DrawLineNumberGutter(
		m_buffer,
		editorId,
		ImVec2(sourceStart.x, editorMin.y),
		ImVec2(editorMin.x, editorMax.y));

	if (Editor::GetMonoFont()) ImGui::PopFont();
	if (isInclude) ImGui::EndDisabled();

	ImGui::PopStyleVar();
	ImGui::PopStyleColor(2);

	ImGui::EndChild();
}

void ShaderEditorPanel::DrawLog(float height)
{
	if (ImGui::Button("Clear##shaderlog")) m_log.clear();

	ImGui::SameLine();
	ImGui::TextDisabled("%zu entries", m_log.size());

	ImGui::BeginChild("ShaderLog", ImVec2(-1.0f, height - ImGui::GetFrameHeightWithSpacing()), true,
		ImGuiWindowFlags_HorizontalScrollbar);

	for (const auto& line : m_log)
	{
		ImGui::PushStyleColor(ImGuiCol_Text,
			line.ok ? ImVec4(0.45f, 0.85f, 0.45f, 1.0f) : ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
		ImGui::TextUnformatted(line.text.c_str());
		ImGui::PopStyleColor();
	}

	if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
		ImGui::SetScrollHereY(1.0f);

	ImGui::EndChild();
}

void ShaderEditorPanel::PumpReports(Renderer& renderer)
{
	for (auto& report : renderer.GetShaderHotReload().DrainReports())
	{
		m_log.push_back({ fmt::format("{}\n{}", report.shader, report.message), report.ok });
		if (m_log.size() > MAX_LOG_LINES)
			m_log.erase(m_log.begin(), m_log.begin() + (m_log.size() - MAX_LOG_LINES));
	}
}

void ShaderEditorPanel::Draw(Renderer& renderer)
{
	RebuildIndex(renderer);

	DrawToolbar(renderer);
	ImGui::Separator();

	const float available = ImGui::GetContentRegionAvail().y;
	const float mainHeight = std::max(
		MIN_MAIN, available - LOG_HEIGHT - ImGui::GetStyle().ItemSpacing.y * 2.0f);

	DrawTree(renderer, mainHeight);
	ImGui::SameLine();
	DrawSource(renderer, mainHeight);

	DrawLog(LOG_HEIGHT);
}