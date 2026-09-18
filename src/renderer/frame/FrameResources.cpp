#include "pch.h"

#include "FrameResources.h"
#include "../backend/memory/AllocatedBuffer.h"
#include "../backend/memory/Budgets.h"
#include "../Renderer.h"
#include "core/JobSystem.h"
#include "../../renderer/backend/Device.h"
#include "../../renderer/backend/descriptors/DescriptorManager.h"
#include "../scene/World.h"
#include "../scene/Scene.h"

inline constexpr glm::vec2 FroxelClips{ 0.5f, 120.0f };

void Renderer::InitFrameResources(JobSystem& jobSystem)
{
	m_framesInFlight = m_swapchain.GetImageCount();
	fmt::println("Frames in flight:[{}]", m_framesInFlight);

	m_rtRayListLayout.Update(m_renderExtent.Width(), m_renderExtent.Height());
	m_clusterBufferSizes.UpdateClusterBufferSizes(m_renderExtent.Width(), m_renderExtent.Height());

	const uint32_t threadCount = jobSystem.GetThreadCount();
	const auto logicalDevice = m_device->GetContext().device;

	// Allocate from the shared descriptor pool serially before starting frames.
	std::vector<VkDescriptorSet> frameSets(m_framesInFlight);
	for (uint32_t i = 0; i < m_framesInFlight; ++i)
	{
		frameSets[i] = m_descriptorManager->AllocateFrameDescriptorSet(logicalDevice);
	}

	// RunParallel waits for these iterations before local captures go out of scope.
	jobSystem.RunParallel(m_framesInFlight,
		[this, threadCount, &frameSets](ThreadContext&, uint32_t i)
		{
			auto& frame = m_frameContexts[i];
			frame.Init(
				i,
				threadCount,
				*m_device,
				*m_descriptorManager,
				m_allocator,
				frameSets[i]);

			frame.m_cachedDrawExtent = m_renderExtent;
			frame.CreateClusterBuffers(m_clusterBufferSizes, m_allocator);
			frame.CreateRTRayListBuffer(m_rtRayListLayout, m_allocator);
		});
}

void Renderer::CleanupFrameResources()
{
	for (uint32_t i = 0; i < m_framesInFlight; ++i)
	{
		m_frameContexts[i].Cleanup(
			m_device->GetContext(),
			m_allocator);
	}
}


void Renderer::InitRenderSettings(
	bool enableLensFlare,
	bool enableChromaticAberration,
	bool enableBloom,
	bool enableShadows,
	bool enableSSS,
	bool enableVolumetrics,
	bool enableRTReflections,
	RD::AntiAliasingMethod aaMode,
	RD::GIMethod giMode,
	RD::ShadowQuality shadowQuality,
	RD::SunShadowFilter sunShadowFilter,
	bool enableProfilerView,
	bool enableSettings)
{
	RD::RenderToggles& toggles = m_profiler.debugToggles;

	toggles.enableBloom = enableBloom ? 1u : 0u;
	toggles.enableLensFlare = enableLensFlare ? 1u : 0u;
	toggles.enableChromaticAberration = enableChromaticAberration ? 1u : 0u;
	toggles.enableShadows = enableShadows ? 1u : 0u;
	toggles.enableSSS = enableSSS ? 1u : 0u;
	toggles.enableVolumetrics = enableVolumetrics ? 1u : 0u;
	toggles.enableRTReflections = enableRTReflections ? 1u : 0u;

	toggles.bloomIntensity = 0.02f;

	toggles.aaMode = static_cast<uint32_t>(aaMode);
	toggles.giMode = static_cast<uint32_t>(giMode);

	toggles.enableProfilerView = enableProfilerView ? 1u : 0u;
	toggles.enableSettings = enableSettings ? 1u : 0u;

	m_currentShadowQuality = shadowQuality;
	m_profiler.shadowQuality = shadowQuality;
	toggles.sunShadowFilter = static_cast<uint32_t>(sunShadowFilter);

	toggles.depthScale = 1.0f / World::GetScene().GetCamera().GetFarClip();
}

