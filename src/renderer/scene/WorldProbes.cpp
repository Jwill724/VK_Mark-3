#include "pch.h"

#include "../Renderer.h"
#include "WorldProbeBudget.h"
#include "World.h"
#include "Scene.h"

void Renderer::RequestWorldProbeReset() noexcept
{
	++m_worldProbesState.epoch;
	++m_worldProbesState.resetRequested;
}

void Renderer::UpdateWorldProbes(bool geometryDirty, bool ssgiValid, float deltaSeconds)
{
	auto& s = m_profiler.worldProbeSettings;
	auto& state = m_worldProbesState;
	const auto& scene = World::GetScene().GetSceneData();
	const auto& sky = m_profiler.atmosphereSkySettings;
	const WorldProbeHeader old = m_worldProbesHeader;

	s.baseSpacing = std::clamp(s.baseSpacing, 0.1f, 100.0f);
	s.spacingMultiplier = 2.0f;
	s.viewForwardBias = std::clamp(s.viewForwardBias, 0.0f, 1.0f);
	s.scrollHysteresis = std::clamp(s.scrollHysteresis, 0.0f, 4.0f);
	s.cascadeEdgeFade = std::clamp(s.cascadeEdgeFade, 0.01f, 3.0f);
	s.probesPerFrame = std::min(s.probesPerFrame, RD::WORLD_PROBE_COUNT);
	s.fullUpdateRatio = std::clamp(s.fullUpdateRatio, 0.0f, 1.0f);
	s.cascadePriorityFalloff = std::clamp(s.cascadePriorityFalloff, 0.01f, 1.0f);
	s.maxTraceDistance = std::clamp(s.maxTraceDistance, 1.0f, 100000.0f);
	s.traceBias = std::clamp(s.traceBias, 0.0001f, 0.25f * s.baseSpacing);
	s.visibilityVariance = std::clamp(s.visibilityVariance, 0.000001f, 0.1f);
	s.visibilityExponent = std::clamp(s.visibilityExponent, 1.0f, 128.0f);
	s.backfaceThreshold = std::clamp(s.backfaceThreshold, 0.0f, 1.0f);
	s.skyHistoryWeight = std::clamp(s.skyHistoryWeight, 0.0f, 0.99f);
	s.minFrontfaceDistance = std::clamp(s.minFrontfaceDistance, 0.001f, 100.0f);
	s.relocationReturnStep = std::clamp(s.relocationReturnStep, 0.0f, 0.49f);
	s.relocationCellMargin = std::clamp(s.relocationCellMargin, 0.001f, 0.49f);
	s.relocationMoveEpsilon = std::clamp(s.relocationMoveEpsilon, 0.00001f, 0.1f * s.baseSpacing);
	s.bounceUpdateRate = std::clamp(s.bounceUpdateRate, 0.001f, 1.0f);
	s.confidenceHalfLife = std::clamp(s.confidenceHalfLife, 0.05f, 120.0f);
	s.minSampleConfidence = std::clamp(s.minSampleConfidence, 0.0f, 1.0f);
	s.injectionRadius = std::clamp(s.injectionRadius, 0.1f, 10000.0f);
	s.debugMode = std::min(s.debugMode, 6u);
	s.debugCascade = std::clamp(s.debugCascade, -1, 4);

	WorldProbeHeader h{};
	h.layoutInfo = glm::uvec4(5u, s.enabled ? RD::WORLD_PROBE_COUNT : 0u, 64u, 16u);
	h.gather = glm::vec4(s.injectionRadius, s.pauseUpdates ? 0.0f : std::max(deltaSeconds, 0.0f),
		std::clamp(sky.mieAlbedo, 0.0f, 1.0f), std::clamp(sky.mieG, -0.95f, 0.95f));
	h.blend = glm::vec4(s.skyHistoryWeight, s.bounceUpdateRate, s.confidenceHalfLife, s.minSampleConfidence);
	h.relocation0 = glm::vec4(s.minFrontfaceDistance, s.backfaceThreshold, s.relocationReturnStep, s.relocation ? 1.0f : 0.0f);
	h.relocation1 = glm::vec4(s.relocationCellMargin, s.relocationMoveEpsilon, s.visibilityVariance, s.visibilityExponent);
	h.atlas = glm::uvec4(1280u, 800u, 10u, 128u);
	const uint32_t regular = s.enabled && !s.pauseUpdates ? s.probesPerFrame : 0u;
	const uint32_t full = regular == 0u ? 0u : std::clamp(
		uint32_t(std::round(float(regular) * s.fullUpdateRatio)), 1u, regular);
	const auto fullCounts = DistributeWorldProbeBudget(full, s.cascadePriorityFalloff);
	const auto relightCounts = DistributeWorldProbeBudget(regular - full, s.cascadePriorityFalloff);
	h.schedule = glm::uvec4(regular, uint32_t(std::round(s.fullUpdateRatio * 65535.0f)), RD::WORLD_PROBE_COUNT,
		(s.quarterResolutionScrollIn ? WORLD_PROBE_QUARTER_SCROLL : 0u) |
		(s.enabled && s.injectSSGI && ssgiValid ? WORLD_PROBE_SSGI_VALID : 0u));

	const bool spacingChanged = state.hasSettings && old.cascades[0].originSpacing.w != s.baseSpacing;
	if (state.hasSettings && (spacingChanged || (s.enabled && !state.wasEnabled) ||
		old.cascades[0].trace.w != s.traceBias || old.relocation1.w != h.relocation1.w))
		RequestWorldProbeReset();

	// Camera local -Z is forward; only horizontal XZ movement biases the lattice.
	glm::vec3 forward = -glm::vec3(scene.invView[2]);
	forward.y = 0.0f;
	const float length2 = glm::dot(forward, forward);
	if (length2 > 1e-8f) state.lastHorizontalForward = forward / std::sqrt(length2);
	forward = state.lastHorizontalForward;
	const glm::uvec3 dims(16u, 8u, 16u);
	const glm::vec3 halfDims = 0.5f * glm::vec3(dims - glm::uvec3(1u));
	for (uint32_t c = 0; c < 5u; ++c)
	{
		auto& runtime = state.cascades[c];
		auto& cascade = h.cascades[c];
		const float spacing = s.baseSpacing * float(1u << c);
		const glm::vec3 focus = (s.followCamera ? glm::vec3(scene.cameraPos) : s.anchor) +
			s.centerBias + forward * (s.viewForwardBias * 8.0f * spacing);
		const glm::vec3 cellFocus = focus / spacing;
		if (!runtime.hasBase || spacingChanged)
		{
			runtime.baseCell = glm::ivec3(glm::floor(cellFocus - halfDims + 0.5f));
			runtime.hasBase = true;
		}
		else if (!s.freezeClipmaps)
		{
			const glm::vec3 center = glm::vec3(runtime.baseCell) + halfDims;
			for (int a = 0; a < 3; ++a)
				if (std::abs(cellFocus[a] - center[a]) > 0.5f + s.scrollHysteresis)
					runtime.baseCell[a] = int32_t(std::floor(cellFocus[a] - halfDims[a] + 0.5f));
		}
		cascade.dimsCount = glm::uvec4(dims, c * 2048u);
		cascade.originSpacing = glm::vec4(glm::vec3(runtime.baseCell) * spacing, spacing);
		cascade.baseCell = glm::ivec4(runtime.baseCell, 2048);
		cascade.trace = glm::vec4(s.maxTraceDistance, std::sqrt(3.0f) * spacing, s.cascadeEdgeFade, s.traceBias);
		cascade.schedule = glm::uvec4(runtime.fullCursor, fullCounts[c], runtime.relightCursor, relightCounts[c]);
		if (regular != 0u)
		{
			runtime.fullCursor = (runtime.fullCursor + fullCounts[c]) % 2048u;
			runtime.relightCursor = (runtime.relightCursor + relightCounts[c]) % 2048u;
		}
	}

	const std::array<glm::vec4, 11> skyInputs = {
		scene.atmosphereRayleigh, scene.atmosphereMie, scene.atmosphereAbsorption,
		scene.atmosphereGeometry, scene.atmospherePlacement, scene.atmosphereScattering,
		scene.atmosphereSun, scene.atmosphereGround, scene.sunlightColor,
		scene.sunlightDirection, glm::vec4(scene.atmosphereIntegration)
	};
	if (state.hasSettings)
	{
		bool skyChanged = h.gather.z != old.gather.z || h.gather.w != old.gather.w;
		for (size_t i = 0; i < skyInputs.size(); ++i)
			skyChanged |= !WorldProbeRuntimeState::SameProbeVector(skyInputs[i], state.skyInputs[i]);
		if (skyChanged) ++state.skyRevision;
		geometryDirty |= !WorldProbeRuntimeState::SameProbeVector(h.relocation0, old.relocation0) ||
			h.relocation1.x != old.relocation1.x || h.relocation1.y != old.relocation1.y ||
			h.cascades[0].trace.x != old.cascades[0].trace.x;
	}
	if (geometryDirty) ++state.geometryRevision;
	state.skyInputs = skyInputs;
	state.hasSettings = true;
	state.wasEnabled = s.enabled;
	h.revisions = glm::uvec4(m_frameNumber, state.epoch, state.skyRevision, state.geometryRevision);
	m_worldProbesHeader = h;
	m_worldProbesPush.update = glm::uvec4(0u,
		s.enabled && state.resetRequested != state.resetSubmitted ? 1u : 0u,
		(h.schedule.w & WORLD_PROBE_SSGI_VALID) != 0u ? 1u : 0u, m_frameNumber);
	m_renderGraphState.SetWorldProbeSSGIValid(m_worldProbesPush.update.z != 0u);

	// This frame's fence has been waited before UpdateRendererContext. The mapped
	// upload allocation belongs to this FrameContext and remains alive until cleanup.
	const auto& upload = GetCurrentFrame().GetGPUBuffer(RD::Renderer_Buffer::WorldProbeFrameInfo);
	ASSERT(upload.m_mappedPtr != nullptr && upload.m_address != 0u);
	std::memcpy(upload.m_mappedPtr, &h, sizeof(h));
	VK_CHECK(vmaFlushAllocation(m_allocator.GetVma(), upload.m_allocation, 0u, sizeof(h)));
	state.preparedResetRequest = state.resetRequested;
	state.framePrepared = true;
}

void Renderer::OnWorldProbesSubmitted() noexcept
{
	auto& state = m_worldProbesState;
	if (!state.framePrepared) return;
	if (m_worldProbesHeader.layoutInfo.y != 0u && m_worldProbesPush.update.y != 0u)
		state.resetSubmitted = state.preparedResetRequest;
	state.framePrepared = false;
}
