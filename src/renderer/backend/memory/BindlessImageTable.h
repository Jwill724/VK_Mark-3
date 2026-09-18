#pragma once

#include <renderer/backend/VulkanForward.h>
#include "AllocatedImage.h"
#include <renderer/RendererDefinitions.h>
#include "../../../core/asset/AssetUploadTypes.h"
#include "ImageSpecs.h"
#include <span>

namespace ImageSpecs
{
	enum class ImageGroup : uint8_t;
}

#define COLOR_RESOLVED_A RD::Renderer_RenderTarget::ColorHistoryA
#define COLOR_RESOLVED_B RD::Renderer_RenderTarget::ColorHistoryB

#define RADIANCE_RESOLVED_A RD::Renderer_RenderTarget::DiffuseRadianceA
#define RADIANCE_RESOLVED_B RD::Renderer_RenderTarget::DiffuseRadianceB

#define GI_RESOLVED_A RD::Renderer_RenderTarget::GIHistoryA
#define GI_RESOLVED_B RD::Renderer_RenderTarget::GIHistoryB

#define FROXEL_SCATTER_RESOLVED_A RD::Renderer_RenderTarget::FroxelScatterExtA
#define FROXEL_SCATTER_RESOLVED_B RD::Renderer_RenderTarget::FroxelScatterExtB

#define SHADING_LOW_RESOLVED_A RD::Renderer_RenderTarget::ShadingLowA
#define SHADING_LOW_RESOLVED_B RD::Renderer_RenderTarget::ShadingLowB

#define AO_RESOLVED_A RD::Renderer_RenderTarget::AOHistoryA
#define AO_RESOLVED_B RD::Renderer_RenderTarget::AOHistoryB

class Allocator;
class StagingBuffer;
struct Extents3D;

namespace RD = RendererDefinitions;

class BindlessImageTable final
{
public:
	void Init(
		Extents3D drawExtent,
		RD::ShadowQuality shadowQuality,
		VkDevice device,
		Allocator& allocator);
	void Shutdown(VkDevice device, Allocator& allocator);

	void PreallocateEquirects(std::span<const char* const> hdrPaths, Allocator& allocator);
	void UploadStaticTextures(StagingBuffer& staging, VkCommandBuffer cmd);
	void UploadEquirects(
		std::span<const char* const> hdrPaths,
		Allocator& allocator,
		VkCommandBuffer              cmd);
	void FreeEquirects(Allocator& allocator);

	void UpdateRenderTargets(Extents3D drawExtent, Allocator& allocator);

	// --- Render targets ---
	const AllocatedImage& GetRenderTarget(RD::Renderer_RenderTarget slot) const;
	std::array<AllocatedImage, RD::RENDER_TARGET_COUNT>& GetRenderTargetsMutable() { return m_renderTargets; }
	const std::array<AllocatedImage, RD::RENDER_TARGET_COUNT>& GetRenderTargets()  const { return m_renderTargets; }
	void TransitionRenderTargetsFromUndefined(VkCommandBuffer cmd);

	// Quick resize for resolution swap
	void UpdateCSMAtlasExtent(RD::ShadowQuality quality, Allocator& allocator);

	// When rt shadows enable free up vram
	// Cached info
	void FreeCSMAtlas(Allocator& allocator);
	void RecreateCSMAtlas(Allocator& allocator);

	// --- Samplers ---
	VkSampler GetSampler(RD::Renderer_Sampler slot) const;
	const std::array<VkSampler, RD::SAMPLER_COUNT>& GetSamplers() const { return m_samplers; }

	// --- Static textures ---
	const AllocatedImage& GetStaticTexture(RD::Renderer_Texture slot) const;
	const std::array<AllocatedImage, RD::STATIC_TEXTURE_COUNT>& GetStaticTextures() const { return m_staticTextures; }

	size_t CalcStaticTexturesStagingSize() const;

	// --- Environment sets ---
	const EnvironmentSet& GetEnvironmentSet(uint32_t index)        const;
	EnvironmentSet& GetEnvironmentSetMutable(uint32_t index);
	uint32_t               EnvironmentSetCount()                    const noexcept;

