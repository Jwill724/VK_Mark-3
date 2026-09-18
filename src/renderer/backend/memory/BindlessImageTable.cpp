#include "pch.h"
#include <stdexcept>
#include <fstream>

#include "BindlessImageTable.h"
#include "ImageSpecs.h"
#include "ResourceAllocator.h"
#include "../ImageUtils.h"
#include "Staging.h"
#include "TextureStaging.h"
#include "../../scene/LightUnits.h"

namespace IS = ImageSpecs;

static ImageDesc CSMAtlasDesc(uint32_t res)
{
	return IS::MakeImageDesc(
		IS::RenderTarget(RD::Renderer_RenderTarget::DirectionalCSMAtlas),
		IS::ImageExtentContext{ {}, res });
}

static size_t Index(RD::Renderer_RenderTarget slot) noexcept
{
	ASSERT(static_cast<size_t>(slot) < RD::RENDER_TARGET_COUNT);
	return static_cast<size_t>(slot);
}
static size_t Index(RD::Renderer_Texture slot) noexcept
{
	ASSERT(static_cast<size_t>(slot) < RD::STATIC_TEXTURE_COUNT);
	return static_cast<size_t>(slot);
}
static size_t Index(RD::Renderer_Sampler slot) noexcept
{
	ASSERT(static_cast<size_t>(slot) < RD::SAMPLER_COUNT);
	return static_cast<size_t>(slot);
}

static Vulkan_Format ResolveAssetFormat(const TextureDesc& desc)
{
	switch (desc.format)
	{
	case TextureFormat::BC7:
		return desc.isSRGB ? Vulkan_Format::BC7srgb : Vulkan_Format::BC7unorm;
	case TextureFormat::BC5:
		return Vulkan_Format::BC5unorm;
	default:
		return desc.isSRGB ? Vulkan_Format::RGBA8srgb : Vulkan_Format::RGBA8unorm;
	}
}

static uint32_t ClampMipCount(Extents3D extent, uint32_t requested)
{
	uint32_t maxDim = std::max(extent.Width(), extent.Height());
	uint32_t mips = 1u;
	while (maxDim > 1u) { maxDim >>= 1; ++mips; }
	return std::min(requested, mips);
}

// --------
// Hilbert
// --------

static constexpr uint32_t HILBERT_LEVEL = 6u;
static constexpr uint32_t HILBERT_WIDTH = 1u << HILBERT_LEVEL;

static uint32_t HilbertIndex(uint32_t posX, uint32_t posY)
{
	uint32_t index = 0u;

	for (uint32_t curLevel = HILBERT_WIDTH / 2u;
		curLevel > 0u;
		curLevel /= 2u)
	{
		const uint32_t regionX = (posX & curLevel) != 0u;
		const uint32_t regionY = (posY & curLevel) != 0u;

		index += curLevel * curLevel * ((3u * regionX) ^ regionY);

		if (regionY == 0u)
		{
			if (regionX == 1u)
			{
				posX = HILBERT_WIDTH - 1u - posX;
				posY = HILBERT_WIDTH - 1u - posY;
			}

			const uint32_t temp = posX;
			posX = posY;
			posY = temp;
		}
	}

	return index;
}

// Headerless, interleaved RG8 atlas. Using raw bytes avoids image-loader
// flip, channel conversion, and gamma settings changing the STBN sequence.
static std::vector<uint8_t> LoadShadowSTBNAtlas()
{
	constexpr size_t atlasBytes = 1024u * 1024u * 2u;
	constexpr const char* path = "res/assets/noise/shadow_stbn_128x128x64.rg8";

	std::ifstream input(path, std::ios::binary | std::ios::ate);
	if (!input)
		throw std::runtime_error("Could not open shadow STBN atlas: res/assets/noise/shadow_stbn_128x128x64.rg8");

	if (input.tellg() != static_cast<std::streamoff>(atlasBytes))
		throw std::runtime_error("Shadow STBN atlas must contain exactly 2097152 bytes (1024x1024 RG8)");

	std::vector<uint8_t> pixels(atlasBytes);
	input.seekg(0, std::ios::beg);
	if (!input.read(reinterpret_cast<char*>(pixels.data()), static_cast<std::streamsize>(pixels.size())))
		throw std::runtime_error("Failed to read shadow STBN atlas");

	return pixels;
}

// ---------
// Rainbow
// ---------

static glm::vec3 CieXyzFit(float w)
{
	auto lobe = [](float x, float mu, float s1, float s2)
		{
			float t = (x - mu) * ((x < mu) ? s1 : s2);
			return std::exp(-0.5f * t * t);
		};

	return {
		0.362f * lobe(w, 442.0f, 0.0624f, 0.0374f)
	  + 1.056f * lobe(w, 599.8f, 0.0264f, 0.0323f)
	  - 0.065f * lobe(w, 501.1f, 0.0490f, 0.0382f),

		0.821f * lobe(w, 568.8f, 0.0213f, 0.0247f)
	  + 0.286f * lobe(w, 530.9f, 0.0613f, 0.0322f),

		1.217f * lobe(w, 437.0f, 0.0845f, 0.0278f)
	  + 0.681f * lobe(w, 459.0f, 0.0385f, 0.0725f)
	};
}


// ======
// INIT
// ======

void BindlessImageTable::Init(
	Extents3D drawExtent,
	RD::ShadowQuality shadowQuality,
	VkDevice  device,
	Allocator& allocator)
{
	CreateSamplers(device);
	CreateRenderTargets(drawExtent, allocator);
	CreateShadowMaps(shadowQuality, allocator);
	CreateFroxelFogTargets(allocator);
	CreateAtmosphereTargets(allocator);
	CreateRenderTargetGroup(IS::ImageGroup::WorldProbes, allocator);
	CreateStaticTextures(allocator);
}

void BindlessImageTable::Shutdown(VkDevice device, Allocator& allocator)
{
	ClearDescriptorArrays();
	FreeRenderTargets(allocator);
	FreeShadowMaps(allocator);
	FreeFroxelFogTargets(allocator);
	FreeAtmosphereTargets(allocator);
	FreeRenderTargetGroup(IS::ImageGroup::WorldProbes, allocator);
	FreeStaticTextures(allocator);
	FreeSamplers(device);
	m_assetTextures.clear();
}

// ===============
// RENDER TARGETS
// ===============

void BindlessImageTable::CreateRenderTargetGroup(IS::ImageGroup group, Allocator& allocator)
{
	const IS::ImageExtentContext ctx{ m_drawExtent, m_csmAtlasRes };
	const bool updateCombinedRange = IsRenderTargetCombinedRangeBuilt();
	std::unique_lock combinedLock(m_combinedMutex, std::defer_lock);
	if (updateCombinedRange)
		combinedLock.lock();

	for (size_t i = 0; i < RD::RENDER_TARGET_COUNT; ++i)
	{
		const IS::ImageSpec& spec = IS::kRenderTargets[i];
		if (spec.group != group) continue;

		AllocatedImage image = allocator.AllocateImage(IS::MakeImageDesc(spec, ctx));

		if (updateCombinedRange)
		{
			const auto slot = static_cast<RD::Renderer_RenderTarget>(i);
			const uint32_t bindlessID = GetRenderTargetCombinedID(slot);
			image.m_bindlessID = bindlessID;
			m_renderTargets[i] = std::move(image);
			if (bindlessID != UINT32_MAX)
				UpdateCombinedLocked(
					bindlessID,
					m_renderTargets[i].m_imageView,
					ResolveRenderTargetSampler(slot));
		}
		else
		{
			m_renderTargets[i] = std::move(image);
		}
	}

	MarkDirty();
}

