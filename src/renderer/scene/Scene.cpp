#include "pch.h"

#include "scene.h"
#include "../../profiler/Profiler.h"
#include "EngineTypes.h"
#include "LightUnits.h"

constexpr float kMaxHiZTranslation = 1.5f;
constexpr float kMaxHiZRotationDeg = 15.0f;
constexpr float kTaaMipBias        = -0.5f;
constexpr float kTaaBiasFadeLod    = 2.0f;
constexpr float kTaaBiasFadeSpan   = 4.0f;

static constexpr uint32_t AA_OFF_U32 = static_cast<uint32_t>(RD::AntiAliasingMethod::AA_OFF);

static glm::vec2 BuildTemporalJitterPixels(uint32_t frameIndex);
static glm::vec2 ConvertJitterPixelsToNDC(
	const glm::vec2 jitterPixels,
	const float width,
	const float height);
static glm::mat4 ApplyProjectionJitter(
	glm::mat4 proj,
	const glm::vec2 jitterNDC);
static float HaltonSequence(uint32_t index, uint32_t base);

static constexpr glm::vec4 BuildAtlasUV(
	VkExtent2D atlasExtent,
	VkExtent2D tileExtent,
	uint32_t tileX,
	uint32_t tileY,
	uint32_t borderPixels);

glm::vec3 Scene::GetLightDir() { return glm::normalize(glm::vec3(m_sceneInfo.sunlightDirection)); }

void Scene::InitScene(glm::vec3 spawn)
{
	m_camera.SetSpawnPoint(spawn);
	m_camera.SetPosition(spawn);
	m_camera.SetYaw(-90.0f);
	m_camera.SetFovY(90.0f);

	m_camera.SetSensitivity(50.0f); // Feels good on 1600dpi
	m_camera.SetMaxSpeed(16.0f);
	m_camera.SetMinSpeed(3.0f);

	m_camera.SetAcceleration(10.0f);
	m_camera.SetDamping(8.0f);

	m_sceneInfo.cameraClips = glm::vec4(m_camera.GetNearClip(), m_camera.GetFarClip(), 0.0f, 0.0f);

	m_currentJitterNDC = glm::vec2(0.0f);
	m_previousJitterNDC = glm::vec2(0.0f);

	glm::vec3 tint(1.0f, 0.55f, 0.2f);
	tint /= (0.2126f * tint.r + 0.7152f * tint.g + 0.0722f * tint.b);
	m_sceneInfo.sunlightColor = glm::vec4(tint, LightUnits::SUN_LOW_GOLDEN);
	m_sceneInfo.sunlightDirection = glm::vec4(0.36f, 0.46f, -0.09f, 0.0f);

	m_shadowControl.shadowFar = m_shadowFar;
}

