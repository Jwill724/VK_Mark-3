#pragma once

#include "../VulkanTypes.h"
#include <string>
#include <vector>

struct CompileResult
{
	bool                     ok = false;
	std::vector<uint32_t>    spirv;
	std::vector<std::string> includes;
	std::string              log;
};

namespace ShaderCompiler
{
	CompileResult Compile(
		const std::string& sourcePath,
		const std::string& sourceText,
		Vulkan_ShaderStage stage,
		bool               optimize);

	bool ValidateSpirv(
		const std::vector<uint32_t>& spirv,
		Vulkan_ShaderStage           stage,
		std::string& outLog);
}