void BindlessImageTable::FreeRenderTargetGroup(IS::ImageGroup group, Allocator& allocator)
{
	for (size_t i = 0; i < RD::RENDER_TARGET_COUNT; ++i)
	{
		if (IS::kRenderTargets[i].group != group) continue;

		AllocatedImage& img = m_renderTargets[i];
		if (!img.IsValid()) continue;

		allocator.FreeImage(img);
		img.Reset();
	}

	MarkDirty();
}

void BindlessImageTable::CreateRenderTargets(Extents3D drawExtent, Allocator& allocator)
{
	m_drawExtent = drawExtent;
	CreateRenderTargetGroup(IS::ImageGroup::Resolution, allocator);
}

void BindlessImageTable::FreeRenderTargets(Allocator& allocator)
{
	FreeRenderTargetGroup(IS::ImageGroup::Resolution, allocator);
}

void BindlessImageTable::CreateShadowMaps(RD::ShadowQuality quality, Allocator& allocator)
{
	if (m_bAreShadowsCreated) return;

	m_csmAtlasRes = RD::EvaluateShadowQuality(quality);
	CreateRenderTargetGroup(IS::ImageGroup::ShadowMap, allocator);
	m_bAreShadowsCreated = true;
}

void BindlessImageTable::FreeShadowMaps(Allocator& allocator)
{
	FreeRenderTargetGroup(IS::ImageGroup::ShadowMap, allocator);
	m_cachedCsmAtlasInfo = {};
	m_csmAtlasRes = 0u;
	m_bAreShadowsCreated = false;
}

void BindlessImageTable::CreateAtmosphereTargets(Allocator& allocator)
{
	if (m_bAreAtmosphereTargetsCreated) return;

	CreateRenderTargetGroup(IS::ImageGroup::Atmosphere, allocator);
	m_bAreAtmosphereTargetsCreated = true;
}

void BindlessImageTable::FreeAtmosphereTargets(Allocator& allocator)
{
	if (!m_bAreAtmosphereTargetsCreated) return;

	FreeRenderTargetGroup(IS::ImageGroup::Atmosphere, allocator);
	m_bAreAtmosphereTargetsCreated = false;
}

void BindlessImageTable::CreateFroxelFogTargets(Allocator& allocator)
{
	if (m_bAreFroxelFogCreated) return;

	CreateRenderTargetGroup(IS::ImageGroup::FroxelFog, allocator);
	m_bAreFroxelFogCreated = true;
}

void BindlessImageTable::FreeFroxelFogTargets(Allocator& allocator)
{
	if (!m_bAreFroxelFogCreated) return;

	FreeRenderTargetGroup(IS::ImageGroup::FroxelFog, allocator);
	m_bAreFroxelFogCreated = false;
}

void BindlessImageTable::CreateStaticTextures(Allocator& allocator)
{
	for (size_t i = 0; i < RD::STATIC_TEXTURE_COUNT; ++i)
	{
		const IS::ImageSpec& spec = IS::kStaticTextures[i];
		if (spec.group == IS::ImageGroup::Unused) continue;

		m_staticTextures[i] = allocator.AllocateImage(IS::MakeImageDesc(spec));
	}

	MarkDirty();
}

void BindlessImageTable::FreeCSMAtlas(Allocator& allocator)
{
	ASSERT(m_bAreShadowsCreated);

	if (m_cachedCsmAtlasInfo.isActive) return;

	AllocatedImage& atlas = m_renderTargets[Index(RD::Renderer_RenderTarget::DirectionalCSMAtlas)];

	ASSERT(atlas.IsValid());
	ASSERT(atlas.m_bindlessID != UINT32_MAX);
	ASSERT(atlas.m_bindlessID < static_cast<uint32_t>(m_combinedViews.size()));

	m_cachedCsmAtlasInfo.csmAtlasBindlessID = atlas.m_bindlessID;

	const uint32_t bindlessID = m_cachedCsmAtlasInfo.csmAtlasBindlessID;

	AllocatedImage placeholder = allocator.AllocateImage(CSMAtlasDesc(1u));
	ASSERT(placeholder.IsValid());

	placeholder.m_bindlessID = bindlessID;

	{
		std::scoped_lock lock(m_combinedMutex);
		UpdateCombinedLocked(bindlessID, placeholder.m_imageView, GetSampler(RD::Renderer_Sampler::ShadowMap));
	}

	allocator.FreeImage(atlas);
	atlas = std::move(placeholder);

	m_cachedCsmAtlasInfo.isActive = true;
	MarkDirty();
}

void BindlessImageTable::RecreateCSMAtlas(Allocator& allocator)
{
	ASSERT(m_bAreShadowsCreated);

	if (!m_cachedCsmAtlasInfo.isActive) return;

	AllocatedImage& atlas = m_renderTargets[Index(RD::Renderer_RenderTarget::DirectionalCSMAtlas)];
	ASSERT(atlas.IsValid());

	const uint32_t bindlessID = m_cachedCsmAtlasInfo.csmAtlasBindlessID;
	ASSERT(bindlessID != UINT32_MAX);
	ASSERT(bindlessID < static_cast<uint32_t>(m_combinedViews.size()));

	AllocatedImage restoredAtlas = allocator.AllocateImage(CSMAtlasDesc(m_csmAtlasRes));
	ASSERT(restoredAtlas.IsValid());

	restoredAtlas.m_bindlessID = bindlessID;

	{
		std::scoped_lock lock(m_combinedMutex);
		UpdateCombinedLocked(bindlessID, restoredAtlas.m_imageView, GetSampler(RD::Renderer_Sampler::ShadowMap));
	}

	allocator.FreeImage(atlas);
	atlas = std::move(restoredAtlas);

	m_cachedCsmAtlasInfo.isActive = false;
	MarkDirty();
}

void BindlessImageTable::UpdateCSMAtlasExtent(RD::ShadowQuality quality, Allocator& allocator)
{
	ASSERT(m_bAreShadowsCreated && "UpdateCSMAtlasExtent: shadow maps not created");

	m_csmAtlasRes = RD::EvaluateShadowQuality(quality);

	if (m_cachedCsmAtlasInfo.isActive) return;

	AllocatedImage& atlas = m_renderTargets[Index(RD::Renderer_RenderTarget::DirectionalCSMAtlas)];
	ASSERT(atlas.IsValid() && "UpdateCSMAtlasExtent: CSM atlas image is invalid");

	const uint32_t bindlessID = atlas.m_bindlessID;

	allocator.FreeImage(atlas);
	atlas.Reset();

	atlas = allocator.AllocateImage(CSMAtlasDesc(m_csmAtlasRes));
	atlas.m_bindlessID = bindlessID;

	{
		std::scoped_lock lock(m_combinedMutex);
		UpdateCombinedLocked(bindlessID, atlas.m_imageView, GetSampler(RD::Renderer_Sampler::ShadowMap));
	}

	MarkDirty();
}

