#include "pch.h"

#include "ShaderCompiler.h"
#include "ShaderCache.h"

#include <shaderc/shaderc.hpp>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace
{
	std::string ReadTextFile(const std::string& path)
	{
		std::ifstream f(path, std::ios::binary | std::ios::ate);
		if (!f.is_open()) return {};
		const size_t size = static_cast<size_t>(f.tellg());
		std::string out(size, '\0');
		f.seekg(0);
		f.read(out.data(), static_cast<std::streamsize>(size));
		return out;
	}

	shaderc_shader_kind KindOf(Vulkan_ShaderStage stage)
	{
		switch (stage)
		{
		case Vulkan_ShaderStage::VERTEX_STAGE:   return shaderc_vertex_shader;
		case Vulkan_ShaderStage::FRAGMENT_STAGE: return shaderc_fragment_shader;
		case Vulkan_ShaderStage::TASK_STAGE:     return shaderc_task_shader;
		case Vulkan_ShaderStage::MESH_STAGE:     return shaderc_mesh_shader;
		default:                                 return shaderc_compute_shader;
		}
	}

	uint32_t ExecutionModelOf(Vulkan_ShaderStage stage)
	{
		switch (stage)
		{
		case Vulkan_ShaderStage::VERTEX_STAGE:   return 0u;
		case Vulkan_ShaderStage::FRAGMENT_STAGE: return 4u;
		case Vulkan_ShaderStage::TASK_STAGE:     return 5364u;
		case Vulkan_ShaderStage::MESH_STAGE:     return 5267u;
		default:                                 return 5u;
		}
	}

	class TrackingIncluder final : public shaderc::CompileOptions::IncluderInterface
	{
	public:
		explicit TrackingIncluder(std::vector<std::string>* sink) : m_sink(sink) {}

		shaderc_include_result* GetInclude(
			const char* requested,
			shaderc_include_type type,
			const char* requester,
			size_t) override
		{
			auto* payload = new Payload();

			fs::path resolved;
			if (type == shaderc_include_type_relative)
				resolved = fs::path(requester).parent_path() / requested;

			std::error_code ec;
			if (resolved.empty() || !fs::exists(resolved, ec))
				resolved = fs::path(ShaderCache::SHADER_ROOT) / "include" / requested;

			if (!fs::exists(resolved, ec))
			{
				payload->content = fmt::format("could not resolve include '{}'", requested);
				payload->result = { "", 0u, payload->content.c_str(), payload->content.size(), payload };
				return &payload->result;
			}

			payload->name = resolved.lexically_normal().generic_string();
			payload->content = ReadTextFile(payload->name);
			m_sink->push_back(payload->name);

			payload->result = {
				payload->name.c_str(), payload->name.size(),
				payload->content.c_str(), payload->content.size(),
				payload };
			return &payload->result;
		}

		void ReleaseInclude(shaderc_include_result* r) override
		{
			delete static_cast<Payload*>(r->user_data);
		}

	private:
		struct Payload
		{
			std::string            name;
			std::string            content;
			shaderc_include_result result{};
		};

		std::vector<std::string>* m_sink;
	};
}

CompileResult ShaderCompiler::Compile(
	const std::string& sourcePath,
	const std::string& sourceText,
	Vulkan_ShaderStage stage,
	bool               optimize)
{
	CompileResult out;

	shaderc::CompileOptions options;
	options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_4);
	options.SetTargetSpirv(shaderc_spirv_version_1_6);
	options.SetSourceLanguage(shaderc_source_language_glsl);
	options.SetOptimizationLevel(
		optimize ? shaderc_optimization_level_performance : shaderc_optimization_level_zero);
	options.SetIncluder(std::make_unique<TrackingIncluder>(&out.includes));

	shaderc::Compiler compiler;
	const auto module = compiler.CompileGlslToSpv(
		sourceText.c_str(), sourceText.size(),
		KindOf(stage), sourcePath.c_str(), "main", options);

	out.log = module.GetErrorMessage();

	if (module.GetCompilationStatus() != shaderc_compilation_status_success)
		return out;

	out.spirv.assign(module.cbegin(), module.cend());
	out.ok = !out.spirv.empty();

	std::sort(out.includes.begin(), out.includes.end());
	out.includes.erase(std::unique(out.includes.begin(), out.includes.end()), out.includes.end());

	return out;
}

bool ShaderCompiler::ValidateSpirv(
	const std::vector<uint32_t>& spirv,
	Vulkan_ShaderStage           stage,
	std::string& outLog)
{
	if (spirv.size() < 5u || spirv[0] != 0x07230203u)
	{
		outLog = "not a SPIR-V module";
		return false;
	}

	const uint32_t wantModel = ExecutionModelOf(stage);
	bool foundEntry = false;

	for (size_t i = 5u; i < spirv.size(); )
	{
		const uint32_t word = spirv[i];
		const uint32_t count = word >> 16u;
		const uint32_t op = word & 0xFFFFu;

		if (count == 0u || i + count > spirv.size())
		{
			outLog = "malformed SPIR-V instruction stream";
			return false;
		}

		if (op == 15u && count >= 3u)
		{
			if (spirv[i + 1u] != wantModel)
			{
				outLog = fmt::format(
					"execution model {} does not match the declared stage ({})",
					spirv[i + 1u], wantModel);
				return false;
			}
			foundEntry = true;
		}
		else if (op == 71u && count >= 4u && spirv[i + 2u] == 34u)
		{
			const uint32_t set = spirv[i + 3u];
			if (set >= static_cast<uint32_t>(RD::DescriptorSlot::Count))
			{
				outLog = fmt::format(
					"shader declares descriptor set {}, layout only provides {}",
					set, static_cast<uint32_t>(RD::DescriptorSlot::Count));
				return false;
			}
		}

		i += count;
	}

	if (!foundEntry)
	{
		outLog = "no OpEntryPoint found";
		return false;
	}

	return true;
}