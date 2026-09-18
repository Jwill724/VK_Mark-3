#pragma once

#include "renderer/RendererDefinitions.h"
#include <vector>
#include <memory>
#include <Core.h>

namespace RD = RendererDefinitions;

// Virtual control over instances, enables true instancing with unique transforms
struct VirtualInstance
{
	uint32_t instanceID = UINT32_MAX;         // The singular model index tag
	uint8_t sceneID = UINT8_MAX;              // Unordered map id to map this back to loadedScenes
	RD::InstancingMethod instancingMethod =
		RD::InstancingMethod::DrawStatic;     // Controls how an asset is treated in drawing
	glm::vec3 modelOffset{ 0.0f };            // Divides spacing in world space between models

	// Only first transform should change at runtime to move through the global transform vector
	glm::mat4 baseTransform = glm::mat4(0.0f); // First transform matrix in global list
	uint32_t firstTransform = 0;               // start of this instance's transform slab (copy 0, slot 0)
	uint32_t transformCount = 0;               // transforms PER COPY (unique node slots)
	uint32_t perInstanceStride = 0;            // rows PER COPY (meshes/primitives in bakedInstances)

	uint32_t usedCopies = 1;                   // active copies (drawn / in InstanceState)
	uint32_t capacityCopies = 1;               // allocated copies in transform slab (contiguous storage)

	float spinAngleRadians = 0.0f;
	float movePhaseRadians = 0.0f;

	uint32_t flagsForce = 0u;
	uint32_t flagsMask = ~0u;
};

struct BinKey
{
	uint32_t meshID;      // INVALID_U32 = empty slot
	uint32_t materialID;
	uint32_t binID;
};

struct DrawBinKeys
{
	std::vector<BinKey>     hashTable;  // BIN_TABLE_SIZE entries, open addressing
	std::vector<glm::uvec2> denseKeys;  // binID -> {meshID, materialID}, MAX_DRAW_BINS entries
};

struct BinTableBuild
{
	DrawBinKeys binKeys;
	uint32_t binCount = 0;
};

// must match the GLSL hash bit for bit
static uint32_t BinHash(uint32_t meshID, uint32_t materialID)
{
	return ((meshID * 2654435761u) ^ (materialID * 2246822519u)) & (RD::BIN_TABLE_SIZE - 1u);
}

struct AsteroidState
{
	glm::vec3 basePosition;
	float     scale;

	glm::vec3 tumbleAxis;
	float     tumbleRate;      // radians/sec
	float     tumblePhase;

	glm::vec3 driftAmplitude;  // world units
	glm::vec3 driftFrequency;  // Hz
	glm::vec3 driftPhase;
};

struct Node
{
	std::weak_ptr<Node> parent;
	std::vector<std::shared_ptr<Node>> children;

	glm::mat4 localTransform{ 1.0f };
	glm::mat4 worldTransform{ 1.0f };

	constexpr void RefreshTransform(const glm::mat4& parentMatrix) noexcept
	{
		worldTransform = parentMatrix * localTransform;
		for (auto& c : children)
			if (c) c->RefreshTransform(worldTransform);
	}
};

// For use in bend studios screen space contact shadows
struct DispatchData {
	glm::ivec3 waveCount{0};   // Dispatch values passed to compute scope
	glm::ivec2 waveOffset{0};  // Offset passed to push constant equivalent
};
// For use in bend studios screen space contact shadows
struct DispatchList {
	glm::vec4 lightCoords{0.0f}; // Same between all disptaches

	DispatchData dispatch[8]{};
	int dispatchCount = 0;
};

// UNIFORM BUFFER TYPES
struct alignas(16) SceneInfo
{
	glm::mat4 view = glm::mat4{1.0f};
	glm::mat4 proj = glm::mat4{1.0f};
	glm::mat4 projUnjittered{1.0f};
	glm::mat4 invView{1.0f};
	glm::mat4 prevInvView{1.0f};
	glm::mat4 invProj{1.0f};
	glm::mat4 viewProj{1.0f};
	glm::mat4 prevViewProjUnjittered{1.0f};
	glm::mat4 prevView{1.0f};
	glm::mat4 viewProjUnjittered{1.0f};
	// x = frameNumber, y = historyValid (0/1), z = Hi-Z valid(0/1)
	glm::uvec4 temporal{0};
	// x = current jitter x ndc
	// y = current jitter y
	// z = previous jitter x
	// w = previous jitter y
	glm::vec4 temporalJitter{0.0f};
	glm::vec4 taaMipParams{0.0f}; // .x = bias (negative, 0 = off), .y = fade start lod, .z = 1 / fade span
	glm::vec4 sunlightDirection{0.0f};
	// w for sun power
	glm::vec4 sunlightColor{0.0f};
	glm::vec4 cameraPos{0.0f};         // xyz pos
	glm::vec4 cameraClips{0.0f};       // .x near and .y far

