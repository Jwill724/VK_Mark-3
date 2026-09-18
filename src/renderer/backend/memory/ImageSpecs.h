#pragma once

#include "EngineTypes.h"
#include "../VulkanTypes.h"
#include "../../RendererDefinitions.h"

#include <array>

namespace RD = RendererDefinitions;

namespace ImageSpecs
{
	using RT = RD::Renderer_RenderTarget;
	using Tex = RD::Renderer_Texture;

	constexpr uint32_t U(RT s) { return static_cast<uint32_t>(s); }
	constexpr uint32_t U(Tex s) { return static_cast<uint32_t>(s); }

	enum class ImageScale : uint8_t
	{
		Full,
		Half,
		Quarter,
		Eighth,
		CSMAtlas,
		ProbeResolve,
		ProbeReconstruct,
		Fixed
	};

	enum class MipRule : uint8_t
	{
		Single,
		FullChain,
		ClampToExtent
	};

	// Drives creation/free/transition ownership. Resolution = owned by the swapchain
	// extent. ShadowMap/FroxelFog = created once, transitioned by the render graph.
	enum class ImageGroup : uint8_t
	{
		Resolution,
		ShadowMap,
		FroxelFog,
		Atmosphere,
		WorldProbes,
		Unused
	};

	struct ImageSpec
	{
		uint32_t          slot = UINT32_MAX;
		Vulkan_Format     format = {};
		Vulkan_ImageUsage usage = {};
		ImageScale        scale = ImageScale::Full;
		Extents3D         fixedExtent = {};
		MipRule           mipRule = MipRule::Single;
		uint32_t          mipCount = 1u;
		ImageGroup        group = ImageGroup::Resolution;
		bool              bCubemap = false;
		bool              bVolume = false;
		bool              bPerMipStorage = false;
		const char* debugName = nullptr;
	};

	struct ImageExtentContext
	{
		Extents3D drawExtent{};
		uint32_t  csmAtlasRes = 0u;
	};

	template<size_t Count, size_t N>
	constexpr std::array<ImageSpec, Count> Scatter(const ImageSpec(&src)[N])
	{
		std::array<ImageSpec, Count> out{};

		for (const ImageSpec& s : src)
			out[s.slot] = s;

		return out;
	}

	template<size_t N>
	constexpr bool IsComplete(const std::array<ImageSpec, N>& table)
	{
		for (const ImageSpec& s : table)
		{
			if (s.debugName == nullptr)
				return false;
		}

		return true;
	}

	// =====================
	// RENDER TARGET TABLE
	// =====================

