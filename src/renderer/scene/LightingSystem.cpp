#include "pch.h"

#include "LightingSystem.h"
#include "../../core/asset/AssetUploadTypes.h"

namespace LightingSystem
{
	Flashlight _mainFlashLight;
	bool _dynamicLightsEnabled = false;

	std::vector<LocalLight> _globalLightList;
	uint32_t _activeLightCount = 0u;
	uint32_t _lightBufferCount = 0u;
	const uint32_t& GetActiveLightCount() { return _activeLightCount; }
	const uint32_t& GetLightBufferCount() { return _lightBufferCount; }

	static float LumensToIntensity(const LocalLight& l)
	{
		if ((l.flags & RD::LIGHT_FLAG_SPOT) == 0u)
			return l.lumens * (1.0f / (4.0f * glm::pi<float>()));

		const float cosOuter = glm::clamp(l.outerCos, -1.0f, 1.0f);
		const float solidAngle = 2.0f * glm::pi<float>() * (1.0f - cosOuter);

		return l.lumens / std::max(solidAngle, 1e-3f);
	}

	void ResolveLightUnits(float adaptedEV100)
	{
		const float quantizedEV = std::round(adaptedEV100 * 2.0f) * 0.5f;

		for (size_t i = 0; i < _globalLightList.size(); ++i)
		{
			LocalLight& light = _globalLightList[i];

			light.intensity = LumensToIntensity(light);

			if (i == RD::LIGHT_LIST_SLOT_FLASHLIGHT) continue;

			light.radius = LightUnits::LightRadiusFromIntensity(light.intensity, quantizedEV);
		}
	}

	static bool isLightIDAlive(uint32_t lightID) {
		if (lightID >= _lightIDTable.alive.size()) return false;
		return _lightIDTable.alive[lightID] != 0;
	}

	static uint32_t allocateLightID() {
		if (!_lightIDTable.freeIDs.empty()) {
			uint32_t reusedID = _lightIDTable.freeIDs.back();
			_lightIDTable.freeIDs.pop_back();

			ASSERT(reusedID < _lightIDTable.alive.size());
			ASSERT(reusedID < _lightIDTable.idToIndex.size());

			_lightIDTable.alive[reusedID] = 1u;
			_lightIDTable.idToIndex[reusedID] = UINT32_MAX;
			return reusedID;
		}

		uint32_t newID = static_cast<uint32_t>(_lightIDTable.alive.size());

		_lightIDTable.alive.push_back(1u);
		_lightIDTable.idToIndex.push_back(UINT32_MAX);

		return newID;
	}

	static void activateLight(LocalLight&& newLight, uint32_t lightID) {
		ASSERT(isDynamicLightID(lightID));
		ASSERT(lightID < _lightIDTable.idToIndex.size());
		ASSERT(_lightIDTable.idToIndex[lightID] == UINT32_MAX);

		uint32_t denseIndex = static_cast<uint32_t>(_globalLightList.size());

		_globalLightList.push_back(std::move(newLight));
		_lightIDTable.activeLightIDs.push_back(lightID);
		_lightIDTable.idToIndex[lightID] = denseIndex;
	}

	static uint32_t findActiveLightIDIndex(uint32_t lightID) {
		for (uint32_t activeIndex = 0; activeIndex < static_cast<uint32_t>(_lightIDTable.activeLightIDs.size()); ++activeIndex) {
			if (_lightIDTable.activeLightIDs[activeIndex] == lightID) {
				return activeIndex;
			}
		}

		return UINT32_MAX;
	}

	static LocalLight* getDynamicLightByID(uint32_t lightID) {
		if (!isDynamicLightID(lightID)) return nullptr;
		if (!isLightIDAlive(lightID)) return nullptr;

		uint32_t denseIndex = _lightIDTable.idToIndex[lightID];
		if (denseIndex == UINT32_MAX) return nullptr;
		if (denseIndex >= _globalLightList.size()) return nullptr;

		return &_globalLightList[denseIndex];
	}

	void createDefaultLight() {
		uint32_t newID = allocateLightID();

		LocalLight defaultLight{};
		defaultLight.flags |= RD::LIGHT_FLAG_POINT;
		defaultLight.color = glm::vec3(1.0f);
		defaultLight.position = glm::vec3(0.0f);
		defaultLight.lumens = LightUnits::LM_BULB_60W;

		activateLight(std::move(defaultLight), newID);
	}