void BindlessImageTable::UpdateRenderTargets(Extents3D drawExtent, Allocator& allocator)
{
	FreeRenderTargets(allocator);
	CreateRenderTargets(drawExtent, allocator);
}

void BindlessImageTable::SetRenderTarget(RD::Renderer_RenderTarget slot, AllocatedImage image)
{
	const size_t index = Index(slot);

	if (IsRenderTargetCombinedRangeBuilt())
	{
		const uint32_t bindlessID = GetRenderTargetCombinedID(slot);
		image.m_bindlessID = bindlessID;
		m_renderTargets[index] = std::move(image);

		std::scoped_lock lock(m_combinedMutex);
		if (bindlessID != UINT32_MAX)
			UpdateCombinedLocked(
				bindlessID,
				m_renderTargets[index].m_imageView,
				ResolveRenderTargetSampler(slot));
	}
	else
	{
		m_renderTargets[index] = std::move(image);
	}

	MarkDirty();
}

const AllocatedImage& BindlessImageTable::GetRenderTarget(RD::Renderer_RenderTarget slot) const
{
	return m_renderTargets[Index(slot)];
}

void BindlessImageTable::TransitionRenderTargetsFromUndefined(VkCommandBuffer cmd)
{
	std::vector<VkImageMemoryBarrier2> barriers;
	barriers.reserve(RD::RENDER_TARGET_COUNT);

	for (size_t i = 0; i < RD::RENDER_TARGET_COUNT; ++i)
	{
		if (IS::kRenderTargets[i].group != IS::ImageGroup::Resolution) continue;

		const AllocatedImage& img = m_renderTargets[i];
		if (!img.IsValid()) continue;

		const bool isDepth = (img.m_aspect == ImageAspect::Depth);
		VkImageAspectFlags aspectMask = isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

		barriers.emplace_back(VkImageMemoryBarrier2{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
			.srcAccessMask = VK_ACCESS_2_NONE,
			.dstStageMask = isDepth
								? VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
								: VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
			.dstAccessMask = isDepth
								? VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT
								: VK_ACCESS_2_SHADER_READ_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = isDepth
								? VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL
								: VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			.image = img.m_image,
			.subresourceRange = {
				aspectMask,
				0, VK_REMAINING_MIP_LEVELS,
				0, VK_REMAINING_ARRAY_LAYERS
			}
			});
	}

	VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	dep.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
	dep.pImageMemoryBarriers = barriers.data();
	vkCmdPipelineBarrier2(cmd, &dep);
}

// =========
// SAMPLERS
// =========

void BindlessImageTable::CreateSamplers(VkDevice device)
{
	SetSampler(RD::Renderer_Sampler::NearestClamp,
		ImageUtils::CreateSampler(device, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			0.0f, 1.0f, VK_SAMPLER_MIPMAP_MODE_NEAREST));

	SetSampler(RD::Renderer_Sampler::LinearClamp,
		ImageUtils::CreateSampler(device, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			RD::MAX_MIP_LEVELS, 1.0f));

	SetSampler(RD::Renderer_Sampler::HiZ,
		ImageUtils::CreateSampler(device, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			static_cast<float>(RD::HI_Z_MIP_COUNT - 1), 1.0f, VK_SAMPLER_MIPMAP_MODE_NEAREST));

	SetSampler(RD::Renderer_Sampler::LinearLodClamp,
		ImageUtils::CreateSampler(device, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			RD::MAX_MIP_LEVELS, 1.0f, VK_SAMPLER_MIPMAP_MODE_LINEAR));

	SetSampler(RD::Renderer_Sampler::PointBorder,
		ImageUtils::CreateSampler(device, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
			0.0f, 1.0f, VK_SAMPLER_MIPMAP_MODE_NEAREST));

	SetSampler(RD::Renderer_Sampler::TaaHistory,
		ImageUtils::CreateSampler(device, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			0.0f, 1.0f, VK_SAMPLER_MIPMAP_MODE_NEAREST));

	SetSampler(RD::Renderer_Sampler::Noise,
		ImageUtils::CreateSampler(device, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT,
			0.0f, 1.0f, VK_SAMPLER_MIPMAP_MODE_NEAREST));

	SetSampler(RD::Renderer_Sampler::ShadowMap,
		ImageUtils::CreateSampler(device, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			0.0f, 1.0f, VK_SAMPLER_MIPMAP_MODE_NEAREST));

	SetSampler(RD::Renderer_Sampler::Linear,
		ImageUtils::CreateSampler(device, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT,
			RD::MAX_MIP_LEVELS, RD::MAX_ANISOTROPY_LEVEL));

	SetSampler(RD::Renderer_Sampler::Nearest,
		ImageUtils::CreateSampler(device, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT,
			0.0f, 1.0f, VK_SAMPLER_MIPMAP_MODE_NEAREST));

	SetSampler(RD::Renderer_Sampler::Brdf,
		ImageUtils::CreateSampler(device, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			0.0f, 1.0f));

	SetSampler(RD::Renderer_Sampler::Equirect,
		ImageUtils::CreateSamplerAddr(device, VK_FILTER_LINEAR,
			VK_SAMPLER_ADDRESS_MODE_REPEAT,
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			VK_LOD_CLAMP_NONE, 1.0f));

	SetSampler(RD::Renderer_Sampler::Specular,
		ImageUtils::CreateSampler(device, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			static_cast<float>(RD::SPECULAR_PREFILTERED_MIP_LEVELS - 1), 0.0f));

	SetSampler(RD::Renderer_Sampler::Skybox,
		ImageUtils::CreateSampler(device, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			VK_LOD_CLAMP_NONE, 0.0f));
}

void BindlessImageTable::FreeSamplers(VkDevice device)
{
	for (auto& smp : m_samplers)
	{
		if (smp != VK_NULL_HANDLE)
			vkDestroySampler(device, smp, nullptr);
	}
	m_samplers.fill(VK_NULL_HANDLE);

	// TODO: Move asset sampler cleanup somewhere else.
	// Asset samplers are dynamic — destroy all and clear
	for (auto& smp : m_assetSamplers)
		if (smp != VK_NULL_HANDLE)
			vkDestroySampler(device, smp, nullptr);

	m_assetSamplers.clear();
	m_assetSamplerDescs.clear();
}

void BindlessImageTable::SetSampler(RD::Renderer_Sampler slot, VkSampler sampler)
{
	m_samplers[Index(slot)] = sampler;
}

VkSampler BindlessImageTable::GetSampler(RD::Renderer_Sampler slot) const
{
	return m_samplers[Index(slot)];
}

// ================
// STATIC TEXTURES
// ================