	// --- Asset textures ---
	uint32_t              PushAssetTexture(AllocatedImage image);
	const AllocatedImage& GetAssetTexture(uint32_t index)           const;
	AllocatedImage& GetAssetTextureMutable(uint32_t index);
	void                  FreeAssetTexture(uint32_t index);
	uint32_t              AssetTextureCount()                 const noexcept { return static_cast<uint32_t>(m_assetTextures.size()); }
	bool                  IsAssetTextureValid(uint32_t index) const noexcept;

	uint32_t ResolveAssetSampler(const SamplerDesc& desc, VkDevice device);

	VkSampler ResolveDefaultAssetSampler(const AllocatedImage& img) const noexcept;

	std::vector<uint32_t> UploadAssetTextures(
		SceneUploadBatch& batch,
		VkDevice          device,
		Allocator& allocator,
		StagingBuffer& staging,
		VkCommandBuffer   cmd);

	std::span<const AllocatedImage> GetAssetTextureSpan()   const noexcept { return m_assetTextures; }
	std::span<const EnvironmentSet> GetEnvironmentSetSpan() const noexcept { return m_environmentSets; }
	std::span<const AllocatedImage> GetStaticTextureSpan()  const noexcept { return m_staticTextures; }
	std::span<const AllocatedImage> GetRenderTargetSpan()   const noexcept { return m_renderTargets; }

	// --- Descriptor arrays ---
	uint32_t PushCombined(VkImageView view, VkSampler sampler);
	void     PushCombinedBatch(std::span<AllocatedImage> images, VkSampler sampler);
	uint32_t PushSamplerCube(VkImageView view, VkSampler sampler);

	// All non-volume render targets own stable compact descriptor slots.
	// Descriptor indices are NOT Renderer_RenderTarget enum values.
	void RegisterRenderTargetsAsCombined();
	void RegisterStaticTexturesAsCombined(VkSampler genericSampler);
	void RegisterEnvironmentSetAsCube(uint32_t envSetIndex, VkSampler skyboxSampler,
		VkSampler specularSampler, VkSampler irradianceSampler);

	const std::vector<VkDescriptorImageInfo>& GetCombinedSamplerArray() const noexcept { return m_combinedViews; }
	const std::vector<VkDescriptorImageInfo>& GetSamplerCubeArray()     const noexcept { return m_samplerCubeViews; }
	uint32_t CombinedSamplerCount() const noexcept { return static_cast<uint32_t>(m_combinedViews.size()); }
	uint32_t SamplerCubeCount()     const noexcept { return static_cast<uint32_t>(m_samplerCubeViews.size()); }
	uint32_t RenderTargetCombinedBegin() const noexcept { return m_renderTargetCombinedBegin; }
	uint32_t RenderTargetCombinedEnd()   const noexcept { return m_renderTargetCombinedEnd; }
	RD::RenderTargetIDs GetRenderTargetIDs() const;
	uint32_t GetRenderTargetCombinedID(RD::Renderer_RenderTarget slot) const noexcept
	{
		ASSERT(m_bRenderTargetsRegistered);
		ASSERT(static_cast<size_t>(slot) < RD::RENDER_TARGET_COUNT);
		return m_renderTargetCombinedIDs[static_cast<size_t>(slot)];
	}

	void ClearDescriptorArrays()
	{
		std::scoped_lock l(m_combinedMutex, m_samplerCubeMutex);

		m_combinedViews.clear();
		m_combinedViewHashToID.clear();
		m_samplerCubeViews.clear();
		m_samplerCubeViewHashToID.clear();

		m_renderTargetCombinedBegin = 0u;
		m_renderTargetCombinedEnd = 0u;
		m_staticTextureCombinedEnd = 0u;
		m_renderTargetCombinedIDs.fill(UINT32_MAX);
		m_bRenderTargetsRegistered = false;
	}

	void BuildInitialCombinedSamplerArray();
	void BuildInitialSamplerCubeArray();

	void MarkDirty() noexcept { m_bIsTableDirty = true; ++m_cpuVersion; }
	bool IsTableDirty() const noexcept { return m_bIsTableDirty; }
	void ClearDirty() noexcept { m_bIsTableDirty = false; }