	void createRandomLight() {
		uint32_t newID = allocateLightID();

		LocalLight randomLight{};

		const float rand01 = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
		const float rand02 = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
		const float rand03 = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
		//const float rand04 = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
		const float rand06 = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
		const float rand07 = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
		const float rand08 = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);

		const float posRange = 5.0f;
		randomLight.position = glm::vec3(
			(rand01 * 2.0f - 1.0f) * (posRange * 2.5f),
			(rand02 * 2.0f - 1.0f) * (posRange * 1.5f),
			(rand03 * 2.0f - 1.0f) * (posRange * 1.5f)
		);

		static constexpr float kFixtures[] = {
			LightUnits::LM_CANDLE,
			LightUnits::LM_BULB_40W,
			LightUnits::LM_BULB_60W,
			LightUnits::LM_BULB_100W,
			LightUnits::LM_CEILING_FIXTURE,
		};

		const uint32_t pick = static_cast<uint32_t>(rand()) % std::size(kFixtures);
		randomLight.lumens = kFixtures[pick];

		randomLight.flags |= RD::LIGHT_FLAG_POINT;

		const float rand09 = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
		randomLight.sourceRadius = glm::mix(0.03f, 0.30f, rand09 * rand09);

		const float minColor = 0.1f;
		const float maxColor = 1.0f;
		glm::vec3 colorA = glm::clamp(
			glm::vec3(rand06, rand07, rand08), glm::vec3(minColor), glm::vec3(maxColor));

		const float lum = 0.2126f * colorA.r + 0.7152f * colorA.g + 0.0722f * colorA.b;
		randomLight.color = colorA / std::max(lum, 1e-4f);

		activateLight(std::move(randomLight), newID);
	}

	void destroyLight(uint32_t lightID) {
		if (!isDynamicLightID(lightID)) {
			fmt::print("[LightingSystem] Refusing to destroy static light id={}\n", lightID);
			return;
		}

		if (!isLightIDAlive(lightID)) return;

		ASSERT(lightID < _lightIDTable.idToIndex.size());

		uint32_t removedDenseIndex = _lightIDTable.idToIndex[lightID];
		if (removedDenseIndex == UINT32_MAX) return;

		ASSERT(removedDenseIndex >= RD::LIGHT_LIST_STATIC_COUNT);
		ASSERT(removedDenseIndex < _globalLightList.size());

		uint32_t lastDenseIndex = static_cast<uint32_t>(_globalLightList.size() - 1u);

		uint32_t removedActiveIndex = findActiveLightIDIndex(lightID);
		ASSERT(removedActiveIndex != UINT32_MAX);

		if (removedDenseIndex != lastDenseIndex) {
			LocalLight& movedLight = _globalLightList[lastDenseIndex];

			uint32_t movedLightID = UINT32_MAX;
			for (uint32_t activeID : _lightIDTable.activeLightIDs) {
				if (_lightIDTable.idToIndex[activeID] == lastDenseIndex) {
					movedLightID = activeID;
					break;
				}
			}

			ASSERT(movedLightID != UINT32_MAX);

			_globalLightList[removedDenseIndex] = std::move(movedLight);
			_lightIDTable.idToIndex[movedLightID] = removedDenseIndex;
		}

		_globalLightList.pop_back();

		uint32_t lastActiveIndex = static_cast<uint32_t>(_lightIDTable.activeLightIDs.size() - 1u);
		if (removedActiveIndex != lastActiveIndex) {
			_lightIDTable.activeLightIDs[removedActiveIndex] = _lightIDTable.activeLightIDs[lastActiveIndex];
		}
		_lightIDTable.activeLightIDs.pop_back();

		_lightIDTable.idToIndex[lightID] = UINT32_MAX;
		_lightIDTable.alive[lightID] = 0u;
		_lightIDTable.freeIDs.push_back(lightID);
	}

	static float lightChangeRate(const LocalLight& curr, const LightingSystem::PrevLightState& prev)
	{
		if (!prev.valid) return 1.0f;
		if ((curr.flags & RD::LIGHT_FLAG_MASK_ONLY) != 0u) return 1.0f;

		const bool wasOff = (prev.flags & RD::LIGHT_FLAG_FLASHLIGHT_OFF) != 0u;
		const bool isOff = (curr.flags & RD::LIGHT_FLAG_FLASHLIGHT_OFF) != 0u;
		if (wasOff != isOff) return 1.0f;

		float dirDelta = 0.0f;
		if ((curr.flags & RD::LIGHT_FLAG_SPOT) != 0u)
		{
			const float cosDelta = glm::clamp(glm::dot(curr.direction, prev.direction), -1.0f, 1.0f);
			const float coneHalf = std::max(std::acos(glm::clamp(curr.outerCos, -1.0f, 1.0f)), 0.05f);
			dirDelta = std::acos(cosDelta) / coneHalf;
		}

		const float posDelta = glm::length(curr.position - prev.position) / std::max(curr.radius, 1e-3f);
		const float intDelta = std::abs(curr.intensity - prev.intensity) / std::max(curr.intensity, 1e-3f);

		return glm::clamp((dirDelta + posDelta + intDelta) * LightingSystem::_reactiveGain, 0.0f, 1.0f);
	}
}