	inline constexpr ImageSpec kRenderTargetEntries[] =
	{
		// --- World probes ---
		{
			.slot = U(RT::WorldProbeVisibility),
			.format = Vulkan_Format::RG8unorm,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.scale = ImageScale::Fixed,
			.fixedExtent = { 1280u, 800u, 1u },
			.group = ImageGroup::WorldProbes,
			.debugName = "WorldProbeVisibility"
		},
		{
			.slot = U(RT::WorldProbeLighting),
			.format = Vulkan_Format::RGBA16F,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.scale = ImageScale::ProbeResolve,
			.group = ImageGroup::Resolution,
			.debugName = "WorldProbeLighting"
		},


		{
			.slot = U(RT::WorldProbeSkyMean),
			.format = Vulkan_Format::RGBA32F,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.scale = ImageScale::Fixed,
			.fixedExtent = { { 128u, 80u, 1u } },
			.group = ImageGroup::WorldProbes,
			.debugName = "WorldProbeSkyMean"
		},
		{
			.slot = U(RT::WorldProbeReconstructed),
			.format = Vulkan_Format::RGBA16F,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.scale = ImageScale::ProbeReconstruct,
			.group = ImageGroup::Resolution,
			.debugName = "WorldProbeReconstructed"
		},
		{
			.slot = U(RT::WorldProbeSpatialCache),
			.format = Vulkan_Format::RGBA16F,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.scale = ImageScale::Fixed,
			.fixedExtent = { { 80u, 45u, 32u * 13u } },
			.group = ImageGroup::WorldProbes,
			.bVolume = true,
			.debugName = "WorldProbeSpatialCache"
		},

		// --- Full resolution ---
		{
			.slot = U(RT::HDRScene),
			.format = Vulkan_Format::RGBA16F,
			.usage = Vulkan_ImageUsage::DrawColor,
			.debugName = "HDRScene"
		},
		{
			.slot = U(RT::TransparentAccumulation),
			.format = Vulkan_Format::RGBA16F,
			.usage = Vulkan_ImageUsage::DrawColor,
			.debugName = "TransparentAccumulation"
		},
		{
			.slot = U(RT::TransparentVelocityAccum),
			.format = Vulkan_Format::RG16F,
			.usage = Vulkan_ImageUsage::DrawColor,
			.debugName = "TransparentVelocityAccum"
		},
		{
			.slot = U(RT::TransparentRevealage),
			.format = Vulkan_Format::R16F,
			.usage = Vulkan_ImageUsage::DrawColor,
			.debugName = "TransparentRevealage"
		},
		{
			.slot = U(RT::DepthResolved),
			.format = Vulkan_Format::D32,
			.usage = Vulkan_ImageUsage::DrawDepth,
			.debugName = "DepthResolved"
		},
		{
			.slot = U(RT::PrevDepthResolved),
			.format = Vulkan_Format::D32,
			.usage = Vulkan_ImageUsage::DrawDepth,
			.debugName = "PrevDepth"
		},
		{
			.slot = U(RT::Velocity),
			.format = Vulkan_Format::RG16F,
			.usage = Vulkan_ImageUsage::DrawColor,
			.debugName = "Velocity"
		},
		{
			.slot = U(RT::Visibility),
			.format = Vulkan_Format::RG32U,
			.usage = Vulkan_ImageUsage::DrawColor,
			.debugName = "Visibility"
		},
		{
			.slot = U(RT::ViewNormals),
			.format = Vulkan_Format::RG8unorm,
			.usage = Vulkan_ImageUsage::DrawColor,
			.debugName = "ViewNormals"
		},
		{
			.slot = U(RT::PrevViewNormals),
			.format = Vulkan_Format::RG8unorm,
			.usage = Vulkan_ImageUsage::ComputeRWTransfer,
			.debugName = "PrevViewNormals"
		},
		{
			.slot = U(RT::Tonemap),
			.format = Vulkan_Format::RGBA16F,
			.usage = Vulkan_ImageUsage::ComputeRWTransfer,
			.debugName = "ToneMap"
		},
		{
			.slot = U(RT::ColorHistoryA),
			.format = Vulkan_Format::RGBA16F,
			.usage = Vulkan_ImageUsage::ComputeRWTransfer,
			.debugName = "ColorHistoryA"
		},
		{
			.slot = U(RT::ColorHistoryB),
			.format = Vulkan_Format::RGBA16F,
			.usage = Vulkan_ImageUsage::ComputeRWTransfer,
			.debugName = "ColorHistoryB"
		},
		{
			.slot = U(RT::PostNonAAComposite),
			.format = Vulkan_Format::RGBA16F,
			.usage = Vulkan_ImageUsage::ComputeRWTransfer,
			.debugName = "PostNonAAComposite"
		},
		{
			.slot = U(RT::SharpenedColor),
			.format = Vulkan_Format::RGBA16F,
			.usage = Vulkan_ImageUsage::ComputeRWTransfer,
			.debugName = "SharpenedColor"
		},
		{
			.slot = U(RT::SSContactShadows),
			.format = Vulkan_Format::R8unorm,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.debugName = "ScreenSpaceShadowMask"
		},
		{
			.slot = U(RT::BentNormalAO),
			.format = Vulkan_Format::RGBA8unorm,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.debugName = "BentNormalAO"
		},
		{
			.slot = U(RT::GBufferAlbedoRough),
			.format = Vulkan_Format::RGBA8unorm,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.debugName = "GBufferAlbedoRough"
		},
		{
			.slot = U(RT::GBufferNormalMaterial),
			.format = Vulkan_Format::R32U,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.debugName = "GBufferNormalMaterial"
		},
		{
			.slot = U(RT::IndirectSSGI),
			.format = Vulkan_Format::RGBA16F,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.debugName = "IndirectSSGI"
		},
		{
			.slot = U(RT::NRDShadowNormalRoughness),
			.format = Vulkan_Format::ABGRpacked,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.debugName = "NRDShadowNormalRoughness"
		},
		{
			.slot = U(RT::NRDShadowViewZ),
			.format = Vulkan_Format::R32F,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.debugName = "NRDShadowViewZ"
		},
		{
			.slot = U(RT::RTShadowDenoised),
			.format = Vulkan_Format::R16F,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.debugName = "RTShadowDenoised"
		},
		{
			.slot = U(RT::RTShadowPenumbra),
			.format = Vulkan_Format::R32F,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.debugName = "RTShadowPenumbra"
		},

		// --- Hi-Z ---
		{
			.slot = U(RT::HiZ),
			.format = Vulkan_Format::R32U,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.mipRule = MipRule::ClampToExtent,
			.mipCount = RD::HI_Z_MIP_COUNT,
			.bPerMipStorage = true,
			.debugName = "HiZ"
		},
		{
			.slot = U(RT::LinearizedHiZ),
			.format = Vulkan_Format::R32F,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.mipRule = MipRule::ClampToExtent,
			.mipCount = RD::HI_Z_MIP_COUNT,
			.bPerMipStorage = true,
			.debugName = "LinearizedHiZ"
		},

		// --- Half resolution ---
		{
			.slot = U(RT::AoEdgeInfo),
			.format = Vulkan_Format::R8unorm,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.scale = ImageScale::Half,
			.debugName = "AOEdgeInfo"
		},
		{
			.slot = U(RT::AORaw),
			.format = Vulkan_Format::R8unorm,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.scale = ImageScale::Half,
			.debugName = "AORaw"
		},
		{
			.slot = U(RT::AOTemp),
			.format = Vulkan_Format::R8unorm,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.scale = ImageScale::Half,
			.debugName = "AOTemp"
		},
		{
			.slot = U(RT::BentAOUpsampled),
			.format = Vulkan_Format::RGBA8unorm,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.scale = ImageScale::Full,
			.debugName = "BentAOUpsampled"
		},
		{
			.slot = U(RT::AOReconstruction),
			.format = Vulkan_Format::RG16F,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.scale = ImageScale::Full,
			.debugName = "AOReconstruction"
		},
		{
			.slot = U(RT::AOHistoryA),
			.format = Vulkan_Format::RGBA16F,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.scale = ImageScale::Full,
			.debugName = "AOHistoryA"
		},
		{
			.slot = U(RT::AOHistoryB),
			.format = Vulkan_Format::RGBA16F,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.scale = ImageScale::Full,
			.debugName = "AOHistoryB"
		},
		{
			.slot = U(RT::BentNormalAOHalf),
			.format = Vulkan_Format::RGBA8unorm,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.scale = ImageScale::Half,
			.debugName = "BentNormalAOHalf"
		},
		{
			.slot = U(RT::GIHistoryA),
			.format = Vulkan_Format::RGBA16F,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.scale = ImageScale::Half,
			.debugName = "GIHistoryA"
		},
		{
			.slot = U(RT::GIHistoryB),
			.format = Vulkan_Format::RGBA16F,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.scale = ImageScale::Half,
			.debugName = "GIHistoryB"
		},
		{
			.slot = U(RT::GIDenoisePing),
			.format = Vulkan_Format::RGBA16F,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.scale = ImageScale::Half,
			.debugName = "GIDenoisePing"
		},
		{
			.slot = U(RT::ReflectRadiance),
			.format = Vulkan_Format::RGBA16F,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.scale = ImageScale::Half,
			.debugName = "ReflectRadiance"
		},
		{
			.slot = U(RT::ReflectRoughness),
			.format = Vulkan_Format::R16F,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.scale = ImageScale::Half,
			.debugName = "ReflectRoughness"
		},
		{
			.slot = U(RT::NRDMotion),
			.format = Vulkan_Format::RG16F,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.scale = ImageScale::Half,
			.debugName = "NRDMotion"
		},
		{
			.slot = U(RT::NRDNormalRoughness),
			.format = Vulkan_Format::ABGRpacked,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.scale = ImageScale::Half,
			.debugName = "NRDNormalRoughness"
		},
		{
			.slot = U(RT::NRDViewZ),
			.format = Vulkan_Format::R32F,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.scale = ImageScale::Half,
			.debugName = "NRDViewZ"
		},
		{
			.slot = U(RT::RTReflectDenoised),
			.format = Vulkan_Format::RGBA16F,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.scale = ImageScale::Half,
			.debugName = "ReflectDenoised"
		},
		{
			.slot = U(RT::ShadingSignalHalf),
			.format = Vulkan_Format::RG16F,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.scale = ImageScale::Half,
			.debugName = "ShadingSignalReduced"
		},
		{
			.slot = U(RT::BloomMipchain),
			.format = Vulkan_Format::BGRpacked,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.scale = ImageScale::Half,
			.mipRule = MipRule::FullChain,
			.bPerMipStorage = true,
			.debugName = "BloomMipchain"
		},
		{
			.slot = U(RT::DiffuseRadianceA),
			.format = Vulkan_Format::BGRpacked,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.scale = ImageScale::Half,
			.mipRule = MipRule::ClampToExtent,
			// .mipCount = RD::RADIANCE_MIP_COUNT,
			// .bPerMipStorage = true,
			.debugName = "DiffuseRadianceA"
		},
		{
			.slot = U(RT::DiffuseRadianceB),
			.format = Vulkan_Format::BGRpacked,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.scale = ImageScale::Half,
			.mipRule = MipRule::ClampToExtent,
			// .mipCount = RD::RADIANCE_MIP_COUNT,
			// .bPerMipStorage = true,
			.debugName = "DiffuseRadianceB"
		},

		// --- Quarter resolution ---
		{
			.slot = U(RT::FlareBright),
			.format = Vulkan_Format::RGBA16F,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.scale = ImageScale::Quarter,
			.debugName = "FlareBright"
		},
		{
			.slot = U(RT::LensFlareColor),
			.format = Vulkan_Format::RGBA16F,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.scale = ImageScale::Quarter,
			.debugName = "LensFlareColor"
		},

		// --- Eighth resolution ---
		{
			.slot = U(RT::ShadingLowA),
			.format = Vulkan_Format::RG16F,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.scale = ImageScale::Eighth,
			.debugName = "ShadingLowA"
		},
		{
			.slot = U(RT::ShadingLowB),
			.format = Vulkan_Format::RG16F,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.scale = ImageScale::Eighth,
			.debugName = "ShadingLowB"
		},
		{
			.slot = U(RT::ShadowInvalidMask),
			.format = Vulkan_Format::R8U,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.scale = ImageScale::Eighth,
			.debugName = "ShadowInvalidMask"
		},

		// --- Persistent: shadow maps ---
		{
			.slot = U(RT::DirectionalCSMAtlas),
			.format = Vulkan_Format::D32,
			.usage = Vulkan_ImageUsage::ShadowMap,
			.scale = ImageScale::CSMAtlas,
			.group = ImageGroup::ShadowMap,
			.debugName = "DirectionalCSMAtlas"
		},
		{
			.slot = U(RT::FlashlightShadowMap),
			.format = Vulkan_Format::D32,
			.usage = Vulkan_ImageUsage::ShadowMap,
			.scale = ImageScale::Fixed,
			.fixedExtent = { { RD::FLASHLIGHT_SHADOW_MAP_X, RD::FLASHLIGHT_SHADOW_MAP_Y, 1u } },
			.group = ImageGroup::ShadowMap,
			.debugName = "FlashlightShadowMap"
		},
		{
			.slot = U(RT::VolumetricShadowMap),
			.format = Vulkan_Format::D16,
			.usage = Vulkan_ImageUsage::ShadowMap,
			.scale = ImageScale::Fixed,
			.fixedExtent = { { RD::VOL_SHADOW_MAP_X, RD::VOL_SHADOW_MAP_Y, 1u } },
			.group = ImageGroup::ShadowMap,
			.debugName = "VolumetricShadowMap"
		},

		// --- Persistent: froxel fog ---
		{
			.slot = U(RT::FroxelScatterExtA),
			.format = Vulkan_Format::RGBA16F,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.scale = ImageScale::Fixed,
			.fixedExtent = { { RD::FROXEL_GRID_X, RD::FROXEL_GRID_Y, RD::FROXEL_GRID_Z } },
			.group = ImageGroup::FroxelFog,
			.bVolume = true,
			.debugName = "FroxelScatterA"
		},
		{
			.slot = U(RT::FroxelScatterExtB),
			.format = Vulkan_Format::RGBA16F,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.scale = ImageScale::Fixed,
			.fixedExtent = { { RD::FROXEL_GRID_X, RD::FROXEL_GRID_Y, RD::FROXEL_GRID_Z } },
			.group = ImageGroup::FroxelFog,
			.bVolume = true,
			.debugName = "FroxelScatterB"
		},
		{
			.slot = U(RT::FroxelIntegrated),
			.format = Vulkan_Format::RGBA16F,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.scale = ImageScale::Fixed,
			.fixedExtent = { { RD::FROXEL_GRID_X, RD::FROXEL_GRID_Y, RD::FROXEL_GRID_Z } },
			.group = ImageGroup::FroxelFog,
			.bVolume = true,
			.debugName = "FroxelIntegrated"
		},

		// --- Persistent: atmosphere ---
		{
			.slot = U(RT::AtmosphereTransmittance),
			.format = Vulkan_Format::RGBA16F,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.scale = ImageScale::Fixed,
			.fixedExtent = {
				{
					RD::ATMOSPHERE_TRANSMITTANCE_X,
					RD::ATMOSPHERE_TRANSMITTANCE_Y,
					1u
				}
			},
			.group = ImageGroup::Atmosphere,
			.debugName = "AtmosphereTransmittance"
		},
		{
			.slot = U(RT::AtmosphereSkyView),
			.format = Vulkan_Format::RGBA32F,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.scale = ImageScale::Fixed,
			.fixedExtent = {
				{
					RD::ATMOSPHERE_SKY_VIEW_X,
					RD::ATMOSPHERE_SKY_VIEW_Y,
					1u
				}
			},
			.group = ImageGroup::Atmosphere,
			.debugName = "AtmosphereSkyView"
		},
		{
			.slot = U(RT::AtmosphereLighting),
			.format = Vulkan_Format::RGBA32F,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.scale = ImageScale::Fixed,
			.fixedExtent = { { 128u, 448u, 1u } },
			.group = ImageGroup::Atmosphere,
			.debugName = "AtmosphereLighting"
		},

		// --- Full resolution: atmosphere ---
		{
			.slot = U(RT::AtmosphereHDR),
			.format = Vulkan_Format::RGBA16F,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.scale = ImageScale::Full,
			.group = ImageGroup::Resolution,
			.debugName = "AtmosphereHDR"
		},
	};