size_t BindlessImageTable::CalcStaticTexturesStagingSize() const
{
	size_t total = 0;
	for (const auto& tex : m_staticTextures)
	{
		if (!tex.IsValid()) continue;
		const size_t w = tex.Width();
		const size_t h = tex.Height();
		const size_t d = std::max(tex.Depth(), 1u);
		const size_t pixels = w * h * d * tex.m_pixelBytes;
		total += AllocatedBuffer::AlignUp(pixels, static_cast<size_t>(16));
	}
	return total;
}

void BindlessImageTable::UploadStaticTextures(StagingBuffer& staging, VkCommandBuffer cmd)
{
	auto& st = m_staticTextures;

	// --- 1x1 RGBA8 trivial textures ---
	const uint32_t white = glm::packUnorm4x8(glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
	const uint32_t flatNormal = glm::packUnorm4x8(glm::vec4(0.5f, 0.5f, 1.0f, 1.0f));
	const uint32_t black = glm::packUnorm4x8(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
	const uint8_t  metalRough[4] = { 0, static_cast<uint8_t>(0.5f * 255), 0, 255 };
	const uint32_t dummy = 0u;
	const uint8_t  dummyU8 = 0u;

	// --- Checkerboard 16x16 RGBA8 ---
	const uint32_t magenta = glm::packUnorm4x8(glm::vec4(1, 0, 1, 1));
	std::array<uint32_t, 16 * 16> checkerboard{};
	for (int x = 0; x < 16; ++x)
		for (int y = 0; y < 16; ++y)
			checkerboard[static_cast<size_t>(y * 16 + x)] = ((x % 2) ^ (y % 2)) ? magenta : black;

	// --- Rainbow LUT 256x1 RGBA8 ---
	constexpr float kRainbowSaturation = 1.35f;

	std::vector<uint32_t> rainbowLut(256);
	for (uint32_t x = 0; x < 256; ++x)
	{
		float t = static_cast<float>(x) / 255.0f;
		float wavelength = glm::mix(420.0f, 660.0f, t);

		glm::vec3 xyz = CieXyzFit(wavelength);

		glm::vec3 rgb{
			 3.2404542f * xyz.x - 1.5371385f * xyz.y - 0.4985314f * xyz.z,
			-0.9692660f * xyz.x + 1.8760108f * xyz.y + 0.0415560f * xyz.z,
			 0.0556434f * xyz.x - 0.2040259f * xyz.y + 1.0572252f * xyz.z };

		float lowest = std::min({ rgb.r, rgb.g, rgb.b });
		if (lowest < 0.0f) rgb -= glm::vec3(lowest);

		rgb /= std::max({ rgb.r, rgb.g, rgb.b, 1e-4f });

		float lum = glm::dot(rgb, glm::vec3(0.2126f, 0.7152f, 0.0722f));
		rgb = glm::clamp(glm::mix(glm::vec3(lum), rgb, kRainbowSaturation), 0.0f, 1.0f);

		rainbowLut[x] = glm::packUnorm4x8(glm::vec4(rgb, 1.0f));
	}

	// --- Hilbert curve LUT 64x64 R16U ---
	std::vector<uint16_t> hilbertLut(64 * 64);
	for (int x = 0; x < 64; ++x)
		for (int y = 0; y < 64; ++y)
			hilbertLut[static_cast<size_t>(x + 64 * y)] = static_cast<uint16_t>(HilbertIndex(x, y));

	// --- Shadow STBN: all 64 frames, 1024x1024 RG8 atlas ---
	const std::vector<uint8_t> shadowSTBN = LoadShadowSTBNAtlas();

	// --- Cookie gobo R8 from disk ---
	int cookieW, cookieH, cookieCh;
	stbi_uc* cookieData = stbi_load("res/assets/flashlight_cookie/light_cookie.png", &cookieW, &cookieH, &cookieCh, 1);
	ASSERT(cookieData && "Failed to load light_cookie.png");

	TextureUploadDesc uploads[] =
	{
		{.image = &st[Index(RD::Renderer_Texture::White)],           .pixelData = &white,
		  .pixelBytes = st[Index(RD::Renderer_Texture::White)].m_pixelBytes,           .strategy = MipStrategy::GenerateOnGPU },
		{.image = &st[Index(RD::Renderer_Texture::Normal)],          .pixelData = &flatNormal,
		  .pixelBytes = st[Index(RD::Renderer_Texture::Normal)].m_pixelBytes,          .strategy = MipStrategy::GenerateOnGPU },
		{.image = &st[Index(RD::Renderer_Texture::MetalRough)],      .pixelData = metalRough,
		  .pixelBytes = st[Index(RD::Renderer_Texture::MetalRough)].m_pixelBytes,      .strategy = MipStrategy::GenerateOnGPU },
		{.image = &st[Index(RD::Renderer_Texture::Dummy)],           .pixelData = &dummy,
		  .pixelBytes = st[Index(RD::Renderer_Texture::Dummy)].m_pixelBytes,           .strategy = MipStrategy::SingleLevel },
		{.image = &st[Index(RD::Renderer_Texture::DummyU8)],         .pixelData = &dummyU8,
		  .pixelBytes = st[Index(RD::Renderer_Texture::DummyU8)].m_pixelBytes,         .strategy = MipStrategy::SingleLevel },
		{.image = &st[Index(RD::Renderer_Texture::DummyVelocity)],   .pixelData = &dummy,
		  .pixelBytes = st[Index(RD::Renderer_Texture::DummyVelocity)].m_pixelBytes,    .strategy = MipStrategy::SingleLevel },
		{.image = &st[Index(RD::Renderer_Texture::Checkerboard)],    .pixelData = checkerboard.data(),
		  .pixelBytes = st[Index(RD::Renderer_Texture::Checkerboard)].m_pixelBytes,    .strategy = MipStrategy::GenerateOnGPU },
		{.image = &st[Index(RD::Renderer_Texture::RainbowLut)],      .pixelData = rainbowLut.data(),
		  .pixelBytes = st[Index(RD::Renderer_Texture::RainbowLut)].m_pixelBytes,      .strategy = MipStrategy::SingleLevel },
		{.image = &st[Index(RD::Renderer_Texture::HilbertCurveLut)], .pixelData = hilbertLut.data(),
		  .pixelBytes = st[Index(RD::Renderer_Texture::HilbertCurveLut)].m_pixelBytes, .strategy = MipStrategy::SingleLevel },
		{.image = &st[Index(RD::Renderer_Texture::ShadowSTBN)],      .pixelData = shadowSTBN.data(),
		  .pixelBytes = st[Index(RD::Renderer_Texture::ShadowSTBN)].m_pixelBytes,    .strategy = MipStrategy::SingleLevel },
		{.image = &st[Index(RD::Renderer_Texture::CookieGobo)],      .pixelData = cookieData,
		  .pixelBytes = st[Index(RD::Renderer_Texture::CookieGobo)].m_pixelBytes,      .strategy = MipStrategy::SingleLevel },
	};

	staging.ExecuteTextureBatch(cmd, uploads);

	stbi_image_free(cookieData);
}

void BindlessImageTable::FreeStaticTextures(Allocator& allocator)
{
	for (auto& tex : m_staticTextures)
	{
		if (tex.IsValid())
		{
			allocator.FreeImage(tex);
			tex.Reset();
		}
	}
}

void BindlessImageTable::SetStaticTexture(RD::Renderer_Texture slot, AllocatedImage image)
{
	m_staticTextures[Index(slot)] = std::move(image);
	MarkDirty();
}

const AllocatedImage& BindlessImageTable::GetStaticTexture(RD::Renderer_Texture slot) const
{
	return m_staticTextures[Index(slot)];
}

// =================
// ENVIRONMENT SETS
// =================

void BindlessImageTable::CreateEnvironmentSets(uint32_t setCount, Allocator& allocator)
{
	for (uint32_t i = 0; i < setCount; ++i)
	{
		EnvironmentSet envSet;
		envSet.setIndex = i;
		envSet.skybox = allocator.AllocateImage(IS::MakeImageDesc(IS::kSkybox));
		envSet.specular = allocator.AllocateImage(IS::MakeImageDesc(IS::kSpecular));

		const uint32_t specMips = envSet.specular.m_mipLevels;
		envSet.specularPCs.resize(specMips);
		for (uint32_t mip = 0; mip < specMips; ++mip)
		{
			envSet.specularPCs[mip].roughness = static_cast<float>(mip) / static_cast<float>(specMips - 1);
			envSet.specularPCs[mip].sampleCount = RD::PREFILTER_SAMPLE_COUNT;
			envSet.specularPCs[mip].width = std::max(1u, envSet.specular.Width() >> mip);
			envSet.specularPCs[mip].height = std::max(1u, envSet.specular.Height() >> mip);
		}

		AddEnvironmentSet(std::move(envSet));
	}
}

void BindlessImageTable::PreallocateEquirects(
	std::span<const char* const> hdrPaths,
	Allocator& allocator)
{
	ASSERT(hdrPaths.size() <= m_environmentSets.size());

	for (uint32_t i = 0; i < static_cast<uint32_t>(hdrPaths.size()); ++i)
	{
		int w, h, ch;
		float* pixels = stbi_loadf(hdrPaths[i], &w, &h, &ch, 4);
		ASSERT(pixels && "PreallocateEquirects: failed to probe HDR dimensions");
		stbi_image_free(pixels);

		m_environmentSets[i].equirect = allocator.AllocateImage(ImageDesc{
			.format = Vulkan_Format::RGBA32F,
			.extent = { static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 },
			.usage = Vulkan_ImageUsage::ComputeRWTransfer
			});

		ASSERT(m_environmentSets[i].equirect.IsValid() &&
			"PreallocateEquirects: image allocation failed");
	}
}

static float ComputeSkyScale(const float* pixels, uint32_t width, uint32_t height)
{
	const size_t texelCount = static_cast<size_t>(width) * height;

	std::vector<float> rowWeights(height);
	for (uint32_t y = 0; y < height; ++y)
		rowWeights[y] = std::sin((static_cast<float>(y) + 0.5f) / static_cast<float>(height) * glm::pi<float>());

	auto weightedMean = [&](float rejectAbove) -> float
		{
			double sum = 0.0;
			double weightSum = 0.0;

			for (uint32_t y = 0; y < height; ++y)
			{
				const float w = rowWeights[y];
				const float* row = pixels + static_cast<size_t>(y) * width * 4u;

				for (uint32_t x = 0; x < width; ++x)
				{
					const float* px = row + static_cast<size_t>(x) * 4u;
					const float lum = 0.2126f * px[0] + 0.7152f * px[1] + 0.0722f * px[2];

					if (lum > rejectAbove) continue;

					sum += static_cast<double>(lum) * w;
					weightSum += w;
				}
			}

			return weightSum > 0.0 ? static_cast<float>(sum / weightSum) : 0.0f;
		};

	const float rawMean = weightedMean(std::numeric_limits<float>::max());
	if (rawMean <= 1e-6f) return 1.0f;

	const float skyMean = weightedMean(rawMean * 50.0f);

	return LightUnits::SKY_CLEAR_DAY / std::max(skyMean, 1e-6f);
}

void BindlessImageTable::UploadEquirects(
	std::span<const char* const> hdrPaths,
	Allocator& allocator,
	VkCommandBuffer              cmd)
{
	ASSERT(hdrPaths.size() <= m_environmentSets.size());

	struct LoadedHDR
	{
		float* pixels = nullptr;
		uint32_t envSetIndex = 0;
	};

	std::vector<LoadedHDR>         loaded;
	std::vector<TextureUploadDesc> uploads;
	loaded.reserve(hdrPaths.size());
	uploads.reserve(hdrPaths.size());

	for (uint32_t i = 0; i < static_cast<uint32_t>(hdrPaths.size()); ++i)
	{
		ASSERT(m_environmentSets[i].equirect.IsValid() &&
			"UploadEquirects: equirect not preallocated — call PreallocateEquirects first");

		int w, h, ch;
		float* pixels = stbi_loadf(hdrPaths[i], &w, &h, &ch, 4);
		ASSERT(pixels && "UploadEquirects: failed to load HDR pixels");

		m_environmentSets[i].skyScale = ComputeSkyScale(
			pixels, static_cast<uint32_t>(w), static_cast<uint32_t>(h));

		loaded.push_back({ pixels, i });
		uploads.emplace_back(TextureUploadDesc{
			.image = &m_environmentSets[i].equirect,
			.pixelData = pixels,
			.pixelBytes = m_environmentSets[i].equirect.m_pixelBytes,
			.strategy = MipStrategy::SingleLevel
			});
	}

	allocator.GlobalStaging.ExecuteTextureBatch(cmd, uploads);

	for (auto& l : loaded)
		stbi_image_free(l.pixels);
}

void BindlessImageTable::FreeEquirects(Allocator& allocator)
{
	for (auto& env : m_environmentSets)
	{
		if (env.equirect.IsValid())
		{
			allocator.FreeImage(env.equirect);
			env.equirect.Reset();
		}
	}
}

void BindlessImageTable::FreeEnvironmentSets(Allocator& allocator)
{
	for (auto& env : m_environmentSets)
	{
		if (!env.IsValid()) continue;
		ASSERT(!env.equirect.IsValid() && "FreeEquirect must be called before shutdown");
		if (env.skybox.IsValid())     allocator.FreeImage(env.skybox);
		if (env.specular.IsValid())   allocator.FreeImage(env.specular);
		env.Reset();
	}
}

void BindlessImageTable::AddEnvironmentSet(EnvironmentSet envSet)
{
	for (uint32_t i = 0; i < static_cast<uint32_t>(m_environmentSets.size()); ++i)
	{
		if (!m_environmentSets[i].IsValid())
		{
			envSet.setIndex = i;
			m_environmentSets[i] = std::move(envSet);
			MarkDirty();
			return;
		}
	}
	ASSERT(false && "No free EnvironmentSet slots");
}

const EnvironmentSet& BindlessImageTable::GetEnvironmentSet(uint32_t index) const
{
	ASSERT(index < static_cast<uint32_t>(m_environmentSets.size()));
	return m_environmentSets[index];
}

EnvironmentSet& BindlessImageTable::GetEnvironmentSetMutable(uint32_t index)
{
	ASSERT(index < static_cast<uint32_t>(m_environmentSets.size()));
	return m_environmentSets[index];
}

uint32_t BindlessImageTable::EnvironmentSetCount() const noexcept
{
	return static_cast<uint32_t>(
		std::ranges::count_if(m_environmentSets, [](const EnvironmentSet& e) { return e.IsValid(); }));
}

// ===============
// ASSET TEXTURES
// ===============

uint32_t BindlessImageTable::PushAssetTexture(AllocatedImage image)
{
	ASSERT(image.IsValid());
	for (uint32_t i = 0; i < static_cast<uint32_t>(m_assetTextures.size()); ++i)
	{
		if (!m_assetTextures[i].IsValid())
		{
			m_assetTextures[i] = std::move(image);
			MarkDirty();
			return i;
		}
	}
	const uint32_t idx = static_cast<uint32_t>(m_assetTextures.size());
	m_assetTextures.push_back(std::move(image));
	MarkDirty();
	return idx;
}

const AllocatedImage& BindlessImageTable::GetAssetTexture(uint32_t index) const
{
	ASSERT(index < static_cast<uint32_t>(m_assetTextures.size()) && m_assetTextures[index].IsValid());
	return m_assetTextures[index];
}

void BindlessImageTable::FreeAssetTexture(uint32_t index)
{
	ASSERT(index < static_cast<uint32_t>(m_assetTextures.size()) && m_assetTextures[index].IsValid());
	m_assetTextures[index].Reset();
	MarkDirty();
}

AllocatedImage& BindlessImageTable::GetAssetTextureMutable(uint32_t index)
{
	ASSERT(index < static_cast<uint32_t>(m_assetTextures.size()));
	return m_assetTextures[index];
}

bool BindlessImageTable::IsAssetTextureValid(uint32_t index) const noexcept
{
	return index < static_cast<uint32_t>(m_assetTextures.size())
		&& m_assetTextures[index].IsValid();
}

uint32_t BindlessImageTable::ResolveAssetSampler(const SamplerDesc& desc, VkDevice device)
{
	for (uint32_t i = 0; i < static_cast<uint32_t>(m_assetSamplers.size()); ++i)
	{
		if (m_assetSamplerDescs[i].isLinear == desc.isLinear &&
			m_assetSamplerDescs[i].isMipMapped == desc.isMipMapped &&
			m_assetSamplerDescs[i].anisotropy == desc.anisotropy)
			return i;
	}

	VkFilter filter = desc.isLinear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
	VkSamplerMipmapMode mipMode = desc.isMipMapped
		? VK_SAMPLER_MIPMAP_MODE_LINEAR
		: VK_SAMPLER_MIPMAP_MODE_NEAREST;
	float maxLod = desc.isMipMapped ? VK_LOD_CLAMP_NONE : 0.0f;

	VkSampler sampler = ImageUtils::CreateSampler(
		device,
		filter,
		VK_SAMPLER_ADDRESS_MODE_REPEAT,
		maxLod,
		desc.anisotropy,
		mipMode);

	const uint32_t slot = static_cast<uint32_t>(m_assetSamplers.size());
	m_assetSamplers.push_back(sampler);
	m_assetSamplerDescs.push_back(desc);
	return slot;
}

VkSampler BindlessImageTable::ResolveDefaultAssetSampler(const AllocatedImage& img) const noexcept
{
	if (img.m_mipLevels <= 1)
		return GetSampler(RD::Renderer_Sampler::LinearClamp);
	return GetSampler(RD::Renderer_Sampler::Linear); // trilinear repeat + aniso
}

std::vector<uint32_t> BindlessImageTable::UploadAssetTextures(
	SceneUploadBatch& batch,
	VkDevice          device,
	Allocator& allocator,
	StagingBuffer& staging,
	VkCommandBuffer   cmd)
{
	std::vector<uint32_t> ownedSlots;
	if (batch.textures.empty()) return ownedSlots;

	struct TexSlot { uint32_t assetIndex; uint32_t tableIndex; };
	std::vector<TexSlot> validSlots;
	validSlots.reserve(batch.textures.size());

	for (uint32_t i = 0; i < static_cast<uint32_t>(batch.textures.size()); ++i)
	{
		TextureDesc& desc = batch.textures[i];
		if (!desc.IsValid())
		{
			desc.bindlessID = GetStaticTexture(RD::Renderer_Texture::Checkerboard).m_bindlessID;
			continue;
		}

		ImageDesc imgDesc{};
		imgDesc.format = ResolveAssetFormat(desc);
		imgDesc.extent = { desc.width, desc.height, 1u };
		imgDesc.usage = Vulkan_ImageUsage::TextureSampled;
		imgDesc.mipLevels = static_cast<uint32_t>(desc.mips.size());
		imgDesc.debugName = desc.debugName.c_str();

		AllocatedImage img = allocator.AllocateImage(imgDesc);

		VkSampler sampler = VK_NULL_HANDLE;
		if (i < static_cast<uint32_t>(batch.samplers.size())
			&& batch.samplers[i].rendererSlot == UINT32_MAX)
		{
			uint32_t slot = ResolveAssetSampler(batch.samplers[i], device);
			sampler = m_assetSamplers[slot];
			batch.samplers[i].rendererSlot = slot;
		}
		if (sampler == VK_NULL_HANDLE)
			sampler = ResolveDefaultAssetSampler(img);

		img.m_bindlessID = PushCombined(img.m_imageView, sampler);
		desc.bindlessID = img.m_bindlessID;

		const uint32_t tableIdx = PushAssetTexture(std::move(img));
		ownedSlots.push_back(tableIdx);
		validSlots.push_back({ i, tableIdx });
	}

	if (!validSlots.empty())
	{
		std::vector<TextureUploadDesc> uploads;
		uploads.reserve(validSlots.size());

		for (const auto& slot : validSlots)
		{
			TextureDesc& desc = batch.textures[slot.assetIndex];
			AllocatedImage& img = m_assetTextures[slot.tableIndex];

			uploads.emplace_back(TextureUploadDesc{
				.image = &img,
				.pixelData = desc.pixelData.data(),
				.byteSize = desc.pixelData.size(),
				.mips = desc.mips,
				.strategy = MipStrategy::Precomputed
				});
		}

		staging.ExecuteTextureBatch(cmd, uploads);
	}

	for (auto& desc : batch.textures)
	{
		desc.pixelData.clear();
		desc.pixelData.shrink_to_fit();
	}

	MarkDirty();
	return ownedSlots;
}

// =====================
// DESCRIPTOR ARRAYS
// =====================

uint32_t BindlessImageTable::PushCombinedLocked(VkImageView view, VkSampler sampler)
{
	ASSERT(view != VK_NULL_HANDLE && sampler != VK_NULL_HANDLE);
	if (view == VK_NULL_HANDLE || sampler == VK_NULL_HANDLE) return UINT32_MAX;
	auto key = ImageViewSamplerKey{ view, sampler };
	if (auto it = m_combinedViewHashToID.find(key); it != m_combinedViewHashToID.end())
		return it->second;

	const uint32_t index = static_cast<uint32_t>(m_combinedViews.size());
	m_combinedViews.emplace_back(VkDescriptorImageInfo{ sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
	m_combinedViewHashToID[key] = index;
	return index;
}

uint32_t BindlessImageTable::PushCombined(VkImageView view, VkSampler sampler)
{
	std::scoped_lock lock(m_combinedMutex);
	return PushCombinedLocked(view, sampler);
}

void BindlessImageTable::PushCombinedBatch(std::span<AllocatedImage> images, VkSampler sampler)
{
	std::scoped_lock lock(m_combinedMutex);
	for (AllocatedImage& img : images)
	{
		if (!img.IsValid()) continue;
		img.m_bindlessID = PushCombinedLocked(img.m_imageView, sampler);
	}
	MarkDirty();
}

uint32_t BindlessImageTable::PushSamplerCubeLocked(VkImageView view, VkSampler sampler)
{
	ASSERT(view != VK_NULL_HANDLE && sampler != VK_NULL_HANDLE);
	auto key = ImageViewSamplerKey{ view, sampler };
	if (auto it = m_samplerCubeViewHashToID.find(key); it != m_samplerCubeViewHashToID.end())
		return it->second;

	const uint32_t index = static_cast<uint32_t>(m_samplerCubeViews.size());
	m_samplerCubeViews.emplace_back(VkDescriptorImageInfo{ sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
	m_samplerCubeViewHashToID[key] = index;
	return index;
}

uint32_t BindlessImageTable::PushSamplerCube(VkImageView view, VkSampler sampler)
{
	std::scoped_lock lock(m_samplerCubeMutex);
	return PushSamplerCubeLocked(view, sampler);
}

void BindlessImageTable::UpdateCombinedLocked(uint32_t index, VkImageView view, VkSampler sampler)
{
	ASSERT(index < static_cast<uint32_t>(m_combinedViews.size()));
	ASSERT(view != VK_NULL_HANDLE && sampler != VK_NULL_HANDLE);

	// Drop the stale key currently mapped to this slot
	const VkDescriptorImageInfo& prev = m_combinedViews[index];
	if (prev.imageView != VK_NULL_HANDLE)
	{
		auto prevKey = ImageViewSamplerKey{ prev.imageView, prev.sampler };
		if (auto it = m_combinedViewHashToID.find(prevKey);
			it != m_combinedViewHashToID.end() && it->second == index)
			m_combinedViewHashToID.erase(it);
	}

	m_combinedViews[index] = VkDescriptorImageInfo{ sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	m_combinedViewHashToID[ImageViewSamplerKey{ view, sampler }] = index;
}

bool BindlessImageTable::IsRenderTargetCombinedRangeBuilt() const noexcept
{
	return m_bRenderTargetsRegistered;
}

VkSampler BindlessImageTable::ResolveRenderTargetSampler(RD::Renderer_RenderTarget slot) const
{
	switch (slot)
	{
	case RD::Renderer_RenderTarget::DirectionalCSMAtlas:
	case RD::Renderer_RenderTarget::FlashlightShadowMap:
	case RD::Renderer_RenderTarget::VolumetricShadowMap:
		return GetSampler(RD::Renderer_Sampler::ShadowMap);

	case RD::Renderer_RenderTarget::HiZ:
	case RD::Renderer_RenderTarget::LinearizedHiZ:
		return GetSampler(RD::Renderer_Sampler::HiZ);

	case RD::Renderer_RenderTarget::Visibility:
	case RD::Renderer_RenderTarget::ShadowInvalidMask:
	case RD::Renderer_RenderTarget::GBufferNormalMaterial:
		return GetSampler(RD::Renderer_Sampler::NearestClamp);

	case RD::Renderer_RenderTarget::BloomMipchain:
		return GetSampler(RD::Renderer_Sampler::LinearLodClamp);

	default:
		return GetSampler(RD::Renderer_Sampler::LinearClamp);
	}
}

void BindlessImageTable::RegisterRenderTargetsAsCombined()
{
	std::scoped_lock lock(m_combinedMutex);
	if (m_bRenderTargetsRegistered || !m_combinedViews.empty())
		throw std::logic_error("Register render targets once, before static/asset descriptors");

	// Validate before modifying the table. All 2D targets must be created first.
	for (size_t i = 0; i < RD::RENDER_TARGET_COUNT; ++i)
	{
		if (IS::kRenderTargets[i].bVolume) continue;
		const auto slot = static_cast<RD::Renderer_RenderTarget>(i);
		const auto& image = m_renderTargets[i];
		if (!image.IsValid() || image.m_imageView == VK_NULL_HANDLE ||
			ResolveRenderTargetSampler(slot) == VK_NULL_HANDLE)
			throw std::runtime_error("Render target or sampler missing before bindless registration");
	}

	m_renderTargetCombinedBegin = static_cast<uint32_t>(m_combinedViews.size());
	m_renderTargetCombinedIDs.fill(UINT32_MAX);
	m_combinedViews.reserve(m_combinedViews.size() + RD::RENDER_TARGET_COUNT);

	for (uint32_t i = 0; i < RD::RENDER_TARGET_COUNT; ++i)
	{
		auto& image = m_renderTargets[i];
		image.m_bindlessID = UINT32_MAX;
		if (IS::kRenderTargets[i].bVolume) continue;

		const auto slot = static_cast<RD::Renderer_RenderTarget>(i);
		const VkSampler sampler = ResolveRenderTargetSampler(slot);
		const uint32_t id = static_cast<uint32_t>(m_combinedViews.size());
		image.m_bindlessID = id;
		m_renderTargetCombinedIDs[i] = id;
		m_combinedViews.push_back({
			sampler, image.m_imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
		m_combinedViewHashToID[ImageViewSamplerKey{ image.m_imageView, sampler }] = id;
	}

	m_renderTargetCombinedEnd = static_cast<uint32_t>(m_combinedViews.size());
	m_bRenderTargetsRegistered = true;
	MarkDirty();
}

RD::RenderTargetIDs BindlessImageTable::GetRenderTargetIDs() const
{
	ASSERT(m_bRenderTargetsRegistered);
	RD::RenderTargetIDs ids{};
	ids.worldProbeVisibilityID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::WorldProbeVisibility);
	ids.worldProbeLightingID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::WorldProbeLighting);
	ids.worldProbeSkyMeanID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::WorldProbeSkyMean);
	ids.worldProbeReconstructedID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::WorldProbeReconstructed);
	ids.transparentAccumulationID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::TransparentAccumulation);
	ids.transparentRevealageID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::TransparentRevealage);
	ids.transparentVelocityAccumID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::TransparentVelocityAccum);
	ids.hdrSceneID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::HDRScene);
	ids.tonemapID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::Tonemap);
	ids.depthResolvedID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::DepthResolved);
	ids.prevDepthResolvedID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::PrevDepthResolved);
	ids.hiZID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::HiZ);
	ids.linearizedHiZID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::LinearizedHiZ);
	ids.visibilityID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::Visibility);
	ids.aoRawID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::AORaw);
	ids.aoTempID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::AOTemp);
	ids.aoHistoryAID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::AOHistoryA);
	ids.aoHistoryBID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::AOHistoryB);
	ids.aoReconstructionID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::AOReconstruction);
	ids.aoEdgeInfoID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::AoEdgeInfo);
	ids.bentAOUpsampledID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::BentAOUpsampled);
	ids.bentNormalAOID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::BentNormalAO);
	ids.bentNormalAOHalfID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::BentNormalAOHalf);
	ids.colorHistoryAID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::ColorHistoryA);
	ids.colorHistoryBID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::ColorHistoryB);
	ids.flareBrightID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::FlareBright);
	ids.lensFlareColorID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::LensFlareColor);
	ids.bloomMipchainID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::BloomMipchain);
	ids.velocityID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::Velocity);
	ids.viewNormalsID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::ViewNormals);
	ids.prevViewNormalsID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::PrevViewNormals);
	ids.shadowInvalidMaskID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::ShadowInvalidMask);
	ids.rtShadowPenumbraID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::RTShadowPenumbra);
	ids.rtShadowDenoisedID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::RTShadowDenoised);
	ids.nrdShadowNormalRoughnessID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::NRDShadowNormalRoughness);
	ids.nrdShadowViewZID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::NRDShadowViewZ);
	ids.diffuseRadianceAID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::DiffuseRadianceA);
	ids.diffuseRadianceBID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::DiffuseRadianceB);
	ids.giHistoryAID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::GIHistoryA);
	ids.giHistoryBID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::GIHistoryB);
	ids.indirectSSGIID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::IndirectSSGI);
	ids.giDenoisePingID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::GIDenoisePing);
	ids.reflectRadianceID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::ReflectRadiance);
	ids.reflectRoughnessID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::ReflectRoughness);
	ids.atmosphereTransmittanceID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::AtmosphereTransmittance);
	ids.atmosphereSkyViewID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::AtmosphereSkyView);
	ids.atmosphereHDRID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::AtmosphereHDR);
	ids.atmosphereLightingID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::AtmosphereLighting);
	ids.nrdMotionID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::NRDMotion);
	ids.nrdNormalRoughnessID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::NRDNormalRoughness);
	ids.nrdViewZID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::NRDViewZ);
	ids.rtReflectDenoisedID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::RTReflectDenoised);
	ids.gBufferAlbedoRoughID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::GBufferAlbedoRough);
	ids.gBufferNormalMaterialID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::GBufferNormalMaterial);
	ids.postNonAACompositeID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::PostNonAAComposite);
	ids.sharpenedColorID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::SharpenedColor);
	ids.shadingSignalHalfID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::ShadingSignalHalf);
	ids.shadingLowAID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::ShadingLowA);
	ids.shadingLowBID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::ShadingLowB);
	ids.ssContactShadowsID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::SSContactShadows);
	ids.directionalCSMAtlasID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::DirectionalCSMAtlas);
	ids.flashlightShadowMapID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::FlashlightShadowMap);
	ids.volumetricShadowMapID = GetRenderTargetCombinedID(RD::Renderer_RenderTarget::VolumetricShadowMap);
	return ids;
}

