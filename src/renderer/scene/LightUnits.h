#pragma once

#include <algorithm>
#include <cmath>

namespace LightUnits
{
	inline constexpr float SUN_NOON_CLEAR = 100000.0f;
	inline constexpr float SUN_HAZY = 50000.0f;
	inline constexpr float SUN_LOW_GOLDEN = 20000.0f;
	inline constexpr float SKY_OVERCAST = 20000.0f;
	inline constexpr float MOON_FULL = 0.25f;

	inline constexpr float SKY_CLEAR_DAY = 8000.0f;
	inline constexpr float SKY_OVERCAST_DAY = 2000.0f;
	inline constexpr float SKY_TWILIGHT = 10.0f;
	inline constexpr float SKY_NIGHT = 0.001f;

	inline constexpr float EM_PHONE_SCREEN = 400.0f;
	inline constexpr float EM_MONITOR = 250.0f;
	inline constexpr float EM_TV_HDR_HIGHLIGHT = 1000.0f;
	inline constexpr float EM_FLUORESCENT_TUBE = 8000.0f;
	inline constexpr float EM_INCANDESCENT = 60000.0f;
	inline constexpr float EM_LED_INDICATOR = 2000.0f;
	inline constexpr float EM_FIRE_FLAME = 15000.0f;
	inline constexpr float EM_NEON_SIGN = 5000.0f;

	inline constexpr float LM_CANDLE = 12.0f;
	inline constexpr float LM_BULB_40W = 450.0f;
	inline constexpr float LM_BULB_60W = 800.0f;
	inline constexpr float LM_BULB_100W = 1600.0f;
	inline constexpr float LM_CEILING_FIXTURE = 3000.0f;
	inline constexpr float LM_HEADLIGHT = 1200.0f;
	inline constexpr float LM_FLASHLIGHT = 3000.0f;
	inline constexpr float LM_STREETLAMP = 12000.0f;

	inline constexpr float EV_SUNNY_16 = 15.0f;
	inline constexpr float EV_OVERCAST = 12.0f;
	inline constexpr float EV_INDOOR_BRIGHT = 7.0f;
	inline constexpr float EV_INDOOR_DIM = 5.0f;
	inline constexpr float EV_NIGHT_STREET = 1.0f;

	inline constexpr float EV_MIN = -4.0f;
	inline constexpr float EV_MAX = 17.0f;
	inline constexpr float EV_SEED = EV_SUNNY_16;

	inline constexpr float EV_MIDDLE_GREY = 1.2f;

	inline float ExposureFromEV100(float ev100)
	{
		return 1.0f / (EV_MIDDLE_GREY * std::exp2(ev100));
	}

	inline constexpr float VISIBILITY_FRACTION = 0.002f;
	inline constexpr float REFERENCE_ALBEDO = 0.5f;
	inline constexpr float CUTOFF_FLOOR_LUX = 0.5f;

	// RADIUS_SCALE trades cluster occupancy (~r^3) against tail clipping.
	// Lower until light boundaries visibly pop, then back off.
	inline constexpr float RADIUS_SCALE = 0.25f;
	inline constexpr float MIN_LIGHT_RADIUS = 0.1f;
	inline constexpr float MAX_LIGHT_RADIUS = 25.0f;

	inline float CutoffIlluminance(float ev100)
	{
		const float adaptive = VISIBILITY_FRACTION
			* (3.14159265f / REFERENCE_ALBEDO)
			* EV_MIDDLE_GREY
			* std::exp2(ev100);

		return std::max(adaptive, CUTOFF_FLOOR_LUX);
	}

	inline float LightRadiusFromIntensity(float intensity, float ev100)
	{
		const float reach = std::sqrt(intensity / CutoffIlluminance(ev100)) * RADIUS_SCALE;
		return std::clamp(reach, MIN_LIGHT_RADIUS, MAX_LIGHT_RADIUS);
	}
}