void LightingSystem::Init()
{
	_globalLightList.clear();
	_globalLightList.reserve(RD::MAX_LIGHTS);
	_globalLightList.resize(RD::LIGHT_LIST_STATIC_COUNT);

	_mainFlashLight.InitFlags();
	_globalLightList[RD::LIGHT_LIST_SLOT_FLASHLIGHT] = _mainFlashLight;

	_lightIDTable.activeLightIDs.clear();
	_lightIDTable.freeIDs.clear();
	_lightIDTable.alive.clear();
	_lightIDTable.idToIndex.clear();

	_lightIDTable.highlightedIDs.clear();
	_lightIDTable.cleanupIDs.clear();
	_lightIDTable.newCopiedIDs.clear();

	_lightIDTable.activeLightIDs.reserve(RD::MAX_LIGHTS);
	_lightIDTable.freeIDs.reserve(RD::MAX_LIGHTS);
	_lightIDTable.alive.reserve(RD::MAX_LIGHTS);
	_lightIDTable.idToIndex.reserve(RD::MAX_LIGHTS);

	_lightIDTable.highlightedIDs.reserve(RD::MAX_LIGHTS);
	_lightIDTable.cleanupIDs.reserve(RD::MAX_LIGHTS);
	_lightIDTable.newCopiedIDs.reserve(RD::MAX_LIGHTS);

	// Static slots are always "alive" and mapped to themselves.
	_lightIDTable.alive.resize(RD::LIGHT_LIST_STATIC_COUNT, 1u);
	_lightIDTable.idToIndex.resize(RD::LIGHT_LIST_STATIC_COUNT);

	for (uint32_t staticIndex = 0; staticIndex < RD::LIGHT_LIST_STATIC_COUNT; ++staticIndex) {
		_lightIDTable.idToIndex[staticIndex] = staticIndex;
	}
}

uint32_t LightingSystem::AddSceneLight(const SceneLightDesc& desc)
{
	uint32_t typeFlag = 0u;
	switch (desc.type)
	{
	case 0u: typeFlag = RD::LIGHT_FLAG_POINT; break;
	case 1u: typeFlag = RD::LIGHT_FLAG_SPOT;  break;
	default: return UINT32_MAX;
	}

	if (_globalLightList.size() >= RD::MAX_LIGHTS)
	{
		fmt::println("[LightingSystem] Scene light dropped: at MAX_LIGHTS ({})", RD::MAX_LIGHTS);
		return UINT32_MAX;
	}

	LocalLight light{};
	light.flags |= typeFlag;
	light.position = desc.position;
	light.direction = desc.direction;
	light.color = desc.color;
	light.innerCos = desc.innerCos;
	light.outerCos = desc.outerCos;
	light.sourceRadius = 0.0f;
	light.sourceLength = 0.0f;

	light.lumens = (typeFlag == RD::LIGHT_FLAG_SPOT)
		? desc.intensity * (2.0f * glm::pi<float>() * (1.0f - glm::clamp(light.outerCos, -1.0f, 1.0f)))
		: desc.intensity * (4.0f * glm::pi<float>());

	const uint32_t id = allocateLightID();
	activateLight(std::move(light), id);
	return id;
}

void LightingSystem::RemoveSceneLight(uint32_t lightID)
{
	if (lightID == UINT32_MAX) return;
	destroyLight(lightID);
}

void LightingSystem::SetTargetActiveLightCount(uint32_t targetCount) {
	uint32_t currentCount = static_cast<uint32_t>(_lightIDTable.activeLightIDs.size());

	if (targetCount == currentCount) return;

	if (targetCount > currentCount) {
		_lightIDTable.newIDCount += (targetCount - currentCount);
		return;
	}

	uint32_t removeCount = currentCount - targetCount;

	for (uint32_t i = 0; i < removeCount; ++i) {
		uint32_t lastIndex = static_cast<uint32_t>(_lightIDTable.activeLightIDs.size() - 1u - i);
		uint32_t lightID = _lightIDTable.activeLightIDs[lastIndex];
		_lightIDTable.cleanupIDs.push_back(lightID);
	}
}