void BindlessImageTable::RegisterStaticTexturesAsCombined(VkSampler genericSampler)
{
	std::scoped_lock lock(m_combinedMutex);
	for (AllocatedImage& img : m_staticTextures)
	{
		if (img.IsValid())
		{
			img.m_bindlessID = PushCombinedLocked(img.m_imageView, genericSampler);
		}
	}
	m_staticTextureCombinedEnd = static_cast<uint32_t>(m_combinedViews.size());
}

void BindlessImageTable::RegisterEnvironmentSetAsCube(
	uint32_t  envSetIndex,
	VkSampler skyboxSampler,
	VkSampler specularSampler,
	VkSampler irradianceSampler)
{
	ASSERT(envSetIndex < static_cast<uint32_t>(m_environmentSets.size()));
	EnvironmentSet& env = m_environmentSets[envSetIndex];

	std::scoped_lock lock(m_samplerCubeMutex);
	if (env.skybox.IsValid())
		env.skybox.m_bindlessID = PushSamplerCubeLocked(env.skybox.m_imageView, skyboxSampler);
	if (env.specular.IsValid())
		env.specular.m_bindlessID = PushSamplerCubeLocked(env.specular.m_imageView, specularSampler);
	MarkDirty();
}

// ============================
// Descriptor array image bake
// ============================

