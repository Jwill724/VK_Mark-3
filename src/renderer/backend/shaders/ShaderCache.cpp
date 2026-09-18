#include "pch.h"

#include "ShaderCache.h"
#include "ShaderCompiler.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace
{
	constexpr uint64_t FNV_OFFSET = 1469598103934665603ull;
	constexpr uint64_t FNV_PRIME = 1099511628211ull;

	uint64_t HashBytes(const void* data, size_t size, uint64_t seed)
	{
		const auto* bytes = static_cast<const uint8_t*>(data);
		uint64_t h = seed;
		for (size_t i = 0; i < size; ++i)
		{
			h ^= bytes[i];
			h *= FNV_PRIME;
		}
		return h;
	}

	const char* ExtForStage(Vulkan_ShaderStage stage)
	{
		switch (stage)
		{
		case Vulkan_ShaderStage::VERTEX_STAGE:   return ".vert";
		case Vulkan_ShaderStage::FRAGMENT_STAGE: return ".frag";
		case Vulkan_ShaderStage::TASK_STAGE:     return ".task";
		case Vulkan_ShaderStage::MESH_STAGE:     return ".mesh";
		default:                                 return ".comp";
		}
	}

	std::string SourcePathFor(const char* spvPath, Vulkan_ShaderStage stage)
	{
		std::string rel(spvPath);
		const size_t dot = rel.find_last_of('.');
		if (dot != std::string::npos) rel.erase(dot);
		return std::string(ShaderCache::SHADER_ROOT) + rel + ExtForStage(stage);
	}

	std::vector<std::string> Split(const std::string& s, char sep)
	{
		std::vector<std::string> out;
		std::string token;
		std::istringstream stream(s);
		while (std::getline(stream, token, sep))
			if (!token.empty()) out.push_back(token);
		return out;
	}
}

std::string ShaderCache::ReadSource(const std::string& path)
{
	std::ifstream f(path, std::ios::binary | std::ios::ate);
	if (!f.is_open()) return {};
	const size_t size = static_cast<size_t>(f.tellg());
	std::string out(size, '\0');
	f.seekg(0);
	f.read(out.data(), static_cast<std::streamsize>(size));
	return out;
}

bool ShaderCache::WriteSource(const std::string& path, const std::string& text)
{
	std::ofstream f(path, std::ios::binary | std::ios::trunc);
	if (!f.is_open()) return false;
	f.write(text.data(), static_cast<std::streamsize>(text.size()));
	return f.good();
}

uint32_t ShaderCache::FindOrCreate(const char* spvPath, Vulkan_ShaderStage stage)
{
	if (auto it = m_index.find(spvPath); it != m_index.end())
		return it->second;

	const uint32_t idx = static_cast<uint32_t>(m_records.size());

	ShaderRecord record;
	record.spvPath = spvPath;
	record.stage = stage;
	record.sourcePath = SourcePathFor(spvPath, stage);

	m_records.push_back(std::move(record));
	m_blobs.emplace_back();
	m_index.emplace(spvPath, idx);
	return idx;
}

uint64_t ShaderCache::HashTree(
	const std::string& sourcePath,
	const std::vector<std::string>& includes) const
{
	uint64_t h = HashBytes(&OPTIONS_SALT, sizeof(OPTIONS_SALT), FNV_OFFSET);

	const std::string root = ReadSource(sourcePath);
	if (root.empty()) return 0ull;
	h = HashBytes(root.data(), root.size(), h);

	for (const auto& inc : includes)
	{
		const std::string text = ReadSource(inc);
		h = HashBytes(inc.data(), inc.size(), h);
		h = HashBytes(text.data(), text.size(), h);
	}

	return h;
}

uint64_t ShaderCache::HashTree(const ShaderRecord& record) const
{
	return HashTree(record.sourcePath, record.includes);
}

void ShaderCache::LoadManifest()
{
	std::ifstream f(MANIFEST_PATH);
	if (!f.is_open()) return;

	std::string line;
	while (std::getline(f, line))
	{
		if (line.empty()) continue;

		const size_t a = line.find('|');
		if (a == std::string::npos) continue;
		const size_t b = line.find('|', a + 1u);

		const std::string key = line.substr(0, a);

		auto it = m_index.find(key);
		if (it == m_index.end()) continue;

		ShaderRecord& record = m_records[it->second];
		record.hash = std::strtoull(
			line.substr(a + 1u, (b == std::string::npos ? line.size() : b) - a - 1u).c_str(),
			nullptr, 10);

		if (b != std::string::npos)
			record.includes = Split(line.substr(b + 1u), ';');
	}
}

void ShaderCache::SaveManifest() const
{
	std::error_code ec;
	fs::create_directories(fs::path(MANIFEST_PATH).parent_path(), ec);

	std::ofstream f(MANIFEST_PATH, std::ios::trunc);
	if (!f.is_open()) return;

	for (const auto& record : m_records)
	{
		if (record.isInclude) continue;
		f << record.spvPath << '|' << record.hash << '|';
		for (const auto& inc : record.includes) f << inc << ';';
		f << '\n';
	}
}

bool ShaderCache::WriteSpv(const std::string& spvPath, const std::vector<uint32_t>& spirv) const
{
	INVARIANT(spirv.size() >= 5u);
	INVARIANT(spirv[0] == 0x07230203u);
	INVARIANT(spirv.size() < 16u * 1024u * 1024u);

	const fs::path full = fs::path(SHADER_ROOT) / spvPath;

	std::error_code ec;
	fs::create_directories(full.parent_path(), ec);

	std::ofstream f(full, std::ios::binary | std::ios::trunc);
	if (!f.is_open()) return false;

	f.write(reinterpret_cast<const char*>(spirv.data()),
		static_cast<std::streamsize>(spirv.size() * sizeof(uint32_t)));
	return f.good();
}