bool Flashlight::UpdateFlashLight(
	std::vector<LocalLight>& globalLightList,
	const glm::vec3& pos,
	const glm::vec3& dir,
	const float dt,
	const glm::vec2 mouseDelta,
	const glm::vec3 camForward)
{
	ASSERT(m_bTextureIDsInitialized && "Assign Bindless texture Ids to flashlight.");

	glm::vec3 flatForward = glm::normalize(glm::vec3(camForward.x, 0.0f, camForward.z));
	const glm::vec3 upWorld = glm::vec3(0.0f, 1.0f, 0.0f);

	// Handle edge case (looking straight up/down)
	if (glm::length(flatForward) < 0.001f) {
		flatForward = glm::vec3(0.0f, 0.0f, -1.0f);
	}

	glm::vec3 stableRight = glm::normalize(glm::cross(flatForward, upWorld));
	glm::vec3 stableUp = upWorld;

	// Reduce sway when looking up/down
	float verticalFactor = 1.0f - std::abs(camForward.y);
	verticalFactor = glm::smoothstep(0.0f, 1.0f, verticalFactor);

	glm::vec3 swayOffset =
		stableRight * (-mouseDelta.x * m_swayStrength) +
		stableUp    * (-mouseDelta.y * m_swayStrength);

	swayOffset *= verticalFactor;

	glm::vec3 targetDir = glm::normalize(camForward + swayOffset);
	glm::vec3 targetPos = pos;

	// smoothing (exponential)
	float response = 1.0f - std::exp(-m_lagStrength * dt);

	m_smoothedDir = glm::normalize(glm::mix(m_smoothedDir, targetDir, response));
	m_smoothedPos = glm::mix(m_smoothedPos, targetPos, response);

	if (direction != m_smoothedDir || position != m_smoothedPos) {
		direction = m_smoothedDir;
		position = m_smoothedPos;
		m_bLightStateUpdated = true;
	}

	lumens = LightingSystem::_flashlightSettings.lumens;
	sourceRadius = LightingSystem::_flashlightSettings.sourceRadius;

	// Matrix update
	if (m_bLightStateUpdated)
	{
		glm::vec3 fwd = glm::normalize(direction);
		glm::vec3 right = glm::normalize(glm::cross(fwd, upWorld));
		glm::vec3 up = glm::normalize(glm::cross(right, fwd));

		const float offsetRight = LightingSystem::_flashlightSettings.offsetRight;
		const float offsetDown = LightingSystem::_flashlightSettings.offsetDown;
		const float offsetFwd = LightingSystem::_flashlightSettings.offsetFwd;

		glm::vec3 lightPos =
			position +
			right * offsetRight +
			up * offsetDown +
			fwd * offsetFwd;

		glm::mat4 view = glm::lookAt(
			lightPos,
			lightPos + fwd,
			up
		);

		const float fovY = LightingSystem::_flashlightSettings.fovYScale;

		glm::mat4 proj = glm::perspective(
			fovY,
			1.0f,
			0.1f,
			radius
		);

		ViewProj = proj * view;
	}

	// Push to global list
	const bool lightDirty = m_bLightStateUpdated || m_flashlightFlagsChanged;
	if (lightDirty) {
		m_flashlightFlagsChanged = false;
		globalLightList[RD::LIGHT_LIST_SLOT_FLASHLIGHT] = *this;
	}

	m_bLightStateUpdated = false; // Clean slate

	return lightDirty;
}

