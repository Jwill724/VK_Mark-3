#pragma once

#include <array>
#include <string>
#include <vector>

class Renderer;

class ShaderEditorPanel final
{
public:
	static ShaderEditorPanel& Get();

	void Draw(Renderer& renderer);

	void PumpReports(Renderer& renderer);
	void ClearLog() { m_log.clear(); }

private:
	struct Folder
	{
		std::string           name;
		std::vector<uint32_t> records;
	};

	void RebuildIndex(Renderer& renderer);
	void DrawToolbar(Renderer& renderer);
	void DrawTree(Renderer& renderer, float height);
	void DrawSource(Renderer& renderer, float height);
	void DrawLog(float height);

	void Select(Renderer& renderer, const std::string& spvPath);

	std::vector<Folder> m_folders;
	size_t              m_indexedCount = 0u;

	std::string m_selected;
	std::string m_buffer;
	bool        m_dirty = false;
	bool        m_liveApplied = false;

	std::array<char, 64> m_filter{};

	struct LogLine { std::string text; bool ok; };
	std::vector<LogLine> m_log;
};