bool Scene::UpdateCamera(
	Extents2D renderExtent,
	Extents2D displayExtent,
	Profiler& profiler,
	GLFWwindow* window,
	bool isTemporalAllowed)
{
	bool isTemporalInvalid = false;

	const auto& aaMode = profiler.debugToggles.aaMode;
	const bool jitterOn = (aaMode != AA_OFF_U32) && isTemporalAllowed;

	if (aaMode != m_lastAaMode || jitterOn != m_lastJitterOn)
	{
		m_lastAaMode = aaMode;
		m_lastJitterOn = jitterOn;
		isTemporalInvalid = true;
	}

	const glm::mat4 lastView               = m_sceneInfo.view;
	const glm::mat4 lastInvView            = m_sceneInfo.invView;
	const glm::mat4 lastViewProjUnjittered = m_sceneInfo.viewProjUnjittered;

	const float dWidth = static_cast<float>(displayExtent.Width());
	const float dHeight = static_cast<float>(displayExtent.Height());

	const float rWidth  = static_cast<float>(renderExtent.Width());
	const float rHeight = static_cast<float>(renderExtent.Height());
	const float aspect = rWidth / rHeight;

	m_camera.ProcessInput(
		window,
		profiler,
		renderExtent,
		isTemporalInvalid);

	// Determines if the camera moves too fast for occlusion culling
	const glm::vec3 previousPosition = m_camera.GetPreviousPosition();
	const glm::quat previousRotation = m_camera.GetPreviousRotation();

	const glm::vec3 currentPosition = m_camera.GetPosition();
	const glm::quat currentRotation = m_camera.GetRotation();

	const float angle         = glm::degrees(glm::angle(glm::inverse(previousRotation) * currentRotation));
	const float positionDelta = glm::length(currentPosition - previousPosition);

	m_sceneInfo.temporal.z = positionDelta <= kMaxHiZTranslation && angle <= kMaxHiZRotationDeg;

	m_curCamView = m_camera.GetViewMatrix();

	m_curCamProjUnjittered = glm::perspectiveRH_ZO(
		glm::radians(m_camera.GetFovY()),
		aspect,
		m_camera.GetFarClip(),  // This is required for reversed z to work
		m_camera.GetNearClip());

	m_previousJitterNDC = m_currentJitterNDC;

	glm::vec2 jitterNDC = glm::vec2(0.0f);

	if (jitterOn)
	{
		glm::vec2 jitterPixels = BuildTemporalJitterPixels(m_sceneInfo.temporal.x);
		jitterNDC = ConvertJitterPixelsToNDC(jitterPixels, rWidth, rHeight);
	}

	m_sceneInfo.prevView               = lastView;
	m_sceneInfo.prevInvView            = lastInvView;
	m_sceneInfo.prevViewProjUnjittered = lastViewProjUnjittered;

	m_currentJitterNDC = jitterNDC;
	m_curCamProjJittered = ApplyProjectionJitter(m_curCamProjUnjittered, m_currentJitterNDC);

	// Compute both unjittered and jittered viewprojs up front
	glm::mat4 currentViewProjUnjittered = m_curCamProjUnjittered * m_curCamView;
	glm::mat4 currentViewProjJittered = m_curCamProjJittered * m_curCamView;

	m_sceneInfo.view = m_curCamView;
	m_sceneInfo.invView = glm::inverse(m_curCamView);

	if (jitterOn)
	{
		m_sceneInfo.proj = m_curCamProjJittered;
		m_sceneInfo.invProj = glm::inverse(m_curCamProjJittered);
		m_sceneInfo.viewProj = currentViewProjJittered;

		m_sceneInfo.temporalJitter = glm::vec4(
			m_currentJitterNDC.x,
			m_currentJitterNDC.y,
			m_previousJitterNDC.x,
			m_previousJitterNDC.y);
	}
	else
	{
		m_sceneInfo.proj = m_curCamProjUnjittered;
		m_sceneInfo.invProj = glm::inverse(m_curCamProjUnjittered);
		m_sceneInfo.viewProj = currentViewProjUnjittered;

		m_sceneInfo.temporalJitter = glm::vec4(0.0f);
	}

	m_sceneInfo.tanHalfFov.x = 1.0f / m_sceneInfo.proj[0][0];
	m_sceneInfo.tanHalfFov.y = 1.0f / m_sceneInfo.proj[1][1];

	m_sceneInfo.depthLinearizeMult = -m_sceneInfo.proj[3][2];
	m_sceneInfo.depthLinearizeAdd = m_sceneInfo.proj[2][2];

	if (m_sceneInfo.depthLinearizeMult * m_sceneInfo.depthLinearizeAdd < 0.0)
	{
		m_sceneInfo.depthLinearizeAdd = -m_sceneInfo.depthLinearizeAdd;
	}

	m_sceneInfo.ndcToViewMult = glm::vec2(
		m_sceneInfo.tanHalfFov.x * 2.0f,
		m_sceneInfo.tanHalfFov.y * -2.0f);

	m_sceneInfo.ndcToViewAdd = glm::vec2(
		m_sceneInfo.tanHalfFov.x * -1.0f,
		m_sceneInfo.tanHalfFov.y * 1.0f);

	m_sceneInfo.projUnjittered = m_curCamProjUnjittered;

	m_sceneInfo.viewProjUnjittered = currentViewProjUnjittered; // unjittered current — velocity curr NDC

	m_sceneInfo.cameraPos = glm::vec4(m_camera.GetPosition(), 0.0f);

	if (m_sceneInfo.renderExtentSize.x != rWidth ||
		m_sceneInfo.renderExtentSize.y != rHeight ||
		m_sceneInfo.displayExtentSize.x != dWidth ||
		m_sceneInfo.displayExtentSize.y != dHeight)
	{
		// ===================
		// Render extent info
		// ===================
		float renderPixelCount = rWidth * rHeight;
		glm::vec2 fullRenderPixelSize = 1.0f / glm::vec2(rWidth, rHeight);
		VkExtent3D halfRenderExtent = {
			(renderExtent.Width() + 1u) >> 1,
			(renderExtent.Height() + 1u) >> 1,
			1u
		};
		glm::vec2 halfRenderPixelSize = 1.0f /
			glm::vec2(
				static_cast<float>(halfRenderExtent.width),
				static_cast<float>(halfRenderExtent.height));

		m_sceneInfo.renderPixelSizes = glm::vec4(
			fullRenderPixelSize.x,
			fullRenderPixelSize.y,
			halfRenderPixelSize.x,
			halfRenderPixelSize.y);
		m_sceneInfo.renderExtentSize = glm::vec4(rWidth, rHeight, renderPixelCount, 0.0);


		// ====================
		// Display extent info
		// ====================
		float displayPixelCount = dWidth * dHeight;
		glm::vec2 fullDisplayPixelSize = 1.0f / glm::vec2(dWidth, dHeight);
		VkExtent3D halfDisplayExtent = {
			(displayExtent.Width() + 1u) >> 1,
			(displayExtent.Height() + 1u) >> 1,
			1u
		};
		glm::vec2 halfDisplayPixelSize = 1.0f /
			glm::vec2(
				static_cast<float>(halfDisplayExtent.width),
				static_cast<float>(halfDisplayExtent.height));
		m_sceneInfo.displayPixelSizes = glm::vec4(
			fullDisplayPixelSize.x,
			fullDisplayPixelSize.y,
			halfDisplayPixelSize.x,
			halfDisplayPixelSize.y);
		m_sceneInfo.displayExtentSize = glm::vec4(dWidth, dHeight, displayPixelCount, 0.0);


		isTemporalInvalid = true;
	}

	// For when preforming upscaling
	// const float mipBias = std::log2(drawExtent / displayWidth) - 0.5f;

	m_sceneInfo.taaMipParams = jitterOn
		? glm::vec4(kTaaMipBias, kTaaBiasFadeLod, 1.0f / kTaaBiasFadeSpan, 0.0f)
		: glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);

	return isTemporalInvalid;
}