bool LightingSystem::UpdateLightList()
{
	bool listChanged = false;

	for (uint32_t lightID : _lightIDTable.cleanupIDs) {
		destroyLight(lightID);
		listChanged = true;
	}
	_lightIDTable.cleanupIDs.clear();

	for (uint32_t sourceID : _lightIDTable.newCopiedIDs) {
		if (_globalLightList.size() >= RD::MAX_LIGHTS) {
			fmt::println("[LightingSystem::UpdateLightList] copy break: listSize={} max={}",
				_globalLightList.size(),
				RD::MAX_LIGHTS
			);
			break;
		}

		if (!isLightIDAlive(sourceID)) {
			fmt::println("[LightingSystem::UpdateLightList] copy skip: sourceID={} not alive", sourceID);
			continue;
		}

		LocalLight* sourceLight = getDynamicLightByID(sourceID);
		if (sourceLight == nullptr) {
			fmt::println("[LightingSystem::UpdateLightList] copy skip: sourceID={} has no mapped dense light", sourceID);
			continue;
		}

		uint32_t newID = allocateLightID();

		LocalLight copiedLight = *sourceLight;
		activateLight(std::move(copiedLight), newID);

		listChanged = true;
	}
	_lightIDTable.newCopiedIDs.clear();

	uint32_t createCount = _lightIDTable.newIDCount;

	while (createCount > 0 && _globalLightList.size() < RD::MAX_LIGHTS) {
		createRandomLight();
		--createCount;
		listChanged = true;
	}

	_lightIDTable.newIDCount = 0u;

	_lightBufferCount = static_cast<uint32_t>(_globalLightList.size());

	_activeLightCount = static_cast<uint32_t>(_lightIDTable.activeLightIDs.size());
	if (_mainFlashLight.IsFlashLightOn()) {
		++_activeLightCount;
	}

	ASSERT(_lightBufferCount <= RD::MAX_LIGHTS);

	return listChanged;
}

bool LightingSystem::UpdateLightChangeRates()
{
	if (_prevLightState.size() < _lightIDTable.idToIndex.size())
		_prevLightState.resize(_lightIDTable.idToIndex.size());

	bool anyActive = false;

	for (uint32_t lightID = 0; lightID < _lightIDTable.idToIndex.size(); ++lightID)
	{
		if (!isLightIDAlive(lightID)) continue;

		const uint32_t denseIndex = _lightIDTable.idToIndex[lightID];
		if (denseIndex == UINT32_MAX || denseIndex >= _globalLightList.size()) continue;

		LocalLight& light = _globalLightList[denseIndex];
		PrevLightState& prev = _prevLightState[lightID];

		light.changeRate = lightChangeRate(light, prev);
		anyActive = anyActive || (light.changeRate > 0.0f);

		prev.position = light.position;
		prev.direction = light.direction;
		prev.intensity = light.intensity;
		prev.flags = light.flags;
		prev.valid = true;
	}

	const bool needsUpload = anyActive || _changeRatesActive;
	_changeRatesActive = anyActive;
	return needsUpload;
}

static void rotateLightAroundOriginXZ(
	glm::vec3& position,
	float angularSpeedRad,
	float deltaTime)
{
	const float angle = angularSpeedRad * deltaTime;

	const float cosA = std::cos(angle);
	const float sinA = std::sin(angle);

	glm::vec3 p = position;

	glm::vec3 rotated;
	rotated.x = p.x * cosA - p.z * sinA;
	rotated.z = p.x * sinA + p.z * cosA;
	rotated.y = p.y;

	position = rotated;
}

bool LightingSystem::UpdateDynamicLightsOrbit(float deltaTime) {
	if (_lightIDTable.activeLightIDs.empty()) return false;

	constexpr float baseSpeed = glm::radians(0.8f);

	uint32_t index = 0;
	for (uint32_t lightID : _lightIDTable.activeLightIDs) {
		LocalLight* light = getDynamicLightByID(lightID);
		if (light == nullptr) {
			++index;
			continue;
		}

		float speed = baseSpeed * (0.5f + 0.05f * index);

		rotateLightAroundOriginXZ(
			light->position,
			speed,
			deltaTime
		);

		++index;
	}

	return true;
}

void LightingSystem::Cleanup() {
	_lightIDTable.Clear();
	_activeLightCount = 0u;
	_lightBufferCount = 0u;
	_globalLightList.clear();

	_prevLightState.clear();
}

void Flashlight::Init(uint32_t shadowMapID, uint32_t cookieGoboID)
{
	m_shadowMapID = shadowMapID;
	m_cookieGoboID = cookieGoboID;
	flags |= RD::LIGHT_FLAG_SPOT;
	radius = LightingSystem::_flashlightSettings.radius;

	lumens = LightingSystem::_flashlightSettings.lumens;
	sourceRadius = LightingSystem::_flashlightSettings.sourceRadius;

	outerCos = std::cos(glm::radians(LightingSystem::_flashlightSettings.outerDeg));
	innerCos = std::cos(glm::radians(LightingSystem::_flashlightSettings.innerDeg));

	m_bTextureIDsInitialized = true;
	m_bLightStateUpdated = true;
}
