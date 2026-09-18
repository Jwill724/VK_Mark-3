#pragma once

#include "ResourceTypes.h"
#include "renderer/frame/FrameResources.h"

#include "LightUnits.h"

#include "renderer/RendererDefinitions.h"
namespace RD = RendererDefinitions;

// TODO: Redesign all this

// TODO: work on this further
struct Flashlight final : public LocalLight
{
	glm::mat4 ViewProj;
	bool IsFlashLightActive() const { return m_flashlightEnabled || m_flashlightFlagsChanged; }
	bool IsFlashLightOn() const { return m_flashlightEnabled; }
	bool AreFlagsToggled() const { return m_flashlightFlagsChanged; }
	constexpr void ToggleFlashLight() noexcept
	{
		m_flashlightEnabled = !m_flashlightEnabled;

		m_flashlightFlagsChanged = true;
		if (m_flashlightEnabled)
		{
			flags |= RD::LIGHT_FLAG_FLASHLIGHT;
			flags &= ~RD::LIGHT_FLAG_FLASHLIGHT_OFF;
			flags |= RD::LIGHT_FLAG_CASTS_SPOT_SHADOW;
		}
		else
		{
			flags &= ~RD::LIGHT_FLAG_FLASHLIGHT;
			flags |= RD::LIGHT_FLAG_FLASHLIGHT_OFF;
			flags &= ~RD::LIGHT_FLAG_CASTS_SPOT_SHADOW;
		}
	}

	void Init(uint32_t shadowMapID, uint32_t cookieGoboID);

	constexpr void InitFlags() noexcept
	{
		flags &= ~RD::LIGHT_FLAG_FLASHLIGHT;
		flags |= RD::LIGHT_FLAG_FLASHLIGHT_OFF;
		flags &= ~RD::LIGHT_FLAG_CASTS_SPOT_SHADOW;
	}

	// position and direction based off camera
	bool UpdateFlashLight(
		std::vector<LocalLight>& globalLightList,
		const glm::vec3& pos,
		const glm::vec3& dir,
		const float dt,
		const glm::vec2 mouseDelta,
		const glm::vec3 camForward);

// Should be private members
	glm::vec3 m_smoothedDir{0.0f, 0.0f, -1.0f};
	glm::vec3 m_smoothedPos{0.0f};

	glm::vec3 m_dirVelocity{0.0f}; // for spring
	glm::vec3 m_posVelocity{0.0f};

	float m_lagStrength = 40.0f;   // responsiveness
	float m_swayStrength = 0.025f; // camera motion

	bool m_flashlightFlagsChanged = false;
	bool m_flashlightEnabled = false;

	bool m_bTextureIDsInitialized = false;
	bool m_bLightStateUpdated = false;

	uint32_t m_shadowMapID = UINT32_MAX; // bindless image array indices
	uint32_t m_cookieGoboID = UINT32_MAX;
};

struct SceneLightDesc;

namespace LightingSystem
{
	struct PrevLightState
	{
		glm::vec3 position{ 0.0f };
		glm::vec3 direction{ 0.0f, 0.0f, -1.0f };
		float     intensity = 0.0f;
		uint32_t  flags = 0u;
		bool      valid = false;
	};

	inline std::vector<PrevLightState> _prevLightState;
	inline bool  _changeRatesActive = false;
	inline float _reactiveGain = 25.0f;

	bool UpdateLightChangeRates();

	inline struct alignas(16) FlashLightSettings
	{
		float offsetRight = 0.07f;
		float offsetDown = -0.12f;
		float offsetFwd = 0.07f;
		float fovYScale = 1.5f;

		float nearProj = 0.1f;
		float farProjScale = 2.0f;
		float shadowBias = 0.0001f;
		float radiusTexels = 1.0f;

		float radius = 5.0f;
		float outerDeg = 38.0f;
		float innerDeg = 22.0f;
		float lumens = LightUnits::LM_FLASHLIGHT;

		float sourceRadius = 0.04f;
	} _flashlightSettings{};

	inline struct LightIDTable {
		std::vector<uint32_t> activeLightIDs;

		std::vector<uint32_t> idToIndex;
		std::vector<uint32_t> newCopiedIDs;
		std::vector<uint32_t> cleanupIDs;
		uint32_t newIDCount = 0u;

		std::vector<uint32_t> freeIDs;
		std::vector<uint8_t> alive;

		std::vector<uint32_t> highlightedIDs;

		void Clear()
		{
			activeLightIDs.clear();
			newCopiedIDs.clear();
			cleanupIDs.clear();
			idToIndex.clear();
			freeIDs.clear();
			alive.clear();
			highlightedIDs.clear();

			newIDCount = 0u;
		}
	} _lightIDTable;

	extern bool _dynamicLightsEnabled;

	const uint32_t& GetActiveLightCount();
	const uint32_t& GetLightBufferCount();

	uint32_t AddSceneLight(const SceneLightDesc& desc);
	void     RemoveSceneLight(uint32_t lightID);

	extern std::vector<LocalLight> _globalLightList;

	void SetTargetActiveLightCount(uint32_t targetCount);
	bool UpdateLightList();
	bool UpdateDynamicLightsOrbit(float deltaTime);

	void ResolveLightUnits(float adaptedEV100);

	extern Flashlight _mainFlashLight;

	void Init();
	void Cleanup();

	inline uint32_t getDynamicLightBeginIndex()
	{
		return RD::LIGHT_LIST_STATIC_COUNT;
	}

	inline bool isDynamicLightID(uint32_t lightID) noexcept
	{
		return lightID >= RD::LIGHT_LIST_STATIC_COUNT;
	}
}