	inline constexpr auto kRenderTargets =
		Scatter<RD::RENDER_TARGET_COUNT>(kRenderTargetEntries);

	static_assert(
		std::size(kRenderTargetEntries) == RD::RENDER_TARGET_COUNT,
		"Render target spec table has a duplicate or missing slot");

	static_assert(
		IsComplete(kRenderTargets),
		"Every Renderer_RenderTarget needs a spec entry "
		"(use ImageGroup::Unused for reserved slots)");

	// =====================
	// STATIC TEXTURE TABLE
	// =====================

	inline constexpr ImageSpec kStaticTextureEntries[] =
	{
		{
			.slot = U(Tex::White),
			.format = Vulkan_Format::RGBA8unorm,
			.usage = Vulkan_ImageUsage::TextureSampled,
			.scale = ImageScale::Fixed,
			.fixedExtent = { { 1u, 1u, 1u } },
			.debugName = "DefaultWhite"
		},
		{
			.slot = U(Tex::Normal),
			.format = Vulkan_Format::RGBA8unorm,
			.usage = Vulkan_ImageUsage::TextureSampled,
			.scale = ImageScale::Fixed,
			.fixedExtent = { { 1u, 1u, 1u } },
			.debugName = "DefaultFlatNormal"
		},
		{
			.slot = U(Tex::MetalRough),
			.format = Vulkan_Format::RG8unorm,
			.usage = Vulkan_ImageUsage::TextureSampled,
			.scale = ImageScale::Fixed,
			.fixedExtent = { { 1u, 1u, 1u } },
			.debugName = "DefaultMetalRough"
		},
		{
			.slot = U(Tex::Dummy),
			.format = Vulkan_Format::BGRpacked,
			.usage = Vulkan_ImageUsage::TextureSampled,
			.scale = ImageScale::Fixed,
			.fixedExtent = { { 1u, 1u, 1u } },
			.debugName = "DummyBlack"
		},
		{
			.slot = U(Tex::DummyU8),
			.format = Vulkan_Format::R8U,
			.usage = Vulkan_ImageUsage::TextureSampled,
			.scale = ImageScale::Fixed,
			.fixedExtent = { { 1u, 1u, 1u } },
			.debugName = "DummyUint8"
		},
		{
			.slot = U(Tex::DummyVelocity),
			.format = Vulkan_Format::RG16F,
			.usage = Vulkan_ImageUsage::ComputeRWTransfer,
			.scale = ImageScale::Fixed,
			.fixedExtent = { { 1u, 1u, 1u } },
			.debugName = "DummyVelocity"
		},
		{
			.slot = U(Tex::Checkerboard),
			.format = Vulkan_Format::RGBA8unorm,
			.usage = Vulkan_ImageUsage::TextureSampled,
			.scale = ImageScale::Fixed,
			.fixedExtent = { { 16u, 16u, 1u } },
			.debugName = "ErrorCheckerboard"
		},
		{
			.slot = U(Tex::RainbowLut),
			.format = Vulkan_Format::RGBA8unorm,
			.usage = Vulkan_ImageUsage::TextureSampled,
			.scale = ImageScale::Fixed,
			.fixedExtent = { { 256u, 1u, 1u } },
			.debugName = "RainbowLUT"
		},
		{
			.slot = U(Tex::HilbertCurveLut),
			.format = Vulkan_Format::R16U,
			.usage = Vulkan_ImageUsage::TextureSampled,
			.scale = ImageScale::Fixed,
			.fixedExtent = { { 64u, 64u, 1u } },
			.debugName = "HilbertCurveLUT"
		},
		{
			.slot = U(Tex::ShadowSTBN),
			.format = Vulkan_Format::RG8unorm,
			.usage = Vulkan_ImageUsage::TextureSampled,
			.scale = ImageScale::Fixed,
			.fixedExtent = { { 1024u, 1024u, 1u } },
			.debugName = "ShadowSTBN"
		},
		{
			.slot = U(Tex::CookieGobo),
			.format = Vulkan_Format::R8unorm,
			.usage = Vulkan_ImageUsage::TextureSampled,
			.scale = ImageScale::Fixed,
			.fixedExtent = { { 512u, 512u, 1u } },
			.debugName = "CookieGobo"
		},
		{
			.slot = U(Tex::Brdf),
			.format = Vulkan_Format::RG16F,
			.usage = Vulkan_ImageUsage::ComputeOnly,
			.scale = ImageScale::Fixed,
			.fixedExtent = { { RD::BRDF_EXTENT, RD::BRDF_EXTENT, 1u } },
			.debugName = "BRDFLut"
		},
	};

