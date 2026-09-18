#include "pch.h"

#include "../../RenderPasses.h"
#include "../../scopes/ComputeScope.h"
#include "../../../backend/ImageUtils.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../backend/descriptors/DescriptorWriter.h"

namespace I = ImageUtils;

static constexpr size_t PIPE_ID_HDR_CUBEMAP    = 0;
static constexpr size_t PIPE_ID_SH_IRRADIANCE  = 1;
static constexpr size_t PIPE_ID_SPECULAR       = 2;
static constexpr size_t PIPE_ID_BRDF           = 3;

struct alignas(16) ShIrrPush
{
	uint32_t setIndex = UINT32_MAX;
	uint32_t pad0[3]{};
};

struct alignas(16) BrdfPush
{
	uint32_t sampleCountU = 0u;
	uint32_t pad0[3]{};
};

void BakeEnvironmentMaps(
	VkCommandBuffer cmd,
	BindlessImageTable& imageTable,
	std::span<const PipelineHandle> pipelines)
{
	ASSERT(pipelines.size() >= PIPE_ID_BRDF + 1);

	ComputeScope pso{{}};

	PushDescriptorWriter pushWriter;

	//const auto skyboxSampler     = imageTable.GetSampler(RD::Renderer_Sampler::Skybox);
	//const auto specSampler       = imageTable.GetSampler(RD::Renderer_Sampler::Specular);
	//const auto equirectSampler   = imageTable.GetSampler(RD::Renderer_Sampler::Equirect);
	//const auto setCount = imageTable.EnvironmentSetCount();

	//ShIrrPush shIrrPush;
	//
	//for (uint32_t i = 0; i < setCount; ++i)
	//{
	//	const auto& envSet = imageTable.GetEnvironmentSet(i);
	//	ASSERT(envSet.IsValid());

	//	shIrrPush.setIndex = envSet.setIndex;

	//	ASSERT(shIrrPush.setIndex != UINT32_MAX);

	//	auto& equirect   = envSet.equirect;
	//	auto& specular   = envSet.specular;
	//	auto& skybox     = envSet.skybox;

	//	I::TransitionLayout(cmd, skybox,     RD::ImageAccess::Undefined,   RD::ImageAccess::Write);
	//	I::TransitionLayout(cmd, specular,   RD::ImageAccess::Undefined,   RD::ImageAccess::Write);

	//	// =========================
	//	// HDR Equirect to cubemap
	//	// =========================

	//	pso.SetPush(envSet.skyScale);

	//	pso.BindReadImage(pushWriter,  RD::PUSH_BINDING_READ_1,  equirect, equirectSampler);
	//	pso.BindWriteImage(pushWriter, RD::PUSH_BINDING_WRITE_1, skybox, 0);

	//	pso.UpdateExtent({ skybox.Width(), skybox.Height() });
	//	pso.UpdateWorkgroups({ 16, 16, 6 });

	//	pso.DispatchComputePass(cmd, pipelines[PIPE_ID_HDR_CUBEMAP], pushWriter);

	//	I::TransitionLayout(cmd, skybox, RD::ImageAccess::Write, RD::ImageAccess::Read);
	//	I::GenerateCubemapMipLevels(cmd, skybox);

	//	// ======================
	//	// SH Irradiance
	//	// ======================

	//	pso.BindReadImage(pushWriter, RD::PUSH_BINDING_READ_1,  skybox, skyboxSampler);
	//	pso.SetPush(shIrrPush);

	//	pso.UpdateWorkgroups({ 64, 1, 1 });
	//	pso.UpdateExtent({ 64u, 1u });

	//	pso.DispatchComputePass(cmd, pipelines[PIPE_ID_SH_IRRADIANCE], pushWriter);

	//	pso.ClearPush();

	//	// ========================
	//	// Specular prefilter
	//	// ========================

	//	pso.UpdateWorkgroups({8, 8, 6});
	//	for (uint32_t mip = 0; mip < specular.m_mipLevels; ++mip)
	//	{
	//		pso.BindReadImage(pushWriter,  RD::PUSH_BINDING_READ_1,  skybox,  skyboxSampler);
	//		pso.BindWriteImage(pushWriter, RD::PUSH_BINDING_WRITE_1, specular, mip);

	//		pso.UpdateExtent({ envSet.specularPCs[mip].width, envSet.specularPCs[mip].height });
	//		pso.SetPush(envSet.specularPCs[mip]);

	//		pso.DispatchComputePass(cmd, pipelines[PIPE_ID_SPECULAR], pushWriter);
	//	}

	//	I::TransitionLayout(cmd, specular, RD::ImageAccess::Write, RD::ImageAccess::Read);
	//}
	//
	//pso.ClearPush();

	// ======================
	// BRDF LUT
	// ======================

	const auto& brdf = imageTable.GetStaticTexture(RD::Renderer_Texture::Brdf);
	BrdfPush brdfPush;
	brdfPush.sampleCountU = RD::PREFILTER_SAMPLE_COUNT;

	I::TransitionLayout(cmd, brdf, RD::ImageAccess::Undefined, RD::ImageAccess::Write);

	pso.BindWriteImage(pushWriter, RD::PUSH_BINDING_WRITE_1, brdf);
	pso.UpdateExtent({ brdf.Width(), brdf.Height() });
	pso.UpdateWorkgroups(WORKGROUP_8x8);
	pso.SetPush(brdfPush);

	pso.DispatchComputePass(cmd, pipelines[PIPE_ID_BRDF], pushWriter);

	I::TransitionLayout(cmd, brdf, RD::ImageAccess::Write, RD::ImageAccess::Read);
}
