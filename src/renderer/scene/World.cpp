#include "pch.h"

#include "World.h"
#include "Scene.h"
#include "LightingSystem.h"
#include "../RendererDefinitions.h"
#include "../../input/UserInput.h"
#include "../backend/memory/BindlessImageTable.h"
#include "../frame/FrameContext.h"
#include "../../profiler/Profiler.h"
#include "../../core/asset/AssetUploadTypes.h"

namespace RD = RendererDefinitions;

static constexpr glm::vec3 DEFAULT_SPAWN { 1.0, 1.0, 1.0 };

namespace World
{
	struct SceneProfileEntry
	{
		RD::InstancingMethod drawType = RD::InstancingMethod::DrawStatic;
		uint32_t instanceCount = 1;
	};

	// TODO: Add more conditionals to cover counts, theres no safety
	std::unordered_map<ModelID, SceneProfileEntry> _sceneProfiles
	{
		{ ModelID::Sponza,              {} },
		{ ModelID::SponzaIntelMain,     {} },
		{ ModelID::SponzaIntelCurtains, {} },
		{ ModelID::SponzaIntelIvy,      {} },
		{ ModelID::SponzaIntelTree,     {} },
		{ ModelID::SanMiguel,         {} },
		{ ModelID::Bistro,            {} },
		{ ModelID::BistroExt,         {} },
		{ ModelID::MRSpheres,         {} },
		{ ModelID::Duck,              { RD::InstancingMethod::DrawMultiStatic, 200000 } },
		{ ModelID::DamagedHelmet,     { RD::InstancingMethod::DrawStatic, 1 } },
		{ ModelID::DragonAttenuation, {} },
		{ ModelID::City,              {} },
		{ ModelID::Structure,         {} },
		{ ModelID::WrathDragon,       {} },
		{ ModelID::Mech,              {} },
		{ ModelID::YellowMech,        {} },
		{ ModelID::Mini,              {} },

		//{ ModelID::DarkRoom,          {} },
		//{ ModelID::CornellBox,        {} },
		//{ ModelID::BreakfastRoom,     {} },
		//{ ModelID::FireplaceRoom,     {} },
		//{ ModelID::Conference,        {} },
		//{ ModelID::Sibenik,           {} },
		//{ ModelID::MandarinOrange,           {} },
		//{ ModelID::CandleHolder,           {} },
		//{ ModelID::TransmissionTest,           {} },
		//{ ModelID::CompareClearCoat,           {} },
		//{ ModelID::MosquitoInAmber,           {} },
	};

	//std::vector<AsteroidState> _asteroidStates;
	//uint32_t                   _asteroidFieldVI = UINT32_MAX;

	//constexpr uint32_t  ASTEROID_COUNT   = 512u;
	//constexpr glm::vec3 FIELD_CENTER     = { 0.0f, 45.0f, -30.0f };
	//constexpr glm::vec3 FIELD_EXTENTS    = { 60.0f, 12.0f, 60.0f };
	//constexpr float     SCALE_MIN        = 0.15f;
	//constexpr float     SCALE_MAX        = 1.4f;
	//constexpr uint32_t  FIELD_SEED       = 0xA57E401Du;

	Scene _scene;
	Scene& GetScene() { return _scene; }

	InstanceState _instanceState;
	InstanceState& GetInstanceState() { return _instanceState; }

	bool _bIsLastFlashlightActive = false;
	bool _bIsFlashlightDirtyAllFrames = false;
	uint32_t _lightStateVersion = 0u;

	bool _bAreAssetsLoaded = false;

	bool SyncGlobalInstancesAndTransforms(
		std::unordered_map<ModelID, SceneProfileEntry>& sceneProfiles,
		Scene& scene,
		const double deltaTime);
}

void World::Cleanup()
{
	_loadedScenes.clear();
	_instanceState.Cleanup();
	_scene.Shutdown();
}

void World::Init(
	const BindlessImageTable& imgTable,
	Extents2D renderExtent,
	Extents2D displayExtent,
	Profiler& profiler,
	GLFWwindow* window)
{
	uint32_t cookieGoboID =
		imgTable.GetStaticTexture(RD::Renderer_Texture::CookieGobo).m_bindlessID;
	uint32_t flashlightShadowMapID =
		imgTable.GetRenderTarget(RD::Renderer_RenderTarget::FlashlightShadowMap).m_bindlessID;

	LightingSystem::_mainFlashLight.Init(flashlightShadowMapID, cookieGoboID);
	LightingSystem::Init();

	_scene.InitScene(DEFAULT_SPAWN);
	_scene.UpdateCamera(renderExtent, displayExtent, profiler, window, false);

	const auto& csm =
		imgTable.GetRenderTarget(RD::Renderer_RenderTarget::DirectionalCSMAtlas);
	_scene.InitCSMInfo(csm.Width(), csm.Height(), csm.m_bindlessID);

	const auto& volShadow =
		imgTable.GetRenderTarget(RD::Renderer_RenderTarget::VolumetricShadowMap);
	_scene.InitVolumetricShadowInfo(
		volShadow.Width(),
		volShadow.Height(),
		volShadow.m_bindlessID);

	_scene.GetSceneData().renderTargetIDs = imgTable.GetRenderTargetIDs();
}