bool ShaderCache::BuildFromTable(std::string& outLog)
{
	PipelineTable::Build();

	m_records.clear();
	m_blobs.clear();
	m_index.clear();

	for (size_t i = 0; i < RD::PIPELINE_COUNT; ++i)
	{
		const auto id = static_cast<RD::Renderer_Pipeline>(i);
		const PipelineDef& def = PipelineTable::Get(id);

		for (uint32_t s = 0; s < def.shaderCount; ++s)
		{
			const uint32_t idx = FindOrCreate(def.shaders[s].path, def.shaders[s].stage);
			m_records[idx].dependents.push_back(id);
		}
	}

	{
		std::error_code ec;
		for (const auto& entry : fs::recursive_directory_iterator(INCLUDE_DIR, ec))
		{
			if (ec || !entry.is_regular_file()) continue;
			if (entry.path().extension() != ".glsl") continue;

			const std::string source = NormalizePath(entry.path().string());
			const std::string key = fs::relative(entry.path(), SHADER_ROOT, ec).generic_string();
			if (ec) continue;

			if (m_index.find(key) != m_index.end()) continue;

			ShaderRecord record;
			record.spvPath = key;
			record.sourcePath = source;
			record.isInclude = true;

			m_index.emplace(key, static_cast<uint32_t>(m_records.size()));
			m_records.push_back(std::move(record));
			m_blobs.emplace_back();
		}
	}

	LoadManifest();

	uint32_t reused = 0u, compiled = 0u, failed = 0u;

	for (size_t i = 0; i < m_records.size(); ++i)
	{
		ShaderRecord& record = m_records[i];
		if (record.isInclude) continue;

		const uint64_t live = HashTree(record);

		if (live != 0ull && live == record.hash)
		{
			std::ifstream f(fs::path(SHADER_ROOT) / record.spvPath, std::ios::binary | std::ios::ate);
			if (f.is_open())
			{
				const size_t bytes = static_cast<size_t>(f.tellg());
				if (bytes >= 20u && (bytes % sizeof(uint32_t)) == 0u)
				{
					m_blobs[i].resize(bytes / sizeof(uint32_t));
					f.seekg(0);
					f.read(reinterpret_cast<char*>(m_blobs[i].data()),
						static_cast<std::streamsize>(bytes));
					++reused;
					continue;
				}
			}
		}

		const std::string text = ReadSource(record.sourcePath);
		if (text.empty())
		{
			outLog += fmt::format("[shader] missing source {}\n", record.sourcePath);
			++failed;
			continue;
		}

		CompileResult result = ShaderCompiler::Compile(record.sourcePath, text, record.stage, true);
		if (!result.ok)
		{
			outLog += fmt::format("[shader] {}\n{}\n", record.sourcePath, result.log);
			++failed;
			continue;
		}

		record.hash = HashTree(record.sourcePath, result.includes);
		record.includes = std::move(result.includes);
		m_blobs[i] = std::move(result.spirv);

		fmt::println("Shader file: {}", record.spvPath);

		WriteSpv(record.spvPath, m_blobs[i]);
		++compiled;
	}

	SaveManifest();

	outLog += fmt::format(
		"[shader] {} cached, {} compiled, {} failed\n", reused, compiled, failed);

	return failed == 0u;
}

const std::vector<uint32_t>& ShaderCache::GetSpirvUnlocked(const std::string& spvPath) const
{
	auto it = m_index.find(spvPath);
	INVARIANT(it != m_index.end());
	return m_blobs[it->second];
}

std::vector<uint32_t> ShaderCache::SnapshotSpirv(const std::string& spvPath) const
{
	std::shared_lock lock(m_blobLock);
	auto it = m_index.find(spvPath);
	if (it == m_index.end()) return {};
	return m_blobs[it->second];
}

const ShaderRecord* ShaderCache::Find(const std::string& spvPath) const
{
	auto it = m_index.find(spvPath);
	return it == m_index.end() ? nullptr : &m_records[it->second];
}

std::string ShaderCache::NormalizePath(const std::string& path)
{
	return fs::path(path).lexically_normal().generic_string();
}

uint32_t ShaderCache::CountIncludeDependents(const std::string& sourcePath) const
{
	uint32_t count = 0u;

	for (const auto& record : m_records)
	{
		if (record.isInclude) continue;
		if (std::find(record.includes.begin(), record.includes.end(), sourcePath)
			!= record.includes.end())
			++count;
	}

	return count;
}

void ShaderCache::CommitBlob(
	const std::string& spvPath,
	std::vector<uint32_t>&& spirv,
	uint64_t                   hash,
	std::vector<std::string>&& includes,
	bool                       writeToDisk,
	bool                       saveManifest)
{
	auto it = m_index.find(spvPath);
	if (it == m_index.end()) return;

	{
		std::unique_lock lock(m_blobLock);
		m_blobs[it->second] = std::move(spirv);
		m_records[it->second].hash = hash;
		m_records[it->second].includes = std::move(includes);
	}

	if (!writeToDisk) return;

	WriteSpv(spvPath, m_blobs[it->second]);

	if (saveManifest) SaveManifest();
}