	inline constexpr auto kStaticTextures =
		Scatter<RD::STATIC_TEXTURE_COUNT>(kStaticTextureEntries);

	static_assert(
		std::size(kStaticTextureEntries) == RD::STATIC_TEXTURE_COUNT,
		"Static texture spec table has a duplicate or missing slot");

	static_assert(
		IsComplete(kStaticTextures),
		"Every Renderer_Texture needs a spec entry "
		"(use ImageGroup::Unused for reserved slots)");

	// =====================
	// ENVIRONMENT SPECS
	// =====================

	inline constexpr ImageSpec kSkybox
	{
		.format = Vulkan_Format::RGBA16F,
		.usage = Vulkan_ImageUsage::ComputeRWTransfer,
		.scale = ImageScale::Fixed,
		.fixedExtent = { { RD::SKYBOX_EXTENT, RD::SKYBOX_EXTENT, 1u } },
		.mipRule = MipRule::FullChain,
		.bCubemap = true,
		.debugName = "EnvSkybox"
	};

	inline constexpr ImageSpec kSpecular
	{
		.format = Vulkan_Format::RGBA16F,
		.usage = Vulkan_ImageUsage::ComputeOnly,
		.scale = ImageScale::Fixed,
		.fixedExtent = { { RD::SPECULAR_EXTENT, RD::SPECULAR_EXTENT, 1u } },
		.mipRule = MipRule::ClampToExtent,
		.mipCount = RD::SPECULAR_PREFILTERED_MIP_LEVELS,
		.bCubemap = true,
		.bPerMipStorage = true,
		.debugName = "EnvSpecular"
	};