void Scene::BuildMotionMatrices(bool bTemporalValid)
{
	const size_t count = m_dynamicTransforms.size();

	if (m_prevDynamicTransforms.size() != count || !bTemporalValid)
	{
		m_motionMatrices.assign(count, glm::mat4(1.0f));
		m_prevDynamicTransforms = m_dynamicTransforms;
		return;
	}

	if (m_motionMatrices.size() != count) m_motionMatrices.resize(count);

	for (size_t i = 0; i < count; ++i)
		m_motionMatrices[i] = m_prevDynamicTransforms[i] * glm::inverse(m_dynamicTransforms[i]);

	m_prevDynamicTransforms = m_dynamicTransforms;
}

static float HaltonSequence(uint32_t index, uint32_t base)
{
	float f = 1.0, r = 0.0;
	while (index > 0)
	{
		f /= static_cast<float>(base);
		r += f * float(index % base);
		index /= base;
	}
	return r;
}

static glm::vec2 BuildTemporalJitterPixels(uint32_t frameIndex)
{
	static const glm::vec2 kMean = []
		{
			glm::vec2 sum(0.0f);
			for (uint32_t i = 1; i <= RD::TAA_SAMPLE_COUNT; ++i)
				sum += glm::vec2(HaltonSequence(i, 2u), HaltonSequence(i, 3u));
			return sum / static_cast<float>(RD::TAA_SAMPLE_COUNT);
		}();

	const uint32_t index = (frameIndex % RD::TAA_SAMPLE_COUNT) + 1u;
	return glm::vec2(HaltonSequence(index, 2u), HaltonSequence(index, 3u)) - kMean;
}

static glm::vec2 ConvertJitterPixelsToNDC(
	const glm::vec2 jitterPixels,
	const float width,
	const float height)
{
	glm::vec2 jitterNDC = glm::vec2(0.0f);

	jitterNDC.x = (2.0f * jitterPixels.x) / width;
	jitterNDC.y = (2.0f * jitterPixels.y) / height;

	return jitterNDC;
}