	glm::vec4 renderExtentSize{0.0f}; // .x and .y for width and height, .z for pixel count
	glm::vec4 displayExtentSize{0.0f};
	glm::vec4 renderPixelSizes{0.0f}; // .x/.y = (1 / full extent) .z/.w = (1 / half extent)
	glm::vec4 displayPixelSizes{0.0f};

	glm::vec2 tanHalfFov;              // 1 / proj[0][0], 1 / proj[1][1]
	float depthLinearizeMult;          // -proj[3][2]
	float depthLinearizeAdd;           //  proj[2][2]
	glm::vec2 ndcToViewMult;           // tanHalfFov.x *  2, tanHalfFov.y * -2
	glm::vec2 ndcToViewAdd;            // tanHalfFov.x * -1, tanHalfFov.y *  1
	glm::mat4 flashlightVP{0.0f};

	glm::vec4 atmosphereRayleigh{ 0.0f };
	glm::vec4 atmosphereMie{ 0.0f };
	glm::vec4 atmosphereAbsorption{ 0.0f };
	glm::vec4 atmosphereGeometry{ 0.0f };
	glm::uvec4 atmosphereIntegration{ 0u };

	glm::vec4 atmospherePlacement{ 0.0f };
	glm::vec4 atmosphereScattering{ 0.0f };
	glm::vec4 atmosphereSun{ 0.0f };
	glm::vec4 atmosphereGround{ 0.0f };

	RD::RenderTargetIDs renderTargetIDs{};
};

struct alignas(16) DirectionalCSMInfo
{
	glm::mat4 cascadeVP[RD::MAX_SHADOW_CASCADES]{0.0f};
	glm::mat4 cascadeLightViews[RD::MAX_SHADOW_CASCADES]{0.0f};
	glm::mat4 cascadeInvTransVP[RD::MAX_SHADOW_CASCADES]{0.0f};
	glm::vec4 cascadeSplits{0.0f};
	// x=shadowAtlasID, y=cascadeCount, z=atlasTexelSize, w=eplison
	glm::vec4 params{ 0.0f };
	// xy = uvScale, zw = uvOffset (per cascade)
	glm::vec4 atlasUV[RD::MAX_SHADOW_CASCADES]{};
	glm::vec4 maxPcfFilterRadiusTexels{0.0f};
	glm::vec4 maxPcssFilterRadiusTexels{0.0f};
	glm::vec4 cascadeWorldTexels{0.0f};
	// x = tan(sunAngularRadius), y = minFilterRadiusTexels,
	// z = searchRadiusScale, w = maxNormalOffsetTexels
	glm::vec4 pcss{ 0.0f };
	// x = contactOffsetTexels, y = offsetGapFraction
	glm::vec4 pcssBias{ 0.0f };
};

struct alignas(16) VolumetricShadowInfo
{
	glm::mat4 cascadeVP;
	glm::mat4 cascadeLightView;
	glm::vec4 params;
	// x = shadow map ID
	// y = cascadeWorldTexel
	// z = shadow texel size
	// w = light-space epsilon

	glm::vec4 receiverLSMin;
	glm::vec4 receiverLSMax;
};

struct ShadowControl
{
	float splitLambda                = 0.96f;
	float bias                       = 0.0001f;
	float shadowFar                  = 1000.0f;
	float lsEpsilon                  = 1.0f;

	float sunAngularRadiusDeg        = 1.5f;
	float minFilterRadiusTexels      = 0.75f;
	float searchRadiusScale          = 1.0f;
	float maxNormalOffsetTexels      = 4.0f;

	float pcssContactOffsetTexels = 1.0f;
	float pcssOffsetGapFraction = 0.25f;

	glm::vec4 pcssMaxRadiusTexels = { 6.0f, 3.0f, 2.0f, 1.0f };

	// Doubles the far depth range
	bool enableShadowDepthExtendHack = false;
};

// Some chunk of a flat array that needs to go
struct DirtyRange
{
	uint32_t offset = 0;
	uint32_t count = 0;
};