void World::OnSceneLoaded(std::shared_ptr<ModelAsset> asset)
{
	const ModelID id = asset->sceneID;
	_loadedScenes[id] = asset;

	const auto profileIt = _sceneProfiles.find(id);
	const RD::InstancingMethod method = (profileIt != _sceneProfiles.end())
		? profileIt->second.drawType
		: RD::InstancingMethod::DrawStatic;

	const bool bDynamic =
		method == RD::InstancingMethod::DrawDynamic ||
		method == RD::InstancingMethod::DrawMultiDynamic;

	auto& pool = _scene.GetTransformPool(bDynamic);

	const uint32_t firstTransform = static_cast<uint32_t>(pool.size());
	const uint32_t addedCount     = static_cast<uint32_t>(asset->nodeTransforms.size());

	for (const auto& t : asset->nodeTransforms)
		pool.push_back(t);

	if (!bDynamic)
		_scene.MarkStaticTransformsDirty(firstTransform, addedCount);

	asset->virtualInstance.firstTransform   = firstTransform;
	asset->virtualInstance.instancingMethod = method;
	asset->virtualInstance.instanceID       = static_cast<uint32_t>(_scene.GetVirtualInstances().size());

	_scene.GetVirtualInstances().push_back(asset->virtualInstance);

	asset->lightIDs.reserve(asset->lights.size());
	for (const auto& light : asset->lights)
	{
		const uint32_t lightId = LightingSystem::AddSceneLight(light);
		if (lightId != UINT32_MAX) asset->lightIDs.push_back(lightId);
	}

	_bAreAssetsLoaded = true;

	fmt::println("[World] Scene registered: '{}'", asset->sceneName);
}

void World::UpdateWorldState(
	uint32_t frameNumber,
	const Extents2D& renderExtent,
	const Extents2D& displayExtent,
	FrameContext& frameCtx,
	Profiler& profiler,
	GLFWwindow* window,
	const float adaptedEV100,
	bool isTemporalAllowed)
{
	bool bIsTemporalInvalid = false; // Assume clean start each frame

	auto& sceneData = _scene.GetSceneData();
	auto& camera = _scene.GetCamera();

	const auto& debug = profiler.debugToggles;

	_scene.ClearDynamicTransformsFlag();

	sceneData.temporal.x = frameNumber;

	bIsTemporalInvalid = _scene.UpdateCamera(renderExtent, displayExtent, profiler, window, isTemporalAllowed);

	const auto deltaTime = profiler.getStats().deltaSecondsRaw;

	// Light Updates, handle dynamics first
	bool bMainList = false;
	bool bDynamicList = false;
	bool bFlashlightChanged = false;

	// ====================
	// Flashlight updates
	// ====================

	if (UserInput::keyboard.isPressed(UserInput::Keys::F))
	{
		LightingSystem::_mainFlashLight.ToggleFlashLight();
	}

	const bool bIsFlashlightActive = LightingSystem::_mainFlashLight.IsFlashLightOn();

	// Positional changes to dirty light
	bFlashlightChanged = LightingSystem::_mainFlashLight.UpdateFlashLight(
		LightingSystem::_globalLightList,
		camera.GetPosition(),
		camera.GetView(),
		deltaTime,
		UserInput::mouse.rightPressed ? camera.GetDelta() : glm::vec2(0.0f),
		camera.GetView()
	);

	_scene.GetSceneData().flashlightVP = LightingSystem::_mainFlashLight.ViewProj;

	// Light toggle made it dirty
	if (bIsFlashlightActive != _bIsLastFlashlightActive)
	{
		_bIsLastFlashlightActive = bIsFlashlightActive;
		bFlashlightChanged = true;
	}

	if (bFlashlightChanged)
	{
		++_lightStateVersion;
	}

	// Compare current frame versions
	frameCtx.IsFlashlightStateVersionOld(_lightStateVersion);

	// ====================
	// Many lights updates
	// ====================

	bMainList = LightingSystem::UpdateLightList();
	if (LightingSystem::_dynamicLightsEnabled)
	{
		bDynamicList = LightingSystem::UpdateDynamicLightsOrbit(deltaTime);
		frameCtx.MarkDynamicLightTransforms();
	}
	else
	{
		frameCtx.EvaluatePossibleLightUpdateStatus();
	}

	// Static update changes
	frameCtx.EvaluateLightListSizeChanges(LightingSystem::_globalLightList.size());

	LightingSystem::ResolveLightUnits(adaptedEV100);

	bool bIsLightUploadedNeeded = (bMainList || bDynamicList || bFlashlightChanged);

	bIsLightUploadedNeeded = LightingSystem::UpdateLightChangeRates() || bIsLightUploadedNeeded;

	frameCtx.IsFirstLightsUpload(bIsLightUploadedNeeded);

	if (bIsLightUploadedNeeded) { frameCtx.MarkLightUpload(); }

	// ===================
	// Transform updates
	// ===================

	const bool dynamicChanged = SyncGlobalInstancesAndTransforms(_sceneProfiles, _scene, deltaTime);

	_scene.BuildMotionMatrices(_scene.GetTemporalResult());

	frameCtx.EvaluateTransformsStatus(dynamicChanged);

	const bool bTransformsChanged = _scene.VerifyTransformCount();

	bIsTemporalInvalid = bIsTemporalInvalid || bTransformsChanged;

	const bool rtShadowsOff = debug.sunShadowFilter != static_cast<uint32_t>(RD::SunShadowFilter::RT_SOFT);

	// Screen space contact shadows
	if (debug.enableSSS)
	{
		_scene.BuildDispatchList();
	}

	// Cascaded shadow map updates
	if (debug.enableShadows && rtShadowsOff)
	{
		_scene.UpdateShadowTexel(static_cast<RD::SunShadowFilter>(debug.sunShadowFilter));
		_scene.UpdateCSMInfo();
	}

	// Volumetric csm update
	if (debug.enableVolumetrics)
	{
		_scene.UpdateVolumetricShadowInfo(profiler.froxelSettings.froxelClips.y);
	}

	// Now the temporal should be known if this frame is safe
	_scene.SetTemporalValue(bIsTemporalInvalid);
}