static glm::mat4 ApplyProjectionJitter(
	glm::mat4 proj,
	const glm::vec2 jitterNDC)
{
	proj[2][0] += jitterNDC.x;
	proj[2][1] += jitterNDC.y;
	return proj;
}

// =============================
// CASCADED SHADOW MAPPING TECH
// =============================

static constexpr glm::vec4 BuildAtlasUV(
	VkExtent2D atlasExtent,
	VkExtent2D tileExtent,
	uint32_t tileX,
	uint32_t tileY,
	uint32_t borderPixels)
{
	const float atlasW = static_cast<float>(atlasExtent.width);
	const float atlasH = static_cast<float>(atlasExtent.height);

	const float tileW  = static_cast<float>(tileExtent.width);
	const float tileH  = static_cast<float>(tileExtent.height);

	const float borderU = static_cast<float>(borderPixels) / atlasW;
	const float borderV = static_cast<float>(borderPixels) / atlasH;

	const float offsetX = static_cast<float>(tileX) * tileW;
	const float offsetY = static_cast<float>(tileY) * tileH;

	const float offsetU = (offsetX / atlasW) + borderU;
	const float offsetV = (offsetY / atlasH) + borderV;

	const float scaleU  = (tileW / atlasW) - 2.0f * borderU;
	const float scaleV  = (tileH / atlasH) - 2.0f * borderV;

	return glm::vec4(scaleU, scaleV, offsetU, offsetV);
}

void Scene::InitCSMInfo(uint32_t atlasWidth, uint32_t atlasHeight, uint32_t bindlessID)
{
	const VkExtent2D atlasExtent = {
		atlasWidth,
		atlasHeight
	};

	// For a 2x2 grid:
	const uint32_t tilesPerRow = 2u;
	const VkExtent2D tileExtent = {
		atlasWidth / tilesPerRow,
		atlasHeight / tilesPerRow
	};

	m_csmAtlasWidth = atlasWidth;
	m_csmAtlasHeight = atlasHeight;

	const uint32_t borderPixels = 2;

	for (uint32_t cascadeIndex = 0; cascadeIndex < RD::MAX_SHADOW_CASCADES; ++cascadeIndex)
	{
		const uint32_t tileX = cascadeIndex % tilesPerRow;
		const uint32_t tileY = cascadeIndex / tilesPerRow;

		m_csmInfo.atlasUV[cascadeIndex] = BuildAtlasUV(atlasExtent, tileExtent, tileX, tileY, borderPixels);
	}

	m_csmAtlasTileRes = static_cast<float>(atlasWidth / tilesPerRow);

	m_pcssTexel = 1.0f / static_cast<float>(atlasWidth);
	m_pcfTexel = 1.0f / m_csmAtlasTileRes;

	m_csmInfo.params.x = static_cast<float>(bindlessID);
	m_csmInfo.params.y = static_cast<float>(RD::MAX_SHADOW_CASCADES);
	m_csmInfo.params.w = m_shadowControl.lsEpsilon;
	m_csmInfo.maxPcfFilterRadiusTexels = { 1.0f, 1.1f, 1.2f, 1.5f };

	UpdatePCSSParams();

	m_bShouldSplitsUpdate = true;
}

void Scene::UpdatePCSSParams()
{
	// Gaussian-weighted Vogel disk: exp(-2d^2) over a uniform-area disk pulls the
	// effective RMS radius to ~0.83 of nominal.
	static constexpr float PCSS_KERNEL_RMS = 0.83f;

	m_csmInfo.pcss.x = std::tan(glm::radians(m_shadowControl.sunAngularRadiusDeg)) / PCSS_KERNEL_RMS;
	m_csmInfo.pcss.y = m_shadowControl.minFilterRadiusTexels;
	m_csmInfo.pcss.z = m_shadowControl.searchRadiusScale;
	m_csmInfo.pcss.w = std::max(m_shadowControl.maxNormalOffsetTexels, 1.0f);

	m_csmInfo.maxPcssFilterRadiusTexels = m_shadowControl.pcssMaxRadiusTexels;

	m_csmInfo.pcssBias.x = m_shadowControl.pcssContactOffsetTexels;
	m_csmInfo.pcssBias.y = m_shadowControl.pcssOffsetGapFraction;
}