void BindlessImageTable::BuildInitialCombinedSamplerArray()
{
	// Stable compact 2D target range; integer targets are retained.
	// Only bVolume targets (the three froxel images) are omitted.
	RegisterRenderTargetsAsCombined();

	// Static textures and all dynamic asset textures follow the fixed RT range.
	auto pushStatic = [&](RD::Renderer_Texture slot, RD::Renderer_Sampler sampler)
		{
			AllocatedImage& img = m_staticTextures[Index(slot)];
			img.m_bindlessID = PushCombinedLocked(img.m_imageView, GetSampler(sampler));
		};

	std::scoped_lock lock(m_combinedMutex);

	pushStatic(RD::Renderer_Texture::White, RD::Renderer_Sampler::LinearClamp);
	pushStatic(RD::Renderer_Texture::Normal, RD::Renderer_Sampler::LinearClamp);
	pushStatic(RD::Renderer_Texture::MetalRough, RD::Renderer_Sampler::LinearClamp);
	pushStatic(RD::Renderer_Texture::Checkerboard, RD::Renderer_Sampler::LinearClamp);
	pushStatic(RD::Renderer_Texture::RainbowLut, RD::Renderer_Sampler::LinearClamp);
	pushStatic(RD::Renderer_Texture::HilbertCurveLut, RD::Renderer_Sampler::Noise);
	pushStatic(RD::Renderer_Texture::ShadowSTBN, RD::Renderer_Sampler::Noise);
	pushStatic(RD::Renderer_Texture::CookieGobo, RD::Renderer_Sampler::LinearClamp);
	pushStatic(RD::Renderer_Texture::Brdf, RD::Renderer_Sampler::Brdf);

	m_staticTextureCombinedEnd = static_cast<uint32_t>(m_combinedViews.size());

	MarkDirty();
}

void BindlessImageTable::BuildInitialSamplerCubeArray()
{
	std::scoped_lock lock(m_samplerCubeMutex);

	for (auto& env : m_environmentSets)
	{
		if (!env.IsValid()) continue;

		env.specular.m_bindlessID = PushSamplerCubeLocked(env.specular.m_imageView, GetSampler(RD::Renderer_Sampler::Specular));
		env.skybox.m_bindlessID = PushSamplerCubeLocked(env.skybox.m_imageView, GetSampler(RD::Renderer_Sampler::Skybox));
	}

	MarkDirty();
}