static glm::mat4 MakeClusterTransform(
	uint32_t index,
	uint32_t count,
	float radius)
{
	ASSERT(count > 0);

	std::mt19937 rng(9001 + index);

	std::uniform_real_distribution<float> positionDistribution(-radius, radius);
	std::uniform_real_distribution<float> rotationDistribution(0.0f, glm::two_pi<float>());

	const glm::vec3 translation =
		glm::vec3(
			positionDistribution(rng),
			positionDistribution(rng),
			positionDistribution(rng));

	const glm::vec3 rotation =
		glm::vec3(
			rotationDistribution(rng),
			rotationDistribution(rng),
			rotationDistribution(rng));

	return
		glm::translate(glm::mat4(1.0f), translation) *
		glm::rotate(glm::mat4(1.0f), rotation.x, glm::vec3(1.0f, 0.0f, 0.0f)) *
		glm::rotate(glm::mat4(1.0f), rotation.y, glm::vec3(0.0f, 1.0f, 0.0f)) *
		glm::rotate(glm::mat4(1.0f), rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
}

static bool braindeadhack = false;
bool World::SyncGlobalInstancesAndTransforms(
	std::unordered_map<ModelID, SceneProfileEntry>& sceneProfiles,
	Scene& scene,
	const double deltaTime)
{
	if (!_bAreAssetsLoaded) return false;

	bool dynamicChanged = false;
	bool staticUpdated = false;

	for (auto& inst : scene.GetVirtualInstances())
	{
		const bool bDynamic =
			inst.instancingMethod == RD::InstancingMethod::DrawDynamic ||
			inst.instancingMethod == RD::InstancingMethod::DrawMultiDynamic;

		auto& transforms = scene.GetTransformPool(bDynamic);
		uint32_t appendStart = static_cast<uint32_t>(transforms.size());

		ModelID sid = static_cast<ModelID>(inst.sceneID);
		SceneProfileEntry& profile = sceneProfiles.at(sid);

		if (profile.instanceCount == 1)
		{
			if (profile.drawType == RD::InstancingMethod::DrawStatic)
			{
				// TODO: Create a way to modify transforms at runtime
				if (!braindeadhack && sid == ModelID::DamagedHelmet) {
					glm::mat4& M = transforms[inst.firstTransform];

					const glm::mat4& baseTransform = inst.baseTransform;
					const glm::vec3 pivot = glm::vec3(baseTransform[3]);

					// Turn helmet for bake
					glm::mat4 pitch =
						glm::rotate(
							glm::mat4(1.0f),
							glm::radians(90.0f),
							glm::vec3(1.0f, 0.0f, 0.0f)
						);

					glm::mat4 yawLeft =
						glm::rotate(
							glm::mat4(1.0f),
							glm::radians(90.0f),
							glm::vec3(0.0f, 1.0f, 0.0f)
						);

					glm::mat4 rotate = yawLeft * pitch;

					glm::vec3 offset =
						glm::vec3(
							0.0f,  // world X
							2.5f,  // world Y
							0.0f   // world Z
						);

					M =
						glm::translate(glm::mat4(1.0f), offset) *
						glm::translate(glm::mat4(1.0f), pivot) *
						rotate *
						glm::translate(glm::mat4(1.0f), -pivot);
					staticUpdated = true;
					braindeadhack = true;
				}
				inst.instancingMethod = profile.drawType;
				continue; // transforms already baked into static
			}

			if (profile.drawType == RD::InstancingMethod::DrawDynamic)
			{
				inst.instancingMethod = profile.drawType;

				constexpr float spinSpeedRadiansPerSecond = glm::radians(40.0f);
				const float deltaSecondsFloat = static_cast<float>(deltaTime);

				inst.spinAngleRadians += spinSpeedRadiansPerSecond * deltaSecondsFloat;

				const glm::mat4& baseTransform = inst.baseTransform;
				const glm::vec3 pivot = glm::vec3(baseTransform[3]);
				inst.modelOffset = glm::vec3(0.0f, 2.5f, 0.0f);

				const glm::mat4 newTransform =
					glm::translate(glm::mat4(1.0f), pivot) *
					glm::rotate(glm::mat4(1.0f), inst.spinAngleRadians, glm::vec3(0.0f, 1.0f, 0.0f)) *
					glm::translate(glm::mat4(1.0f), -pivot) *
					glm::translate(glm::mat4(1.0f), inst.modelOffset) *
					baseTransform;

				transforms[inst.firstTransform] = newTransform;

				dynamicChanged = true;
				continue;
			}
		}

		// Defined from copy values append list or decrease list
		// on first run this will always be an append
		if (profile.drawType == RD::InstancingMethod::DrawMultiStatic && profile.instanceCount > 1)
		{
			inst.instancingMethod = profile.drawType;

			const uint32_t desiredUsedCopies = std::max(1u, profile.instanceCount);

			const uint32_t desiredCapacityCopies = desiredUsedCopies;

			if (inst.usedCopies == desiredUsedCopies && inst.capacityCopies >= desiredCapacityCopies) continue;

			const uint32_t transformsPerCopy = inst.transformCount;
			ASSERT(transformsPerCopy > 0);

			const uint32_t oldCapacityCopies = inst.capacityCopies;
			const uint32_t oldSlabTransformCount = oldCapacityCopies * transformsPerCopy;
			const uint32_t oldSlabBegin = inst.firstTransform;
			const uint32_t oldSlabEnd = oldSlabBegin + oldSlabTransformCount;

			// We can only append in-place if this slab currently ends at the end of the global array.
			const bool slabIsAtEnd = (oldSlabEnd == transforms.size());

			if (desiredCapacityCopies > oldCapacityCopies)
			{
				//const glm::mat4 baseTransform = transforms[oldSlabBegin];

				if (!slabIsAtEnd) {
					// Relocate this slab to the end to preserve the "contiguous slab" invariant.
					const uint32_t newFirstTransform = static_cast<uint32_t>(transforms.size());

					// Copy old slab transforms.
					for (uint32_t i = 0; i < oldSlabTransformCount; ++i) {
						transforms.push_back(transforms[static_cast<size_t>(oldSlabBegin + i)]);
					}

					inst.firstTransform = newFirstTransform;
				}

				// Append transforms for new copies (copy indices [oldCap .. newCap)).
				for (uint32_t copyIndex = oldCapacityCopies; copyIndex < desiredCapacityCopies; ++copyIndex)
				{
					glm::mat4 offset =
						MakeClusterTransform(
							copyIndex,
							desiredCapacityCopies,
							100.0f);

					for (uint32_t slot = 0; slot < transformsPerCopy; ++slot)
					{
						const uint32_t baseSlotIndex = inst.firstTransform + slot; // copy 0, slot N
						const glm::mat4 slotBaseTransform = transforms[baseSlotIndex];

						transforms.push_back(offset * slotBaseTransform);
					}
				}

				inst.capacityCopies = desiredCapacityCopies;
				dynamicChanged = true;
			}

			uint32_t appendCount = static_cast<uint32_t>(transforms.size()) - appendStart;
			scene.MarkStaticTransformsDirty(appendStart, appendCount);

			// Change active copies (visibility will rebuild/activate).
			inst.usedCopies = desiredUsedCopies;
		}
	}

	if (dynamicChanged)
		scene.MarkDynamicTransformsDirty();

	return dynamicChanged || staticUpdated;
}