void Scene::UpdateShadowTexel(RD::SunShadowFilter filterMode)
{
	if (filterMode == RD::SunShadowFilter::PCF)
	{
		m_csmInfo.params.z = m_pcfTexel;
	}
	else
	{
		m_csmInfo.params.z = m_pcssTexel;
	}
}

// Two great starting points to learn cascade shadow maps
// https://learnopengl.com/Guest-Articles/2021/CSM
// https://www.youtube.com/watch?v=3FMONJ1O39U&list=LL&index=157
// Both GLM_FORCE are enabled globally in hpp within pch
// #define GLM_FORCE_DEPTH_ZERO_TO_ONE
// #define GLM_FORCE_RIGHT_HANDED
// Pipeline depth compare LESS
// CULL MODE: FRONT BIT
void Scene::UpdateCSMInfo()
{
	//constexpr float CASCADE_RADIUS_RATIO[RD::MAX_SHADOW_CASCADES] = {
	//	0.017f,
	//	0.046f,
	//	0.160f,
	//	0.500f
	//};

	// Cascades not dependent FOV
	constexpr float CASCADE_RADIUS_RATIO[RD::MAX_SHADOW_CASCADES] = {
		0.024f,
		0.100f,
		0.500f
	};

	UpdatePCSSParams();

	const auto lightDir = GetLightDir();

	if (m_bShouldSplitsUpdate)
	{
		m_bShouldSplitsUpdate = false;

		// Ranges from 500-1500 (1k is default)
		m_shadowFar = m_shadowControl.shadowFar;

		const float nearClip = m_camera.GetNearClip();
		const float clipRange = m_shadowFar - nearClip;
		const float ratio = m_shadowFar / nearClip;

		// Compute split distances in view space (absolute units)
		for (uint32_t i = 0; i < RD::MAX_SHADOW_CASCADES; ++i)
		{
			const float p              = (static_cast<float>(i) + 1.0f) / static_cast<float>(RD::MAX_SHADOW_CASCADES);
			const float log            = nearClip * std::pow(ratio, p);
			const float uni            = nearClip + (clipRange * p);
			m_csmInfo.cascadeSplits[i] = (m_shadowControl.splitLambda * log) + ((1.0f - m_shadowControl.splitLambda) * uni);

			// Cascade radius and world texels
			float radius = CASCADE_RADIUS_RATIO[i] * m_shadowFar;
			const float worldUnitsPerTexel = (radius * 2.0f) / m_csmAtlasTileRes;
			radius = std::ceil(radius / worldUnitsPerTexel) * worldUnitsPerTexel;
			m_csmInfo.cascadeWorldTexels[i] = worldUnitsPerTexel;

			// Light projection setup
			const glm::vec3 max = glm::vec3(radius);
			glm::vec3 min = -max;

			// Extend depth range to keep shadow visuals consistent
			const float depthRange = max.z - min.z;
			min.z -= depthRange;

			// Hack that accounts for very far away occluders
			if (m_shadowControl.enableShadowDepthExtendHack)
			{
				min.z *= 2.0f;
			}

			// Orthographic projection
			m_cascadeLightProjs[i] = glm::orthoRH_ZO(min.x, max.x, min.y, max.y, min.z, max.z);
		}
	}

	float lastSplitDist = m_camera.GetNearClip();
	for (uint32_t i = 0; i < RD::MAX_SHADOW_CASCADES; ++i)
	{
		const float curSplit = m_csmInfo.cascadeSplits[i];

		const float splitMid = (lastSplitDist + curSplit) * 0.5f;

		const glm::vec3 camPos = m_camera.GetPosition();
		const glm::vec3 camForward = m_camera.GetView();

		const glm::vec3 frustumCenter = camPos + camForward * splitMid;

		// Light view
		const glm::vec3 lightPos = frustumCenter + lightDir;
		const glm::mat4 lightView = glm::lookAtRH(lightPos, frustumCenter, glm::vec3(0.0f, 1.0f, 0.0f));
		m_csmInfo.cascadeLightViews[i] = lightView;

		glm::mat4 shadowMatrix = m_cascadeLightProjs[i] * lightView;

		// This works beautifully, it keeps the shadows 100% stable during movement
		// https://github.com/tonadr1022/vkrender2/blob/main/src/techniques/CSM.cpp
		// scale origin by shadow map size
		// round it (nearest texel)
		// get the offset
		// scale it back down, only use x,y and apply it to vp matrix
		glm::vec3 shadowOrigin  = shadowMatrix * glm::vec4(glm::vec3(0.0f), 1.0f);
		shadowOrigin            = shadowOrigin * m_csmAtlasTileRes / 2.0f;
		glm::vec3 roundedOrigin = glm::round(shadowOrigin);
		glm::vec3 roundOffset   = roundedOrigin - shadowOrigin;
		roundOffset             = roundOffset * 2.0f / m_csmAtlasTileRes;
		roundOffset.z           = 0.0f;
		shadowMatrix[3]        += glm::vec4(roundOffset, 0.0f);
		m_csmInfo.cascadeVP[i]  = shadowMatrix;

		m_csmInfo.cascadeInvTransVP[i] = glm::transpose(glm::inverse(glm::mat3(shadowMatrix)));

		lastSplitDist = curSplit;
	}
}

