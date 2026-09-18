#pragma once

#include <glm/glm.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <type_traits>

// Transport uses kilometers and inverse kilometers. No exposure in these values.
struct alignas(16) AtmosphereParameters
{
	glm::vec4 rayleigh{ 0.005802f, 0.013558f, 0.033100f, 8.0f }; // RGB extinction, scale height
	glm::vec4 mie{ 0.004440f, 0.004440f, 0.004440f, 1.2f };     // RGB extinction, scale height
	glm::vec4 absorption{ 0.000650f, 0.001881f, 0.000085f, 1.0f }; // RGB extinction, density multiplier
	glm::vec4 geometry{ 6360.0f, 100.0f, 25.0f, 15.0f }; // bottom radius, thickness, absorption center/half-width
	glm::uvec4 integration{ 256u, 0u, 0u, 0u }; // midpoint samples; padding
};

struct alignas(16) AtmosphereResources
{
	glm::uvec4 textureIDs{ UINT32_MAX }; // x transmittance; y skyView, z lighting atlas  w unused.
	glm::uvec4 transmittanceExtent{ 512u, 512u, 0u, 0u };
};

struct alignas(16) AtmosphereDebugPush
{
	AtmosphereParameters parameters{};
	glm::vec4 display{ 1.0f, 0.0f, 0.0f, 0.0f }; // optical-depth scale; padding
	glm::uvec4 mode{ 1u, 0u, 0u, 0u }; // 1 RGB transmittance; 2 optical depth
};

struct AtmosphereSettings
{
	AtmosphereParameters parameters{};
	uint32_t debugMode = 0u;
	float opticalDepthScale = 0.1f;
};

inline void SanitizeAtmosphere(AtmosphereParameters& p)
{
	p.rayleigh = glm::max(p.rayleigh, glm::vec4(0.0f));
	p.mie = glm::max(p.mie, glm::vec4(0.0f));
	p.absorption = glm::max(p.absorption, glm::vec4(0.0f));
	p.rayleigh.w = std::max(p.rayleigh.w, 0.1f);
	p.mie.w = std::max(p.mie.w, 0.1f);
	p.geometry.x = std::max(p.geometry.x, 100.0f);
	p.geometry.y = std::max(p.geometry.y, 1.0f);
	p.geometry.z = std::clamp(p.geometry.z, 0.0f, p.geometry.y);
	p.geometry.w = std::max(p.geometry.w, 0.1f);
	p.integration.x = std::clamp(p.integration.x, 32u, 1024u);
	p.integration.y = p.integration.z = p.integration.w = 0u;
}

static_assert(std::is_standard_layout_v<AtmosphereParameters>);
static_assert(sizeof(AtmosphereParameters) == 80);
static_assert(offsetof(AtmosphereParameters, geometry) == 48);
static_assert(offsetof(AtmosphereParameters, integration) == 64);
static_assert(sizeof(AtmosphereResources) == 32);
static_assert(offsetof(AtmosphereResources, transmittanceExtent) == 16);
static_assert(sizeof(AtmosphereDebugPush) == 112);
static_assert(offsetof(AtmosphereDebugPush, display) == 80);
static_assert(offsetof(AtmosphereDebugPush, mode) == 96);

struct AtmosphereSkySettings
{
	glm::vec3 seaLevelOrigin{ 0.0f };
	float worldToKm = 0.001f;
	float mieAlbedo = 0.9f;
	float mieG = 0.8f;
	float sunAngularRadiusDeg = 0.2666f;
	float solarIntensityScale = 1.0f;
	uint32_t viewSamples = 64u;
	glm::vec4 ground{ 0.15f, 0.15f, 0.15f, 0.0f };
};

struct alignas(16) AtmosphereSkyPush
{
	AtmosphereParameters parameters{};
	glm::vec4 placement{ 0.0f, 0.0f, 0.0f, 0.001f };
	glm::vec4 scattering{ 0.9f, 0.8f, 0.0f, 0.0f };
	glm::vec4 sun{ 0.004653f, 1.0f, 0.0f, 0.0f }; // angular radius radians, illuminance scale
	glm::uvec4 options{ 64u, 0u, 0u, 0u };
};

inline AtmosphereSkyPush MakeAtmosphereSkyPush(
	const AtmosphereParameters& parameters, const AtmosphereSkySettings& settings)
{
	AtmosphereSkyPush p{};
	p.parameters = parameters;
	p.placement = glm::vec4(settings.seaLevelOrigin, std::max(settings.worldToKm, 1e-8f));
	p.scattering.x = std::clamp(settings.mieAlbedo, 0.0f, 1.0f);
	p.scattering.y = std::clamp(settings.mieG, -0.95f, 0.95f);
	p.sun.x = std::clamp(settings.sunAngularRadiusDeg, 0.05f, 2.0f) * (3.14159265359f / 180.0f);
	p.sun.y = std::max(settings.solarIntensityScale, 0.0f);
	p.options.x = std::clamp(settings.viewSamples, 16u, 256u);
	return p;
}

static_assert(sizeof(AtmosphereSkyPush) == 144);
static_assert(offsetof(AtmosphereSkyPush, placement) == 80);
static_assert(offsetof(AtmosphereSkyPush, scattering) == 96);
static_assert(offsetof(AtmosphereSkyPush, sun) == 112);
static_assert(offsetof(AtmosphereSkyPush, options) == 128);