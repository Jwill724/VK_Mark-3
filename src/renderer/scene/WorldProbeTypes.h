#pragma once

#include <glm/glm.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include "renderer/RendererDefinitions.h"

inline constexpr uint32_t WORLD_PROBE_QUEUE_COUNT = 3u;
inline constexpr uint32_t WORLD_PROBE_SCROLL_QUEUE = 0u;
inline constexpr uint32_t WORLD_PROBE_FULL_QUEUE = 1u;
inline constexpr uint32_t WORLD_PROBE_RELIGHT_QUEUE = 2u;
inline constexpr uint32_t WORLD_PROBE_QUARTER_SCROLL = 1u;
inline constexpr uint32_t WORLD_PROBE_SSGI_VALID = 2u;

struct alignas(16) WorldProbeCascadeDesc
{
	glm::uvec4 dimsCount{ 0u };
	glm::vec4 originSpacing{ 0.0f };
	glm::ivec4 baseCell{ 0 };
	glm::vec4 trace{ 0.0f }; // tMax, normalized visibility range, edge fade, ray bias
	glm::uvec4 schedule{ 0u }; // full start/count, relight start/count
};

struct alignas(16) WorldProbeHeader
{
	glm::uvec4 layoutInfo{ 0u }; // cascade count, enabled probe count, full rays, quarter rays
	glm::uvec4 revisions{ 0u };
	glm::vec4 gather{ 0.0f };
	glm::vec4 blend{ 0.0f };
	glm::vec4 relocation0{ 0.0f };
	glm::vec4 relocation1{ 0.0f };
	glm::uvec4 atlas{ 0u };
	glm::uvec4 schedule{ 0u };
	WorldProbeCascadeDesc cascades[RendererDefinitions::WORLD_PROBE_CASCADE_COUNT]{};
};
using WorldProbeFrameInfo = WorldProbeHeader;

struct alignas(16) WorldProbeData
{
	glm::vec4 position{ 0.0f };
	glm::uvec4 state{ 0u };
	glm::ivec4 cell{ 0 };
	glm::uvec4 meta{ 0u }; // last geometry trace, last relight, stable passes, update count
	glm::vec4 sky[6]{};
	glm::vec4 bounce[6]{};
	glm::uvec4 rayClassMask{ 0u };
};
struct alignas(16) WorldProbeTraceSummary
{
	glm::vec4 distance{ 0.0f };
	glm::uvec4 direction{ 0u }; // bit 31 identifies a quarter-ray index
	glm::uvec4 counts{ 0u };
};
struct alignas(16) WorldProbePush
{
	glm::uvec4 update{ 0u }; // phase/queue, explicit reset, SSGI valid, reserved
};
struct alignas(16) WorldProbeDebugPush
{
	glm::vec4 params{ 0.0f };
	glm::uvec4 mode{ 0u }; // view, show inactive, frame, cascade+1 (0=all)
};
struct WorldProbeDispatchCommand
{
	uint32_t x = 0u, y = 1u, z = 1u;
};

struct alignas(16) WorldProbeCacheInfo
{
	glm::mat4 viewProjection{ 1.0f };
	glm::mat4 inverseViewProjection{ 1.0f };
	glm::mat4 view{ 1.0f };
	glm::vec4 cameraPosition{ 0.0f };
	glm::vec4 depthBias{ 0.1f, 150.0f, 0.02f, 1.0f };
	glm::vec4 fallbackSky{ 0.0f };
	glm::uvec4 options{ 1u, 0u, 0u, 0u }; // 1=fog only, 3=fog+directional surfaces
};

static_assert(offsetof(WorldProbeCacheInfo, inverseViewProjection) == 64);
static_assert(offsetof(WorldProbeCacheInfo, view) == 128);
static_assert(offsetof(WorldProbeCacheInfo, cameraPosition) == 192);
static_assert(offsetof(WorldProbeCacheInfo, depthBias) == 208);
static_assert(offsetof(WorldProbeCacheInfo, fallbackSky) == 224);
static_assert(offsetof(WorldProbeCacheInfo, options) == 240);
static_assert(sizeof(WorldProbeCacheInfo) == 256);

static_assert(sizeof(WorldProbeFrameInfo) == 528);
static_assert(alignof(WorldProbeFrameInfo) == 16);

static_assert(sizeof(WorldProbeCascadeDesc) == 80);
static_assert(offsetof(WorldProbeCascadeDesc, originSpacing) == 16);
static_assert(offsetof(WorldProbeCascadeDesc, baseCell) == 32);
static_assert(offsetof(WorldProbeCascadeDesc, trace) == 48);
static_assert(offsetof(WorldProbeCascadeDesc, schedule) == 64);
static_assert(sizeof(WorldProbeHeader) == 528);
static_assert(offsetof(WorldProbeHeader, revisions) == 16);
static_assert(offsetof(WorldProbeHeader, gather) == 32);
static_assert(offsetof(WorldProbeHeader, blend) == 48);
static_assert(offsetof(WorldProbeHeader, relocation0) == 64);
static_assert(offsetof(WorldProbeHeader, relocation1) == 80);
static_assert(offsetof(WorldProbeHeader, atlas) == 96);
static_assert(offsetof(WorldProbeHeader, schedule) == 112);
static_assert(offsetof(WorldProbeHeader, cascades) == 128);
static_assert(sizeof(WorldProbeData) == 272);
static_assert(offsetof(WorldProbeData, state) == 16);
static_assert(offsetof(WorldProbeData, cell) == 32);
static_assert(offsetof(WorldProbeData, meta) == 48);
static_assert(offsetof(WorldProbeData, sky) == 64);
static_assert(offsetof(WorldProbeData, bounce) == 160);
static_assert(offsetof(WorldProbeData, rayClassMask) == 256);
static_assert(sizeof(WorldProbeTraceSummary) == 48);
static_assert(offsetof(WorldProbeTraceSummary, direction) == 16);
static_assert(offsetof(WorldProbeTraceSummary, counts) == 32);
static_assert(sizeof(WorldProbePush) == 16);
static_assert(sizeof(WorldProbeDebugPush) == 32);
static_assert(sizeof(WorldProbeDispatchCommand) == 12);
static_assert(std::is_trivially_copyable_v<WorldProbeHeader>);
static_assert(std::is_trivially_copyable_v<WorldProbeData>);
static_assert(std::is_trivially_copyable_v<WorldProbePush>);