void Scene::InitVolumetricShadowInfo(
	uint32_t shadowWidth,
	uint32_t shadowHeight,
	uint32_t bindlessID)
{
	ASSERT(shadowWidth == shadowHeight);

	m_volumetricShadowTileRes = static_cast<float>(shadowWidth);

	m_volumetricShadowInfo.params.x = static_cast<float>(bindlessID);

	m_volumetricShadowInfo.params.z = 1.0f / m_volumetricShadowTileRes;

	m_volumetricShadowInfo.params.w = m_shadowControl.lsEpsilon;
}

void Scene::UpdateVolumetricShadowInfo(float maxDistance)
{
	const glm::vec3 lightDir = GetLightDir();

	const float tanHalfFovX = m_sceneInfo.tanHalfFov.x;
	const float tanHalfFovY = m_sceneInfo.tanHalfFov.y;

	const float nearClip = m_camera.GetNearClip();
	const float farClip = m_camera.GetFarClip();

	const float fogFar =
		glm::clamp(
			maxDistance,
			nearClip + 0.001f,
			farClip);

	const glm::mat4 invView = m_sceneInfo.invView;

	glm::vec3 cornersWS[8];

	uint32_t cornerIndex = 0u;

	for (uint32_t zSide = 0u; zSide < 2u; ++zSide)
	{
		const float z =
			(zSide == 0u)
			? nearClip
			: fogFar;

		const float x = z * tanHalfFovX;
		const float y = z * tanHalfFovY;

		for (uint32_t corner = 0u; corner < 4u; ++corner)
		{
			const glm::vec3 viewPos = glm::vec3(
				(corner & 1u) != 0u ? x : -x,
				(corner & 2u) != 0u ? y : -y,
				-z);

			cornersWS[cornerIndex++] = glm::vec3(invView * glm::vec4(viewPos, 1.0f));
		}
	}

	glm::vec3 frustumCenter(0.0f);

	for (const glm::vec3& corner : cornersWS)
	{
		frustumCenter += corner;
	}

	frustumCenter *= 1.0f / 8.0f;

	float radius = 0.0f;

	for (const glm::vec3& corner : cornersWS)
	{
		radius = std::max(radius, glm::length(corner - frustumCenter));
	}

	float worldUnitsPerTexel = (radius * 2.0f) / m_volumetricShadowTileRes;

	radius = std::ceil(radius / worldUnitsPerTexel) * worldUnitsPerTexel;

	worldUnitsPerTexel = (radius * 2.0f) / m_volumetricShadowTileRes;

	m_volumetricShadowInfo.params.y = worldUnitsPerTexel;

	const glm::vec3 lightPos = frustumCenter + lightDir;

	const glm::mat4 lightView =
		glm::lookAtRH(
			lightPos,
			frustumCenter,
			glm::vec3(0.0f, 1.0f, 0.0f));

	m_volumetricShadowInfo.cascadeLightView = lightView;

	glm::vec3 receiverLSMin(FLT_MAX);
	glm::vec3 receiverLSMax(-FLT_MAX);

	for (const glm::vec3& corner : cornersWS)
	{
		const glm::vec3 cornerLS = glm::vec3(lightView * glm::vec4(corner, 1.0f));

		receiverLSMin = glm::min(receiverLSMin, cornerLS);
		receiverLSMax = glm::max(receiverLSMax, cornerLS);
	}

	const float casterExtension = radius;

	const float minZ = receiverLSMin.z;
	const float maxZ = receiverLSMax.z + casterExtension;

	m_volumetricShadowInfo.receiverLSMin =
		glm::vec4(receiverLSMin, 0.0f);

	m_volumetricShadowInfo.receiverLSMax =
		glm::vec4(receiverLSMax, maxZ);

	const glm::mat4 lightProj =
		glm::orthoRH_ZO(
			-radius,
			radius,
			-radius,
			radius,
			minZ,
			maxZ);

	glm::mat4 shadowMatrix = lightProj * lightView;

	glm::vec3 shadowOrigin = glm::vec3(shadowMatrix * glm::vec4(glm::vec3(0.0f), 1.0f));

	shadowOrigin *= m_volumetricShadowTileRes * 0.5f;
	const glm::vec3 roundedOrigin = glm::round(shadowOrigin);
	glm::vec3 roundOffset = roundedOrigin - shadowOrigin;
	roundOffset *= 2.0f / m_volumetricShadowTileRes;
	roundOffset.z = 0.0f;
	shadowMatrix[3] += glm::vec4(roundOffset, 0.0f);

	m_volumetricShadowInfo.cascadeVP = shadowMatrix;
}