void Renderer::ApplyPushConstantDefaults()
{
	{
		auto& p = m_profiler.forwardPush;
		p.oitDepthScale = 400.0f;
		p.bounceFeedback = 0.6f;
		p.giIntensity = 1.0f;
	}

	{
		auto& p = m_profiler.ssgiSettings;

		// Trace
		p.effectRadius = 10.0f;
		p.effectFalloffRange = 0.6f;
		p.radiusMultiplier = 1.457f;
		p.sampleDistributionPower = 2.0f;

		// Spatial denoise
		p.denoiseBlurBeta = 0.7f;
		p.upsampleDepthSigma = 96.0f;

		// GI
		p.giClampMax = 2.0f;
		p.giReprojTolerance = 0.075f;
		p.giTemporalAlpha = 0.1f;
		p.giFallbackStrength = 0.4f;

		// AO temporal accumulation
		p.aoHistoryValid = 0u;
		p.aoHistoryWeight = 0.97f;
		p.aoDepthTolerance = 0.02f;
		p.aoNormalThreshold = 0.9f;
		p.aoMaxHistorySamples = 32.0f;
		p.aoInitialVariance = 0.0004f;

		// AO reconstruction confidence
		p.aoMinObservation = 0.05f;
		p.aoNeighborConfidence = 0.25f;
		p.aoClipConfidence = 0.50f;
		p.aoHistoryFootprintMinSupport = 0.25f;

		// AO history clipping and response
		p.aoClipSigma = 2.0f;
		p.aoClipMaxSigma = 0.08f;
		p.aoClipMargin = 0.015f;
		p.aoReactiveThreshold = 0.04f;

		// AO missing observations
		p.aoMissingHoldFrames = 4.0f;
		p.aoMissingMaxFrames = 12.0f;
		p.aoMissingAgeDecay = 0.90f;
	}

	{
		auto& p = m_profiler.taaSettings;
		p.clampGamma = 6.0f;
		p.depthRejectScale = 5.0f;
		p.motionSpeedScale = 0.0003f;
		p.sigmaFloor = 0.02f;
		p.shadingResponse = 2.0f;
		p.shadingRejectScale = 1.5f;
	}

	{
		auto& p = m_profiler.casSettings;
		p.sharpness = 0.5f;
		p.denoise = 1.0f;
		p.hdrCompress = 0.0f;
	}

	{
		auto& p = m_profiler.froxelSettings;
		p.froxelClips = FroxelClips;
		p.density = 0.0002f;
		p.scatteringStrength = 5.0f;
		p.extinction = 0.08f;
		p.heightFalloff = 0.06f;
		p.asymmetryFactor = 0.5f;
		p.jitterStrength = 0.9f;
		p.historyWeight = 0.92f;
		p.localLightIntensity = 1.0f;
	}

	{
		auto& p = m_profiler.compositePush;
		p.froxelClips = FroxelClips;
		p.fogEnabled = 0u;
	}

	{
		auto& p = m_profiler.lensFlareSettings;
		p.froxelClips = FroxelClips;
		p.fogEnabled = 0u;

		p.sunUv = glm::vec2(0.5f);
		p.sunVisible = 1.0f;

		p.brightIntensity = 1.0f;
		p.starburstIntensity = 1.3f;

		p.ringInnerRadius = 0.18f;
		p.ringOuterRadius = 0.21f;
		p.chromaStrength = 0.9f;
		p.haloAnisotropy = 0.4f;

		p.streakStrength = 0.07f;
		p.streakWidth = 0.01f;
		p.streakLength = 0.055f;
		p.starburstRotation = -2.2f;

		p.occlusionRadiusPixels = 5.0f;
		p.occlusionDepthBias = 0.0f;
		p.occlusionFade = 200.0f;
		p.sunJitterScale = 0.0f;

		p.starburstBlades = 6.0f;
		p.starburstLength = 0.05f;
		p.starburstWidth = 0.1f;
		p.haloOpacity = 0.01f;

		p.haloSqueeze = 1.4f;
		p.haloAngleGain = 2.8f;
		p.ghostStrength = 0.005f;
		p.ghostSpacing = 1.0f;
	}

	{
		auto& p = m_profiler.caPush;
		p.maxShiftPixels = 3.0f;
		p.distortionAmount = 0.02f;
		p.falloffExponent = 3.0f;
		p.maxTaps = 10u;
	}

	{
		auto& p = m_profiler.contactShadowsSettings;
		p.surfaceThickness = 0.005f;
		p.bilinearThreshold = 0.1f;
		p.shadowContrast = 4.0f;
	}

	{
		auto& p = m_profiler.bloomPush;
		p.filterRadius = 1.0f;
		p.bloomThreshold = 6.0f;
		p.bloomKnee = 3.0f;
	}

	{
		auto& p = m_profiler.lumaExposureSettings;
		p.resetAdaptation = 1u;
		p.manualExposure = 0u;
		p.manualEV100 = 15.0f;
		p.exposureCompensation = 0.0f;
		p.adaptSpeedUp = 4.0f;
		p.adaptSpeedDown = 0.2f;
		p.minEV100 = 9.0f;
		p.maxEV100 = 20.0f;
	}

	{
		auto& p = m_profiler.rtShadowPush;
		p.shadow.rayTMin = 0.001f;
		p.shadow.rayTMax = 500.0f;
		p.shadow.rayBias = 1e-4f;
		p.shadow.normalBias = 0.03f;

		p.saturationEps = 0.02f;
		p.disocclusionScale = 0.05f;
	}

	{
		auto& p = m_profiler.reflectPush;
		p.shadow.rayTMin = 0.001f;
		p.shadow.rayTMax = 60.0f;
		p.shadow.rayBias = 1e-4f;
		p.shadow.normalBias = 0.06f;

		p.reflectRoughnessCutoff = 0.60f;
		p.roughnessFadeStart = 0.45f;
		p.ambientScale = 1.0f;
		p.bounceRoughnessCutoff = 0.35f;

		p.maxBounces = 3u;
		p.maxReflectLights = 50u;
		p.shadowSkipThreshold = 0.01f;
	}
}