	bool IsShadowAtlasCached() const noexcept { return m_cachedCsmAtlasInfo.isActive; }
	uint32_t GetCachedCSMRes() const noexcept { return m_csmAtlasRes; }

private:
	void CreateRenderTargetGroup(ImageSpecs::ImageGroup group, Allocator& allocator);
	void FreeRenderTargetGroup(ImageSpecs::ImageGroup group, Allocator& allocator);

	void CreateRenderTargets(Extents3D drawExtent, Allocator& allocator);
	void CreateStaticTextures(Allocator& allocator);
	void CreateEnvironmentSets(uint32_t setCount, Allocator& allocator);
	void CreateShadowMaps(RD::ShadowQuality quality, Allocator& allocator);
	void CreateFroxelFogTargets(Allocator& allocator);
	void CreateSamplers(VkDevice device);
	void CreateAtmosphereTargets(Allocator& allocator);

	void FreeAtmosphereTargets(Allocator& allocator);
	void FreeRenderTargets(Allocator& allocator);
	void FreeShadowMaps(Allocator& allocator);
	void FreeFroxelFogTargets(Allocator& allocator);
	void FreeStaticTextures(Allocator& allocator);
	void FreeEnvironmentSets(Allocator& allocator);
	void FreeSamplers(VkDevice device);

	void SetRenderTarget(RD::Renderer_RenderTarget slot, AllocatedImage image);
	void SetStaticTexture(RD::Renderer_Texture slot, AllocatedImage image);
	void SetSampler(RD::Renderer_Sampler slot, VkSampler sampler);
	void AddEnvironmentSet(EnvironmentSet envSet);

	uint32_t PushCombinedLocked(VkImageView view, VkSampler sampler);
	uint32_t PushSamplerCubeLocked(VkImageView view, VkSampler sampler);
	void UpdateCombinedLocked(uint32_t index, VkImageView view, VkSampler sampler);
	VkSampler ResolveRenderTargetSampler(RD::Renderer_RenderTarget slot) const;
	bool IsRenderTargetCombinedRangeBuilt() const noexcept;

	std::array<AllocatedImage, RD::RENDER_TARGET_COUNT>                         m_renderTargets{};
	std::array<AllocatedImage, RD::STATIC_TEXTURE_COUNT>                        m_staticTextures{};
	std::array<EnvironmentSet, static_cast<size_t>(RD::MAX_ENVIRONMENT_SETS)>   m_environmentSets{};
	std::array<VkSampler, RD::SAMPLER_COUNT>                               m_samplers{};
	std::vector<AllocatedImage>                                                 m_assetTextures{};
	std::vector<VkSampler>                                                      m_assetSamplers{};
	std::vector<SamplerDesc>                                                    m_assetSamplerDescs{};
	std::unordered_map<std::string, AssetTextureEntry>                          m_assetTextureCache{};
	std::mutex                                                                  m_assetTextureCacheMutex{};

	bool     m_bAreAtmosphereTargetsCreated = false;
	bool     m_bAreShadowsCreated = false;
	bool     m_bAreFroxelFogCreated = false;
	bool     m_bIsTableDirty = false;
	uint32_t m_cpuVersion = 1u;
	uint32_t m_gpuVersion = 0u;

	struct CachedCSMAtlasInfo
	{
		uint32_t csmAtlasBindlessID = UINT32_MAX;
		bool isActive = false;
	} m_cachedCsmAtlasInfo{};

	Extents3D m_drawExtent{};
	uint32_t  m_csmAtlasRes = 0u;

	using ImageViewSamplerKey = std::pair<VkImageView, VkSampler>;

	struct HashPair
	{
		size_t operator()(const ImageViewSamplerKey& k) const noexcept
		{
			return std::hash<uintptr_t>()(reinterpret_cast<uintptr_t>(k.first))
				^ (std::hash<uintptr_t>()(reinterpret_cast<uintptr_t>(k.second)) << 1);
		}
	};
	struct EqualPair
	{
		bool operator()(const ImageViewSamplerKey& a, const ImageViewSamplerKey& b) const noexcept
		{
			return a.first == b.first && a.second == b.second;
		}
	};