static int bend_min(const int a, const int b) { return a > b ? b : a; }
static int bend_max(const int a, const int b) { return a > b ? a : b; }

// Dispatch building logic based on Bend Studio's
// https://www.bendstudio.com/blog/inside-bend-screen-space-shadows/
void Scene::BuildDispatchList(const int waveSize)
{
	const auto lightDir = GetLightDir();
	const glm::vec2 renderExtentSize = { m_sceneInfo.renderExtentSize.x, m_sceneInfo.renderExtentSize.y };

	glm::vec4 lightProj = m_sceneInfo.viewProj * glm::vec4(lightDir, 0.0f);

	DispatchList result;

	// Floating point division in the shader has a practical limit for precision when the light is *very* far off screen (~1m pixels+)
	// So when computing the light XY coordinate, use an adjusted w value to handle these extreme values
	float xy_light_w = lightProj[3];
	const float FP_limit = 0.000002f * static_cast<float>(waveSize);

	if (xy_light_w >= 0 && xy_light_w < FP_limit) xy_light_w = FP_limit;
	else if (xy_light_w < 0 && xy_light_w > -FP_limit) xy_light_w = -FP_limit;

	// Need precise XY pixel coordinates of the light
	result.lightCoords[0] = ((lightProj[0] / xy_light_w) * +0.5f + 0.5f) * renderExtentSize.x;

	// NOTE: Y flip required for my light projection to work
	result.lightCoords[1] = (1.0f - ((lightProj[1] / xy_light_w) * -0.5f + 0.5f)) * renderExtentSize.y;
	result.lightCoords[2] = lightProj[3] == 0 ? 0 : (lightProj[2] / lightProj[3]);
	result.lightCoords[3] = lightProj[3] > 0 ? 1.0f : -1.0f;

	int32_t light_xy[2] =
	{
		static_cast<int32_t>((result.lightCoords[0] + 0.5f)),
		static_cast<int32_t>((result.lightCoords[1] + 0.5f))
	};

	// Make the bounds inclusive, relative to the light
	const int32_t biased_bounds[4] =
	{
		0 - light_xy[0],
		-(static_cast<int32_t>(renderExtentSize.y) - light_xy[1]),
		static_cast<int32_t>(renderExtentSize.x) - light_xy[0],
		-(0 - light_xy[1]),
	};

	// Process 4 quadrants around the light center,
	// They each form a rectangle with one corner on the light XY coordinate
	// If the rectangle isn't square, it will need breaking in two on the larger axis
	// 0 = bottom left, 1 = bottom right, 2 = top left, 2 = top right
	for (int q = 0; q < 4; q++)
	{
		// Quads 0 and 3 needs to be +1 vertically, 1 and 2 need to be +1 horizontally
		bool vertical = q == 0 || q == 3;

		// Bounds relative to the quadrant
		const int bounds[4] =
		{
			bend_max(0, ((q & 1) ? biased_bounds[0] : -biased_bounds[2])) / waveSize,
			bend_max(0, ((q & 2) ? biased_bounds[1] : -biased_bounds[3])) / waveSize,
			bend_max(0, (((q & 1) ? biased_bounds[2] : -biased_bounds[0]) + waveSize * (vertical ? 1 : 2) - 1)) / waveSize,
			bend_max(0, (((q & 2) ? biased_bounds[3] : -biased_bounds[1]) + waveSize * (vertical ? 2 : 1) - 1)) / waveSize,
		};

		if ((bounds[2] - bounds[0]) > 0 && (bounds[3] - bounds[1]) > 0)
		{
			int bias_x = (q == 2 || q == 3) ? 1 : 0;
			int bias_y = (q == 1 || q == 3) ? 1 : 0;

			DispatchData& disp = result.dispatch[result.dispatchCount++];

			disp.waveCount[0] = waveSize; // 64
			disp.waveCount[1] = bounds[2] - bounds[0];
			disp.waveCount[2] = bounds[3] - bounds[1];
			disp.waveOffset[0] = ((q & 1) ? bounds[0] : -bounds[2]) + bias_x;
			disp.waveOffset[1] = ((q & 2) ? -bounds[3] : bounds[1]) + bias_y;

			// We want the far corner of this quadrant relative to the light,
			// as we need to know where the diagonal light ray intersects with the edge of the bounds
			int axis_delta = +biased_bounds[0] - biased_bounds[1];
			if (q == 1) axis_delta = +biased_bounds[2] + biased_bounds[1];
			if (q == 2) axis_delta = -biased_bounds[0] - biased_bounds[3];
			if (q == 3) axis_delta = -biased_bounds[2] + biased_bounds[3];

			axis_delta = (axis_delta + waveSize - 1) / waveSize;

			if (axis_delta > 0)
			{
				DispatchData& disp2 = result.dispatch[result.dispatchCount++];

				// Take copy of current volume
				disp2 = disp;

				if (q == 0)
				{
					// Split on Y, split becomes -1 larger on x
					disp2.waveCount[2] = bend_min(disp.waveCount[2], axis_delta);
					disp.waveCount[2] -= disp2.waveCount[2];
					disp2.waveOffset[1] = disp.waveOffset[1] + disp.waveCount[2];
					disp2.waveOffset[0]--;
					disp2.waveCount[1]++;
				}
				if (q == 1)
				{
					// Split on X, split becomes +1 larger on y
					disp2.waveCount[1] = bend_min(disp.waveCount[1], axis_delta);
					disp.waveCount[1] -= disp2.waveCount[1];
					disp2.waveOffset[0] = disp.waveOffset[0] + disp.waveCount[1];
					disp2.waveCount[2]++;
				}
				if (q == 2)
				{
					// Split on X, split becomes -1 larger on y
					disp2.waveCount[1] = bend_min(disp.waveCount[1], axis_delta);
					disp.waveCount[1] -= disp2.waveCount[1];
					disp.waveOffset[0] += disp2.waveCount[1];
					disp2.waveCount[2]++;
					disp2.waveOffset[1]--;
				}
				if (q == 3)
				{
					// Split on Y, split becomes +1 larger on x
					disp2.waveCount[2] = bend_min(disp.waveCount[2], axis_delta);
					disp.waveCount[2] -= disp2.waveCount[2];
					disp.waveOffset[1] += disp2.waveCount[2];
					disp2.waveCount[1]++;
				}

				// Remove if too small
				if (disp2.waveCount[1] <= 0 || disp2.waveCount[2] <= 0)
				{
					disp2 = result.dispatch[--result.dispatchCount];
				}
				if (disp.waveCount[1] <= 0 || disp.waveCount[2] <= 0)
				{
					disp = result.dispatch[--result.dispatchCount];
				}
			}
		}
	}

	// Scale the shader values by the wave count, the shader expects this
	for (int i = 0; i < result.dispatchCount; i++)
	{
		result.dispatch[i].waveOffset[0] *= waveSize;
		result.dispatch[i].waveOffset[1] *= waveSize;
	}

	m_dispatchList = std::move(result);
}