	// =====================
	// RESOLUTION
	// =====================

	inline const ImageSpec& RenderTarget(RT slot)
	{
		return kRenderTargets[U(slot)];
	}

	inline const ImageSpec& StaticTexture(Tex slot)
	{
		return kStaticTextures[U(slot)];
	}

	inline Extents3D ResolveExtent(
		const ImageSpec& spec,
		const ImageExtentContext& ctx)
	{
		auto scaled = [&](uint32_t d) -> Extents3D
			{
				return {
					{
						(ctx.drawExtent.Width() + d - 1u) / d,
						(ctx.drawExtent.Height() + d - 1u) / d,
						1u
					}
				};
			};

		switch (spec.scale)
		{
		case ImageScale::Full:
			return scaled(1u);

		case ImageScale::Half:
			return scaled(2u);

		case ImageScale::Quarter:
			return scaled(4u);

		case ImageScale::Eighth:
			return scaled(8u);

		case ImageScale::CSMAtlas:
			return {
				{
					ctx.csmAtlasRes,
					ctx.csmAtlasRes,
					1u
				}
			};

		case ImageScale::ProbeResolve:
		{
			constexpr uint32_t d = RD::WORLD_PROBE_RESOLVE_DIVISOR;
			static_assert(d > 0u);

			const uint32_t width =
				(ctx.drawExtent.Width() + d - 1u) / d;

			const uint32_t height =
				(ctx.drawExtent.Height() + d - 1u) / d;

			return {
				{
					width,
					height * 2u,
					1u
				}
			};
		}

		case ImageScale::ProbeReconstruct:
			return { { ctx.drawExtent.Width(), ctx.drawExtent.Height() * 2u, 1u } };

		case ImageScale::Fixed:
			return spec.fixedExtent;
		}

		return {};
	}