void ClusterBufferSizes::UpdateClusterBufferSizes(
	uint32_t screenWidth,
	uint32_t screenHeight,
	uint32_t tileSizeX,
	uint32_t tileSizeY,
	uint32_t zSlices)
{
	tileCountX = (screenWidth + tileSizeX - 1u) / tileSizeX;
	tileCountY = (screenHeight + tileSizeY - 1u) / tileSizeY;

	tileCount = tileCountX * tileCountY;
	clusterCount = tileCount * zSlices;

	clusterCountsBytes =
		static_cast<size_t>(clusterCount) * sizeof(uint32_t);

	clusterOffsetsBytes =
		static_cast<size_t>(clusterCount) * sizeof(uint32_t);

	clusterCursorsBytes =
		static_cast<size_t>(clusterCount) * sizeof(uint32_t);

	clusterLightIDsBytes =
		static_cast<size_t>(clusterCount) *
		static_cast<size_t>(RD::MAX_LIGHTS_PER_CLUSTER) *
		sizeof(uint32_t);

	clusterTileSliceRangesBytes = static_cast<size_t>(tileCount) * sizeof(glm::uvec2);

	clusterScanScratchBytes = 4u;

	tileTransparentNearBytes = static_cast<size_t>(tileCount) * sizeof(uint32_t);

	clusterCountsBytes =
		AllocatedBuffer::AlignUp(clusterCountsBytes, MIN_SSBO_ALIGNMENT_BYTES);

	clusterOffsetsBytes =
		AllocatedBuffer::AlignUp(clusterOffsetsBytes, MIN_SSBO_ALIGNMENT_BYTES);

	clusterCursorsBytes =
		AllocatedBuffer::AlignUp(clusterCursorsBytes, MIN_SSBO_ALIGNMENT_BYTES);

	clusterLightIDsBytes =
		AllocatedBuffer::AlignUp(clusterLightIDsBytes, MIN_SSBO_ALIGNMENT_BYTES);

	clusterTileSliceRangesBytes =
		AllocatedBuffer::AlignUp(clusterTileSliceRangesBytes, MIN_SSBO_ALIGNMENT_BYTES);

	clusterScanScratchBytes =
		AllocatedBuffer::AlignUp(clusterScanScratchBytes, MIN_SSBO_ALIGNMENT_BYTES);

	tileTransparentNearBytes =
		AllocatedBuffer::AlignUp(tileTransparentNearBytes, MIN_SSBO_ALIGNMENT_BYTES);
}

void RTRayListLayout::Update(uint32_t screenWidth, uint32_t screenHeight)
{
	halfWidth = (screenWidth + 1u) / 2u;
	halfHeight = (screenHeight + 1u) / 2u;

	const uint32_t halfPixels = halfWidth * halfHeight;
	const uint32_t fullPixels = screenWidth * screenHeight;

	capacities[RD::RT_RAY_SLOT_REFLECT] = halfPixels;
	capacities[RD::RT_RAY_SLOT_TRANSPARENCY] = 0u;
	capacities[RD::RT_RAY_SLOT_SHADOW] = fullPixels;

	uint32_t cursor = 0u;
	for (uint32_t i = 0; i < RD::RT_RAY_SLOT_COUNT; ++i)
	{
		bases[i] = cursor;
		cursor += capacities[i];
	}

	totalBytes = HEADER_BYTES + cursor * sizeof(uint32_t);
}


//
//void Cmaa2BufferSizes::UpdateCmaa2BufferSizes(
//	const uint32_t extentWidth,
//	const uint32_t extentHeight)
//{
//	pixelCount = extentWidth * extentHeight;
//
//	quadCountX = (extentWidth + 1u) / 2u;
//	quadCountY = (extentHeight + 1u) / 2u;
//	quadCount  = quadCountX * quadCountY;
//
//	controlBytes = 64u;
//
//	shapeCandidatesBytes =
//		static_cast<size_t>(pixelCount) * sizeof(uint32_t);
//
//	deferredLocationsBytes =
//		static_cast<size_t>(quadCount) * sizeof(uint32_t);
//
//	deferredHeadsBytes =
//		static_cast<size_t>(quadCount) * sizeof(uint32_t);
//
//	deferredItemsCapacity = pixelCount * 2u;
//
//	deferredItemsBytes =
//		static_cast<size_t>(deferredItemsCapacity) *
//		sizeof(uint32_t) * 4u;
//
//	controlBytes = AllocatedBuffer::AlignUp(controlBytes, MIN_SSBO_ALIGNMENT_BYTES);
//
//	shapeCandidatesBytes =
//		AllocatedBuffer::AlignUp(shapeCandidatesBytes, MIN_SSBO_ALIGNMENT_BYTES);
//
//	deferredLocationsBytes =
//		AllocatedBuffer::AlignUp(deferredLocationsBytes, MIN_SSBO_ALIGNMENT_BYTES);
//
//	deferredHeadsBytes =
//		AllocatedBuffer::AlignUp(deferredHeadsBytes, MIN_SSBO_ALIGNMENT_BYTES);
//
//	deferredItemsBytes =
//		AllocatedBuffer::AlignUp(deferredItemsBytes, MIN_SSBO_ALIGNMENT_BYTES);
//}
