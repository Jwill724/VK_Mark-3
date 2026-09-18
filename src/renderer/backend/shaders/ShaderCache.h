#pragma once

#include "../pipelines/PipelineTable.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <shared_mutex>

struct ShaderRecord
{
	std::string                        spvPath;
	std::string                        sourcePath;
	Vulkan_ShaderStage                 stage = Vulkan_ShaderStage::COMPUTE_STAGE;
	uint64_t                           hash = 0ull;
	std::vector<std::string>           includes;
	std::vector<RD::Renderer_Pipeline> dependents;
	bool                               isInclude = false;
};

class ShaderCache final
{
public:
	static constexpr const char* SHADER_ROOT = "res/shaders/";
	static constexpr const char* INCLUDE_DIR = "res/shaders/include";
	static constexpr const char* MANIFEST_PATH = "res/shaders/cache/manifest.txt";
	static constexpr uint64_t    OPTIONS_SALT = 0x5643414348453031ull;

	bool BuildFromTable(std::string& outLog);

	const std::vector<uint32_t>& GetSpirvUnlocked(const std::string& spvPath) const;
	std::vector<uint32_t>        SnapshotSpirv(const std::string& spvPath) const;

	static std::string NormalizePath(const std::string& path);
	uint32_t CountIncludeDependents(const std::string& sourcePath) const;

	void CommitBlob(
		const std::string& spvPath,
		std::vector<uint32_t>&& spirv,
		uint64_t                  hash,
		std::vector<std::string>&& includes,
		bool                      writeToDisk,
		bool                      saveManifest = true);

	const std::vector<ShaderRecord>& Records() const noexcept { return m_records; }
	const ShaderRecord* Find(const std::string& spvPath) const;

	uint64_t HashTree(const ShaderRecord& record) const;
	uint64_t HashTree(const std::string& sourcePath,
		const std::vector<std::string>& includes) const;

	static std::string ReadSource(const std::string& path);
	static bool        WriteSource(const std::string& path, const std::string& text);

	void SaveManifest() const;
private:
	uint32_t FindOrCreate(const char* spvPath, Vulkan_ShaderStage stage);

	void LoadManifest();

	bool WriteSpv(const std::string& spvPath, const std::vector<uint32_t>& spirv) const;

	std::vector<ShaderRecord>                m_records;
	std::vector<std::vector<uint32_t>>       m_blobs;
	std::unordered_map<std::string, uint32_t> m_index;

	mutable std::shared_mutex m_blobLock;
};