	inline uint32_t ResolveMipLevels(
		const ImageSpec& spec,
		Extents3D extent)
	{
		switch (spec.mipRule)
		{
		case MipRule::Single:
			return 1u;

		case MipRule::FullChain:
			return 0u;

		case MipRule::ClampToExtent:
		{
			uint32_t maxDim =
				std::max(extent.Width(), extent.Height());

			uint32_t mips = 1u;

			while (maxDim > 1u)
			{
				maxDim >>= 1;
				++mips;
			}

			return std::min(spec.mipCount, mips);
		}
		}

		return 1u;
	}

	inline ImageDesc MakeImageDesc(
		const ImageSpec& spec,
		const ImageExtentContext& ctx = {})
	{
		ImageDesc desc{};

		desc.format = spec.format;
		desc.extent = ResolveExtent(spec, ctx);
		desc.usage = spec.usage;
		desc.mipLevels = ResolveMipLevels(spec, desc.extent);
		desc.bIsCubemap = spec.bCubemap;
		desc.bPerMipStorage = spec.bPerMipStorage;
		desc.bIsConcurrent = spec.group == ImageGroup::WorldProbes
			|| spec.scale == ImageScale::ProbeResolve
			|| spec.scale == ImageScale::ProbeReconstruct;
		desc.imageType = spec.bVolume ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
		desc.debugName = spec.debugName;

		return desc;
	}
}