inline constexpr size_t GetWorldProbeBufferBytes(uint32_t count) noexcept
{
	return sizeof(WorldProbeHeader) + size_t(count) * sizeof(WorldProbeData);
}
inline constexpr size_t GetWorldProbeIndirectOffset(uint32_t queue = 0u) noexcept
{
	return 16u + size_t(WORLD_PROBE_QUEUE_COUNT) * RendererDefinitions::WORLD_PROBE_COUNT * 4u
		+ size_t(queue) * sizeof(WorldProbeDispatchCommand);
}
inline constexpr size_t GetWorldProbeScheduleBytes() noexcept
{
	return GetWorldProbeIndirectOffset(WORLD_PROBE_QUEUE_COUNT);
}
inline constexpr size_t GetWorldProbeSummaryBytes() noexcept
{
	return size_t(RendererDefinitions::WORLD_PROBE_COUNT) * sizeof(WorldProbeTraceSummary);
}
inline constexpr size_t GetWorldProbeVisibilityBytes() noexcept
{
	return size_t(1280u) * 800u * 2u;
}

struct WorldProbeSettings
{
	bool enabled = true;
	bool injectSSGI = true;
	bool pauseUpdates = false;
	bool freezeClipmaps = false;
	bool followCamera = true;
	bool relocation = true;
	bool quarterResolutionScrollIn = true;
	glm::vec3 anchor{ 0.0f };
	glm::vec3 centerBias{ 0.0f };
	float baseSpacing = 3.0f;
	float spacingMultiplier = 2.0f;
	float viewForwardBias = 0.5f;
	float scrollHysteresis = 0.5f;
	float cascadeEdgeFade = 1.5f;
	uint32_t probesPerFrame = 1024u;
	float fullUpdateRatio = 0.16f;
	float cascadePriorityFalloff = 0.5f;
	float maxTraceDistance = 500.0f;
	float traceBias = 0.2f;
	float visibilityVariance = 0.02f;
	float visibilityExponent = 32.0f;
	float backfaceThreshold = 0.2f;
	float skyHistoryWeight = 0.85f;
	float minFrontfaceDistance = 0.5f;
	float relocationReturnStep = 0.1f;
	float relocationCellMargin = 0.01f;
	float relocationMoveEpsilon = 0.01f;
	float bounceUpdateRate = 0.25f;
	float confidenceHalfLife = 4.0f;
	float minSampleConfidence = 0.25f;
	float injectionRadius = 6.0f;
	bool debugDraw = false;
	bool debugShowInactive = true;
	uint32_t debugMode = 0u;
	int32_t debugCascade = -1;
	float debugRadius = 0.12f;
	float debugMaxDistance = 80.0f;
	float debugIntensity = 1.0f;

	bool reconstructDirectFallback = true;
	bool reconstructDebugView = false;
	bool cacheFog = true;
	bool cacheTransparency = true;
};

struct alignas(16) WorldProbeReconstructPush
{
	uint32_t directFallback = 1u;
	uint32_t debugView = 0u;
	uint32_t pad0 = 0u;
	uint32_t pad1 = 0u;
};

struct alignas(16) WorldProbeCachePush
{
	uint32_t flags = 3u;
	uint32_t pad0 = 0u;
	uint32_t pad1 = 0u;
	uint32_t pad2 = 0u;
};

struct WorldProbeCascadeRuntimeState
{
	glm::ivec3 baseCell{ 0 };
	uint32_t fullCursor = 0u;
	uint32_t relightCursor = 0u;
	bool hasBase = false;
};

struct WorldProbeRuntimeState
{
	std::array<WorldProbeCascadeRuntimeState, RendererDefinitions::WORLD_PROBE_CASCADE_COUNT> cascades{};
	glm::vec3 lastHorizontalForward{ 0.0f, 0.0f, -1.0f };
	uint32_t epoch = 1u;
	uint32_t skyRevision = 1u;
	uint32_t geometryRevision = 1u;
	uint64_t resetRequested = 1u;
	uint64_t resetSubmitted = 0u;
	uint64_t preparedResetRequest = 0u;
	bool hasSettings = false;
	bool wasEnabled = false;
	bool framePrepared = false;
	std::array<glm::vec4, 11> skyInputs{};
	static bool SameProbeVector(const glm::vec4& a, const glm::vec4& b) noexcept
	{
		return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
	}
};