	std::mutex m_combinedMutex;
	std::mutex m_samplerCubeMutex;

	std::vector<VkDescriptorImageInfo>                                         m_combinedViews;
	std::unordered_map<ImageViewSamplerKey, uint32_t, HashPair, EqualPair>     m_combinedViewHashToID;

	std::vector<VkDescriptorImageInfo>                                         m_samplerCubeViews;
	std::unordered_map<ImageViewSamplerKey, uint32_t, HashPair, EqualPair>     m_samplerCubeViewHashToID;

	uint32_t m_renderTargetCombinedBegin = 0u;
	uint32_t m_renderTargetCombinedEnd = 0u;
	uint32_t m_staticTextureCombinedEnd = 0u;
	std::array<uint32_t, RD::RENDER_TARGET_COUNT> m_renderTargetCombinedIDs = []
		{
			std::array<uint32_t, RD::RENDER_TARGET_COUNT> ids{};
			ids.fill(UINT32_MAX);
			return ids;
		}();
	bool m_bRenderTargetsRegistered = false;
};

namespace TemporalHistory
{
	struct Slots
	{
		RD::Renderer_RenderTarget read;   // previous frame's output (history to accumulate against)
		RD::Renderer_RenderTarget write;  // this frame's output (also what later passes sample)
	};

	inline Slots GetColorHistorySlots(uint64_t frameIndex)
	{
		const bool odd = (frameIndex & 1ull) != 0ull;
		return odd
			? Slots{ RD::Renderer_RenderTarget::ColorHistoryB,
					 RD::Renderer_RenderTarget::ColorHistoryA }
			: Slots{ RD::Renderer_RenderTarget::ColorHistoryA,
					 RD::Renderer_RenderTarget::ColorHistoryB };
	}

	inline Slots GetDiffuseRadianceSlots(uint64_t frameIndex)
	{
		const bool odd = (frameIndex & 1ull) != 0ull;
		return odd
			? Slots{ RD::Renderer_RenderTarget::DiffuseRadianceB,
					 RD::Renderer_RenderTarget::DiffuseRadianceA }
			: Slots{ RD::Renderer_RenderTarget::DiffuseRadianceA,
					 RD::Renderer_RenderTarget::DiffuseRadianceB };
	}

	inline Slots GetGIHistorySlots(uint64_t frameIndex)
	{
		const bool odd = (frameIndex & 1ull) != 0ull;
		return odd
			? Slots{ RD::Renderer_RenderTarget::GIHistoryB,
					 RD::Renderer_RenderTarget::GIHistoryA }
			: Slots{ RD::Renderer_RenderTarget::GIHistoryA,
					 RD::Renderer_RenderTarget::GIHistoryB };
	}

	inline Slots GetFroxelScatterSlots(uint64_t frameIndex)
	{
		const bool odd = (frameIndex & 1ull) != 0ull;
		return odd
			? Slots{ RD::Renderer_RenderTarget::FroxelScatterExtB,
					 RD::Renderer_RenderTarget::FroxelScatterExtA }
			: Slots{ RD::Renderer_RenderTarget::FroxelScatterExtA,
					 RD::Renderer_RenderTarget::FroxelScatterExtB };
	}

	inline Slots GetShadingLowSlots(uint64_t frameIndex)
	{
		const bool odd = (frameIndex & 1ull) != 0ull;
		return odd
			? Slots{ RD::Renderer_RenderTarget::ShadingLowB,
					 RD::Renderer_RenderTarget::ShadingLowA }
			: Slots{ RD::Renderer_RenderTarget::ShadingLowA,
					 RD::Renderer_RenderTarget::ShadingLowB };
	}

	inline Slots GetAOHistorySlots(uint64_t frameIndex)
	{
		const bool odd = (frameIndex & 1ull) != 0ull;
		return odd
			? Slots{ RD::Renderer_RenderTarget::AOHistoryB,
					 RD::Renderer_RenderTarget::AOHistoryA }
			: Slots{ RD::Renderer_RenderTarget::AOHistoryA,
					 RD::Renderer_RenderTarget::AOHistoryB };
	}
}