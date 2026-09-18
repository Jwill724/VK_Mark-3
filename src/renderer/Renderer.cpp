#include "pch.h"

#include "Renderer.h"
#include "backend/memory/Budgets.h"
#include "backend/Device.h"
#include "backend/pipelines/PipelineManager.h"
#include "backend/descriptors/DescriptorManager.h"
#include "backend/PhysicalDeviceSelector.h"
#include "backend/BufferBarriers.h"
#include "backend/ImageUtils.h"
#include "rendergraph/RenderPasses.h"
#include "scene/World.h"
#include "scene/LightingSystem.h"
#include "scene/DrawPreparation.h"
#include "scene/Scene.h"
#include "core/Window.h"
#include "core/JobSystem.h"
#include "core/Environment.h"
#include "core/asset/AssetUploadTypes.h"

#ifndef NDEBUG

#define RESIZE_TRACE(...) \
	do { \
		fmt::print(stderr, __VA_ARGS__); \
		fmt::print(stderr, "\n"); \
	} while (0)

#else

#define RESIZE_TRACE(...) ((void)0)

#endif

static_assert(sizeof(InstanceInput)   == SIZEOF_INSTANCE_INPUT);
static_assert(sizeof(DrawBin)         == SIZEOF_DRAW_BIN);
static_assert(sizeof(VkAccelerationStructureInstanceKHR) == SIZEOF_RT_INSTANCE);

inline constexpr bool LensFlareOn           = true;
inline constexpr bool ChromaticAberrationOn = true;
inline constexpr bool BloomOn               = true;
inline constexpr bool VolumetricsOn         = true;
inline constexpr bool ShadowsOn             = true;
inline constexpr bool rtReflectionsOn       = true;
inline constexpr bool ScreenSpaceShadowsOn  = true;
inline constexpr bool ProfilerViewOn        = false;
inline constexpr bool SettingsTabOn         = true;

float Renderer::GetAdaptedEV100() const
{
	return m_luminanceMapped ? (*m_luminanceMapped)[0].z : LightUnits::EV_SEED;
}

void Renderer::Init(
	const Window& window,
	JobSystem& jobSystem)
{
	SetRenderExtent(window.GetExtent());
	SetDisplayExtent(window.GetExtent());

	InitRenderSettings(
		LensFlareOn,
		ChromaticAberrationOn,
		BloomOn,
		ShadowsOn,
		ScreenSpaceShadowsOn,
		VolumetricsOn,
		rtReflectionsOn,
		RD::AntiAliasingMethod::AA_TAA_CAS,
		RD::GIMethod::VBGI,
		RD::ShadowQuality::High,
		RD::SunShadowFilter::RT_SOFT,
		ProfilerViewOn,
		SettingsTabOn);

	ApplyPushConstantDefaults();

	// ==========================
	// === Vulkan state setup ===

	// ----------------
	// Device creation
	// ----------------
	m_device = std::make_unique<Device>();
	m_device->CreateInstance();
	m_device->CreateSurface(window.GetWindowHandle());

	auto deviceCandidate = PhysicalDeviceSelector::PickBest(
		m_device->GetContext().instance,
		m_device->GetSurface(),
		m_device->GetDeviceExtensions());

	m_device->InitLogical(deviceCandidate);

	m_profiler.SetGPUName(m_device->GetPhysicalDeviceName());

	m_device->InitThreadCommandPool(jobSystem.GetThreadCount());

#ifdef TRACY_ENABLE
	const auto& mainThread = jobSystem.GetMainContext();
	auto mainThreadGraphicsPool = m_device->GetThreadCommandPool(mainThread.threadID, QueueType::Graphics);
	auto mainThreadComputePool = m_device->GetThreadCommandPool(mainThread.threadID, QueueType::Compute);
	m_profiler.SetTracyGraphicsCmd(m_device->CreateCommandBuffer(mainThreadGraphicsPool));
	m_profiler.SetTracyComputeCmd(m_device->CreateCommandBuffer(mainThreadComputePool));

	m_profiler.InitTracyGraphics(
		m_device->GetContext().physicalDevice,
		m_device->GetContext().device,
		m_device->GetGraphicsQueue().GetQueue(),
		m_profiler.GetTracyGraphicsCmd());

	m_profiler.InitTracyCompute(
		m_device->GetContext().physicalDevice,
		m_device->GetContext().device,
		m_device->GetComputeQueue().GetQueue(),
		m_profiler.GetTracyComputeCmd(),
		jobSystem.GetThreadCount());
#endif

	m_profiler.SetDevice(m_device.get());

	// -------------------
	// Allocator creation
	// -------------------
	m_allocator.Init(m_device->GetContext());

	// ----------
	// Swapchain
	// ----------
	m_swapchain.Init(
		m_device->GetContext(),
		m_device->GetSurface(),
		m_device->GetSwapchainSupportDetails(),
		window.GetExtent());

	// ------------------------
	// Descriptor sets/layouts
	//-------------------------
	m_descriptorManager = std::make_unique<DescriptorManager>();
	m_descriptorManager->InitDescriptors(m_device->GetContext().device);

	// ----------
	// Pipelines
	//-----------
	m_pipelineManager = std::make_unique<PipelineManager>();
	m_pipelineManager->CreatePipelineLayout(
		m_device->GetContext().device,
		m_descriptorManager->GetDescriptorLayouts());

	{
		std::string shaderLog;
		const bool allOk = m_shaderCache.BuildFromTable(shaderLog);
		fmt::print("{}", shaderLog);
		INVARIANT(allOk);
	}

	m_pipelineManager->InitPipelines(m_device->GetContext().device, m_shaderCache);

	m_shaderHotReload.Init(
		m_shaderCache,
		m_device->GetContext().device,
		m_pipelineManager->GetGlobalLayout().pipelineLayout);

	m_worldProbesHeader = {};
	m_worldProbesPush = {};
	m_worldProbesState = {};

	// Independent resource owners initialize concurrently. Keep each table's
	// internal insertion/allocation order serial within its own job.
	jobSystem.SubmitJob([this](ThreadContext&) {
		m_globalAddressTable.Init(m_allocator);

		m_globalAddressTable.AddGPUBufferToAddressTable(
			RD::Renderer_Buffer::Luminance,
			GPU_BYTES_LUMINANCE,
			m_allocator);

		m_globalAddressTable.AddGPUBufferToAddressTable(
			RD::Renderer_Buffer::WorldProbes,
			GetWorldProbeBufferBytes(RD::WORLD_PROBE_COUNT),
			m_allocator);

		m_globalAddressTable.AddGPUBufferToAddressTable(
			RD::Renderer_Buffer::WorldProbeSchedule,
			GetWorldProbeScheduleBytes(),
			m_allocator);

		m_globalAddressTable.AddGPUBufferToAddressTable(
			RD::Renderer_Buffer::WorldProbeSummary,
			GetWorldProbeSummaryBytes(),
			m_allocator);

		m_globalAddressTable.AddGPUBufferToAddressTable(
			RD::Renderer_Buffer::InstanceInputs,
			GPU_BYTES_INSTANCE_INPUT,
			m_allocator);

		m_globalAddressTable.AddGPUBufferToAddressTable(
			RD::Renderer_Buffer::RTRows,
			GPU_BYTES_RT_ROWS,
			m_allocator);

		m_globalAddressTable.AddGPUBufferToAddressTable(
			RD::Renderer_Buffer::StaticTransforms,
			GPU_BYTES_STATIC_TRANSFORMS,
			m_allocator);

		m_globalAddressTable.AddGPUBufferToAddressTable(
			RD::Renderer_Buffer::DrawBinKeys,
			GPU_BYTES_DRAW_BIN_KEYS,
			m_allocator);

		m_luminanceReadbackBuffer = m_allocator.AllocateBuffer({
			GPU_BYTES_LUMINANCE,
			Vulkan_BufferUsage::READ_BACK,
			HeapType::Readback
			});

		vmaMapMemory(m_allocator.GetVma(),
			m_luminanceReadbackBuffer.m_allocation,
			reinterpret_cast<void**>(
				const_cast<std::array<glm::vec4, RD::MAX_LUMINANCE_GROUPS>**>(&m_luminanceMapped)));
		});

	jobSystem.SubmitJob([this](ThreadContext&) {
		m_bindlessImageTable.Init(
			{ m_renderExtent.Width(), m_renderExtent.Height(), 1u },
			m_currentShadowQuality,
			m_device->GetContext().device,
			m_allocator);
		});

	// Called from the initialization thread, never from a SubmitJob callback.
	// RunParallel joins frame work; Wait also joins both jobs above.
	InitFrameResources(jobSystem);
	jobSystem.Wait();

	const size_t totalFrameStaging =
		GPU_BYTES_INSTANCE_INPUT +
		GPU_BYTES_DRAW_BIN_KEYS +
		GPU_BYTES_DYNAMIC_TRANSFORMS +
		GPU_BYTES_DYNAMIC_TRANSFORMS + // Motion matrices
		GPU_BYTES_LIGHTS +
		GPU_BYTES_LUMINANCE +
		m_globalAddressTable.GPU_ADDRESS_TABLE_SIZE_GPU_BYTES;

	m_allocator.InitFrameStaging(totalFrameStaging, m_device->GetNonCoherentAtomSize());

	m_device->InitCrashMarkers(m_framesInFlight, 64);

	// ===============================
	// === Global Data processing ====

	const size_t globalStagingSize = m_allocator.CalcBaseGlobalStagingSize(m_bindlessImageTable);
	m_allocator.InitGlobalStaging(globalStagingSize, m_device->GetNonCoherentAtomSize());

	jobSystem.SubmitJob([&](ThreadContext& threadCtx) {
		auto cmdpool = m_device->GetThreadCommandPool(threadCtx.threadID, QueueType::Graphics);

		m_device->RecordDeferredCommand([&](VkCommandBuffer cmd)
			{
				m_bindlessImageTable.UploadStaticTextures(m_allocator.GlobalStaging, cmd);

			}, cmdpool, QueueType::Graphics);
		});

	// Keep staged texture bytes alive until deferred uploads are submitted.

	// Luminance buffer default values
	{
		const float seedExposure = LightUnits::ExposureFromEV100(LightUnits::EV_SEED);
		m_defaultLuminance = glm::vec4(seedExposure, seedExposure, LightUnits::EV_SEED, 0.0f);
		// .x = exposure, .y = previous exposure, .z = EV100, .w = history valid
		m_luminanceSums[0] = m_defaultLuminance;
		m_luminanceSumsReadback[0] = m_defaultLuminance;
	}

	// Global address table and luminance buffer upload
	jobSystem.SubmitJob([&](ThreadContext& threadCtx) {
		auto cmdpool = m_device->GetThreadCommandPool(
			threadCtx.threadID,
			QueueType::Transfer);

		auto stageCopyLuminance = m_allocator.GlobalStaging.Stage(
			m_luminanceSums.data(),
			GPU_BYTES_LUMINANCE,
			m_globalAddressTable.GetGPUBuffer(
				RD::Renderer_Buffer::Luminance).m_buffer);

		auto stageCopyGlobalAddrTable = m_allocator.GlobalStaging.Stage(
			m_globalAddressTable.GetAddrPtrTable().data(),
			m_globalAddressTable.GPU_ADDRESS_TABLE_SIZE_GPU_BYTES,
			m_globalAddressTable.GetTableBuffer().m_buffer);

		m_allocator.GlobalStaging.Flush();

		m_device->RecordDeferredCommand(
			[&, stageCopyLuminance, stageCopyGlobalAddrTable](VkCommandBuffer cmd)
			{
				m_allocator.GlobalStaging.CopyCommand(cmd, stageCopyLuminance);
				m_allocator.GlobalStaging.CopyCommand(cmd, stageCopyGlobalAddrTable);

				BufferBarriers::TransferReleaseOnGraphics(
					cmd,
					m_globalAddressTable.GetTableBuffer(),
					m_device->GetContext());
			},
			cmdpool,
			QueueType::Transfer);
		});

	// =======================
	// === BRDF LUT SETUP ====

	jobSystem.SubmitJob([&](ThreadContext& threadCtx) {
		auto cmdpool = m_device->GetThreadCommandPool(threadCtx.threadID, QueueType::Graphics);

		m_device->RecordDeferredCommand([&](VkCommandBuffer cmd)
			{
				m_descriptorManager->BindGlobalSetCompute(
					cmd,
					m_pipelineManager->GetGlobalLayout());

				ComputeScope pso{ {} };
				PushDescriptorWriter pushWriter;

				const auto& brdf = m_bindlessImageTable.GetStaticTexture(RD::Renderer_Texture::Brdf);

				ImageUtils::TransitionLayout(cmd, brdf, RD::ImageAccess::Undefined, RD::ImageAccess::Write);

				pso.BindWriteImage(pushWriter, RD::PUSH_BINDING_WRITE_1, brdf);
				pso.UpdateExtent({ brdf.Width(), brdf.Height() });
				pso.UpdateWorkgroups(WORKGROUP_8x8);
				pso.SetPush(RD::PREFILTER_SAMPLE_COUNT);

				pso.DispatchComputePass(cmd, m_pipelineManager->GetHandle(RD::Renderer_Pipeline::BRDFLUT), pushWriter);

				ImageUtils::TransitionLayout(cmd, brdf, RD::ImageAccess::Write, RD::ImageAccess::Read);

			}, cmdpool, QueueType::Graphics);
		});
	jobSystem.Wait();
	m_device->SubmitDeferredCommands(QueueType::Graphics);
	m_device->SubmitDeferredCommands(QueueType::Transfer);
	m_allocator.GlobalStaging.Reset();

	// ===============================
	// === Global descriptor setup ===

	jobSystem.SubmitJob([&](ThreadContext& threadCtx) {
		m_bindlessImageTable.BuildInitialCombinedSamplerArray();
		});

	m_nrdReflectContext.Init(
		*m_device,
		m_allocator,
		{ (m_renderExtent.Width() + 1u) / 2u, (m_renderExtent.Height() + 1u) / 2u },
		NRDContext::DenoiserMode::Reflections);

	m_nrdShadowContext.Init(
		*m_device,
		m_allocator,
		m_renderExtent,
		NRDContext::DenoiserMode::Shadows);

	jobSystem.SubmitJob([&](ThreadContext& threadCtx) {
		auto cmdGfxPool = m_device->GetThreadCommandPool(threadCtx.threadID, QueueType::Graphics);
		m_device->RecordDeferredCommand([&](VkCommandBuffer cmd)
			{
				m_bindlessImageTable.TransitionRenderTargetsFromUndefined(cmd);
			}, cmdGfxPool, QueueType::Graphics);
		});

	jobSystem.SubmitJob([&](ThreadContext& threadCtx) {
		auto cmdCompPool = m_device->GetThreadCommandPool(threadCtx.threadID, QueueType::Compute);
		m_device->RecordDeferredCommand([&](VkCommandBuffer cmd)
			{
				m_nrdReflectContext.RecordPoolInit(cmd);
				m_nrdShadowContext.RecordPoolInit(cmd);
			}, cmdCompPool, QueueType::Compute);
		});

	jobSystem.Wait();

	m_device->SubmitDeferredCommands(QueueType::Graphics);
	m_device->SubmitDeferredCommands(QueueType::Compute);
	m_device->GetGraphicsQueue().WaitIdle();
	m_device->GetComputeQueue().WaitIdle();

	m_bindlessImageTable.FreeEquirects(m_allocator);

	m_profiler.SetVRAMUsage(m_allocator.GetTotalVRAMUsage());

	CreateRenderGraph();

	m_renderGraph.SetDevice(*m_device);

	World::Init(
		m_bindlessImageTable,
		m_renderExtent,
		m_displayExtent,
		m_profiler,
		window.GetWindowHandle());

	auto& forwardPush = m_profiler.forwardPush;
	forwardPush.flashlightCookieTexID = LightingSystem::_mainFlashLight.m_cookieGoboID;
	forwardPush.flashlightShadowMapID = LightingSystem::_mainFlashLight.m_shadowMapID;

	uint32_t brdfID = m_bindlessImageTable.GetStaticTexture(RD::Renderer_Texture::Brdf).m_bindlessID;

	forwardPush.brdfID = brdfID;
	m_profiler.reflectPush.brdfID = brdfID;

	m_profiler.lensFlareSettings.rainbowLUTIndex = m_bindlessImageTable.GetStaticTexture(RD::Renderer_Texture::RainbowLut).m_bindlessID;

	uint32_t hilbertCurveID = m_bindlessImageTable.GetStaticTexture(RD::Renderer_Texture::HilbertCurveLut).m_bindlessID;
	m_profiler.ssgiSettings.hilbertLutID = hilbertCurveID;
	m_profiler.reflectPush.hilbertLutID = hilbertCurveID;
	m_profiler.rtShadowPush.shadowStbnID = m_bindlessImageTable.GetStaticTexture(RD::Renderer_Texture::ShadowSTBN).m_bindlessID;

	const auto& transmittance = m_bindlessImageTable.GetRenderTarget(
		RD::Renderer_RenderTarget::AtmosphereTransmittance);

	const auto& skyView = m_bindlessImageTable.GetRenderTarget(
		RD::Renderer_RenderTarget::AtmosphereSkyView);

	const auto& lighting = m_bindlessImageTable.GetRenderTarget(
		RD::Renderer_RenderTarget::AtmosphereLighting);

	ASSERT(transmittance.m_bindlessID != UINT32_MAX);
	ASSERT(skyView.m_bindlessID != UINT32_MAX);
	ASSERT(lighting.m_bindlessID != UINT32_MAX);

	AtmosphereResources resources{};

	resources.textureIDs = glm::uvec4(
		transmittance.m_bindlessID,
		skyView.m_bindlessID,
		lighting.m_bindlessID,
		UINT32_MAX);

	resources.transmittanceExtent = glm::uvec4(
		transmittance.Width(),
		transmittance.Height(),
		0u,
		0u);

	m_atmosphereResources_UBO = m_allocator.AllocateUniform(resources);

	m_mainWriter.WriteBuffer(
		RD::GLOBAL_ATMOSPHERE_BINDING,
		m_atmosphereResources_UBO,
		m_descriptorManager->GetGlobalSet(),
		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);

	m_mainWriter.UpdateSet(
		m_device->GetContext().device,
		m_descriptorManager->GetGlobalSet());
}

// Only called after swapchain presented and queue wait
void Renderer::CheckCSMAtlasExtentUpdate()
{
	if (m_currentShadowQuality == m_profiler.shadowQuality) return;

	StallDevice();
	m_currentShadowQuality = m_profiler.shadowQuality;
	m_bindlessImageTable.UpdateCSMAtlasExtent(m_currentShadowQuality, m_allocator);

	if (m_bindlessImageTable.IsShadowAtlasCached()) return;

	m_renderGraph.NotifyLayout(
		RD::Renderer_RenderTarget::DirectionalCSMAtlas,
		RD::ImageAccess::Undefined);

	const auto& csmAtlas = m_bindlessImageTable.GetRenderTarget(RD::Renderer_RenderTarget::DirectionalCSMAtlas);
	World::GetScene().InitCSMInfo(csmAtlas.Width(), csmAtlas.Height(), csmAtlas.m_bindlessID);
}

void Renderer::CheckGlobalDescriptorSetSync()
{
	bool updateSet = false;

	if (m_globalAddressTable.IsTableDirty())
	{
		m_mainWriter.WriteBuffer(
			RD::ADDRESS_TABLE_BINDING,
			m_globalAddressTable.GetTableBuffer(),
			m_descriptorManager->GetGlobalSet());

		m_globalAddressTable.ClearDirty();

		updateSet = true;
	}

	if (m_bindlessImageTable.IsTableDirty())
	{
		m_mainWriter.WriteBindlessImages(
			m_bindlessImageTable.GetCombinedSamplerArray(),
			RD::GLOBAL_BINDING_COMBINED_SAMPLER,
			m_descriptorManager->GetGlobalSet());

		m_mainWriter.WriteBindlessImages(
			m_bindlessImageTable.GetSamplerCubeArray(),
			RD::GLOBAL_BINDING_SAMPLER_CUBE,
			m_descriptorManager->GetGlobalSet());

		m_bindlessImageTable.ClearDirty();

		updateSet = true;
	}

	static RD::RenderToggles last{};
	const RD::RenderToggles& cur = m_profiler.debugToggles;

	if (memcmp(&last, &cur, sizeof(RD::RenderToggles)) != 0)
	{
		m_mainWriter.WriteInlineUniform(
			m_device->GetContext().device,
			m_descriptorManager->GetGlobalSet(),
			&cur,
			static_cast<uint32_t>(sizeof(RD::RenderToggles)));
		last = cur;
	}

	if (updateSet)
	{
		m_mainWriter.UpdateSet(m_device->GetContext().device, m_descriptorManager->GetGlobalSet());
	}
}

void Renderer::CreateRenderGraph()
{
	const uint32_t gfxFamily     = m_device->GetGraphicsQueue().GetFamilyIndex();
	const uint32_t computeFamily = m_device->GetComputeQueue().GetFamilyIndex();

	const bool bDedicatedCompute = (gfxFamily != computeFamily);

	if (!bDedicatedCompute)
	{
		fmt::println(
			"[Renderer] No dedicated compute queue family (graphics and compute "
			"both on family {}). Async compute disabled; the graph will use the "
			"single-batch path.", gfxFamily);
	}

	m_renderGraph.Build(m_renderExtent, m_displayExtent, bDedicatedCompute);
}

void Renderer::DestroyRenderGraph()
{
	m_renderGraph.Shutdown();
}



// ===============================================
// ===============================================
// ===============================================
// ===============================================
// ============= RUNTIME RENDERING ===============

void Renderer::UpdateRendererContext(GLFWwindow* window)
{
	auto& frameCtx = GetCurrentFrame();

	auto& debug = m_profiler.debugToggles;

	const auto& scene = World::GetScene();
	const auto& sceneData = scene.GetSceneData();

	UpdateShadowMode();

	World::UpdateWorldState(
		m_frameNumber,
		m_renderExtent,
		m_displayExtent,
		frameCtx,
		m_profiler,
		window,
		GetAdaptedEV100(),
		m_renderGraphState.TemporalAllowed());

	frameCtx.SetTemporalResult(scene.GetTemporalResult() && !IsFirstFrame());
	frameCtx.SetHiZValidResult(scene.GetHiZTemporalResult());

	{
		if (m_bLuminanceResetPending)
		{
			frameCtx.MarkLuminanceReset();
			m_bLuminanceResetPending = false;
		}
	}

	m_renderGraphState.SetTemporalIndex(static_cast<uint64_t>(sceneData.temporal.x));

	uint64_t noiseIndex = m_renderGraphState.GetTemporalIndex() % 64u;

	bool instanceUploadNeeded = false;

	instanceUploadNeeded = DrawPreparation::SyncInstanceInputs(
		World::GetInstanceState(),
		scene,
		World::_loadedScenes,
		m_registeredMeshes.GetMeshes(),
		m_registeredMeshes.GetLods(),
		m_materialFlagsIDs,
		m_blasAddresses);

	frameCtx.FlagInstanceInputUpload(instanceUploadNeeded);

	const bool rtDirty = instanceUploadNeeded || scene.HasDynamicTransformChanges();

	if (rtDirty)
	{
		for (uint32_t i = 0; i < m_framesInFlight; ++i)
			m_frameContexts[i].MarkTlasDirty();
	}

	if (frameCtx.IsInstanceInputsUploadNeeded())
		m_drawBinTableBuild = DrawPreparation::BuildDrawBinTable(World::GetInstanceState().gpuInputs);

	auto& forwardPush = m_profiler.forwardPush;
	auto& lumaPush = m_profiler.lumaExposureSettings;
	auto& taaPush = m_profiler.taaSettings;
	auto& reflectPush = m_profiler.reflectPush;
	auto& rtShadowPush = m_profiler.rtShadowPush;
	auto& nrdRPush = m_profiler.nrdReflectPush;
	auto& nrdSPush = m_profiler.nrdShadowPush;

	glm::vec2 fullPixelSize = glm::vec2(sceneData.renderPixelSizes);

	const float rawDt = std::max(m_profiler.getStats().deltaSecondsRaw, 1e-5f);
	taaPush.invDeltaTime = 1.0f / rawDt;

	// Luminace exposure pass
	const uint32_t lumaW = m_renderExtent.Width();
	const uint32_t lumaH = m_renderExtent.Height();

	uint32_t tilesX = (lumaW + 15u) / 16u;
	uint32_t tilesY = (lumaH + 15u) / 16u;

	lumaPush.totalLumaTiles = tilesX * tilesY;
	lumaPush.pixelCount = lumaW * lumaH;
	lumaPush.deltaTime = std::clamp(rawDt, 1e-4f, 0.1f);
	lumaPush.resetAdaptation = frameCtx.IsLuminanceResetNeeded() ? 1u : 0u;

	if (lumaPush.manualExposure != 0u && m_lastManualExposure == 0u)
		lumaPush.manualEV100 = scene.GetCamera().GetEV100();

	m_lastManualExposure = lumaPush.manualExposure;

	glm::vec2 halfResSize = {
		static_cast<float>(m_rtRayListLayout.halfWidth),
		static_cast<float>(m_rtRayListLayout.halfHeight)
	};

	glm::vec2 halfResTexel = 1.0f / halfResSize;

	nrdRPush.resSize = halfResSize;
	nrdRPush.resTexel = halfResTexel;
	nrdRPush.writeMotion = 1u;

	// Full screen sizes
	nrdSPush.resSize = glm::vec2(sceneData.renderExtentSize); // .xy
	nrdSPush.resTexel = fullPixelSize;
	nrdSPush.writeMotion = 0u;

	// RT Shadow push
	{
		const SunBasis sun = BuildSunBasis(sceneData.sunlightDirection);

		rtShadowPush.shadow.sunDirectionWS = glm::vec4(sun.direction, rtShadowPush.shadow.rayTMin);
		rtShadowPush.shadow.sunTangentWS = glm::vec4(sun.tangent, rtShadowPush.shadow.rayTMax);
		rtShadowPush.shadow.sunBitangentWS = glm::vec4(sun.bitangent, 1.0);
		rtShadowPush.shadow.sunDirectionVS = glm::vec4(glm::mat3(sceneData.view) * sun.direction, 0.0f);

		rtShadowPush.resolution = nrdSPush.resSize;
		rtShadowPush.invResolution = nrdSPush.resTexel;

		rtShadowPush.rayCapacity = m_rtRayListLayout.capacities[RD::RT_RAY_SLOT_SHADOW];
		rtShadowPush.rayBase = m_rtRayListLayout.bases[RD::RT_RAY_SLOT_SHADOW];
	}

	// RT reflection push
	{
		reflectPush.halfResSize = halfResSize;
		reflectPush.halfResTexel = halfResTexel;
		reflectPush.noiseIndex = noiseIndex;
		reflectPush.shadow.sunDirectionWS = rtShadowPush.shadow.sunDirectionWS;
		reflectPush.rayCapacity = m_rtRayListLayout.capacities[RD::RT_RAY_SLOT_REFLECT];
		reflectPush.rayBase = m_rtRayListLayout.bases[RD::RT_RAY_SLOT_REFLECT];
	}

	if (m_activeEnvSet != debug.activeEnvMap)
	{
		m_activeEnvSet = debug.activeEnvMap;

		const auto& envSet = m_bindlessImageTable.GetEnvironmentSet(m_activeEnvSet);
		forwardPush.specularID = envSet.specular.m_bindlessID;

		reflectPush.specularID = envSet.specular.m_bindlessID;
		reflectPush.skyboxID = envSet.skybox.m_bindlessID;
	}

	forwardPush.reflectRoughCutoff = reflectPush.reflectRoughnessCutoff;
	forwardPush.reflectRoughFade = reflectPush.roughnessFadeStart;
	forwardPush.halfTexel = halfResTexel;

	debug.enableWireframe = m_profiler.enableWireframeView;

	debug.enableFlashlight = LightingSystem::_mainFlashLight.IsFlashLightOn();

	debug.activeInstanceCount = World::GetInstanceState().gpuInputs.size();
	debug.activeLightCount = LightingSystem::GetLightBufferCount();
	debug.activeRTInstances = World::GetInstanceState().rtInstanceCount;

	debug.worldProbesDebugView =
		(m_profiler.worldProbeSettings.enabled && m_profiler.worldProbeSettings.debugDraw) ? 1u : 0u;

	m_renderGraphState.UpdateToggles(debug);
	m_renderGraphState.UpdateTemporal(
		frameCtx.IsTemporalValid(),
		frameCtx.IsHiZValid());

	{
		auto& p = m_profiler.ssgiSettings;

		p.ndcToViewMul_x_PixelSize =
			sceneData.ndcToViewMult * fullPixelSize;

		p.noiseIndex = static_cast<uint32_t>(noiseIndex);
		p.isFinalPass = 0u;

		p.aoHistoryWeight = std::clamp(p.aoHistoryWeight, 0.0f, 0.99f);
		p.aoDepthTolerance = std::clamp(p.aoDepthTolerance, 0.001f, 0.1f);
		p.aoNormalThreshold = std::clamp(p.aoNormalThreshold, 0.0f, 0.9999f);

		const bool aoRunsThisFrame =
			m_renderGraphState.InstancesActive() &&
			debug.giMode != static_cast<uint32_t>(RD::GIMethod::OFF) &&
			!m_renderGraphState.IsWireframeOn();

		p.aoHistoryValid =
			aoRunsThisFrame &&
			m_aoRanPreviousFrame &&
			frameCtx.IsTemporalValid()
			? 1u
			: 0u;

		m_aoRanPreviousFrame = aoRunsThisFrame;
	}

	m_renderGraphState.ResetDebugMask();
	// Priority order
	if (debug.enableWireframe)
	{
		m_renderGraphState.SetDebugMask(RD::DebugState::Wireframe);
	}
	else if (frameCtx.m_bDebugLineRendering)
	{
		m_renderGraphState.SetDebugMask(RD::DebugState::OBBLine);
	}
	else if (debug.debugView != 0u)
	{
		m_renderGraphState.SetDebugMask(RD::DebugState::ShadedOverlay);
	}

	DrawPreparation::UploadGPUBuffersForFrame(
		frameCtx,
		m_globalAddressTable,
		m_drawBinTableBuild.binKeys,
		*m_device,
		m_allocator,
		World::GetInstanceState().gpuInputs,
		World::GetInstanceState().rtRows,
		World::GetScene(), // Needs reference
		LightingSystem::_globalLightList,
		m_defaultLuminance,
		frameCtx.IsTemporalValid() && m_renderGraphState.IsTaaOn());

	if (m_renderGraphState.IsNRDActive())
	{
		m_nrdReflectContext.SetFrameSettings(
			sceneData,
			m_bindlessImageTable,
			m_profiler.getStats().deltaSecondsRaw,
			frameCtx.IsTemporalValid() && !IsFirstFrame());

		m_nrdShadowContext.SetFrameSettings(
			sceneData,
			m_bindlessImageTable,
			m_profiler.getStats().deltaSecondsRaw,
			frameCtx.IsTemporalValid() && !IsFirstFrame());
	}

	m_atmosphereState.Prepare(m_profiler.atmosphereSettings, m_frameNumber);

	{
		const auto push = MakeAtmosphereSkyPush(
			m_atmosphereState.Parameters(),
			m_profiler.atmosphereSkySettings);

		auto& uploadScene = World::GetScene().GetSceneData();

		uploadScene.atmosphereRayleigh = push.parameters.rayleigh;
		uploadScene.atmosphereMie = push.parameters.mie;
		uploadScene.atmosphereAbsorption = push.parameters.absorption;
		uploadScene.atmosphereGeometry = push.parameters.geometry;
		uploadScene.atmosphereIntegration = push.parameters.integration;

		uploadScene.atmospherePlacement = push.placement;
		uploadScene.atmosphereScattering = push.scattering;
		uploadScene.atmosphereSun = push.sun;
		uploadScene.atmosphereGround = glm::vec4(
			glm::clamp(glm::vec3(m_profiler.atmosphereSkySettings.ground), glm::vec3(0.0f), glm::vec3(1.0f)),
			std::max(m_profiler.atmosphereSkySettings.ground.w, 0.0f));
	}

	const bool worldProbeSSGIValid =
		frameCtx.IsTemporalValid() &&
		m_renderGraphState.InstancesActive() &&
		!m_renderGraphState.IsWireframeOn() &&
		debug.giMode == static_cast<uint32_t>(RD::GIMethod::VBGI);

	UpdateWorldProbes(rtDirty, worldProbeSSGIValid, rawDt);

	// ================================
	// Scene uniform buffer creation
	// ================================
	frameCtx.AssignSceneUniform(m_allocator.AllocateUniform(sceneData), m_allocator);

	// Vulkan requires a buffer created once its defined in used shader, even if that buffer isn't actually used.
	frameCtx.AssignCSMUniform(m_allocator.AllocateUniform(scene.GetCSMData()), m_allocator);
	frameCtx.AssignVolumetricShadowUniform(m_allocator.AllocateUniform(scene.GetVolumetricShadowInfo()), m_allocator);

	// ===========================
	// Frame update info finished
	// ===========================

	new (&m_renderPassExecutionContext) RenderPassExecutionContext
	{
		.commandBuffer = frameCtx.GetPrimaryCommandBuffer(), // placeholder
		.frameCtx = &frameCtx,
		.profiler = &m_profiler,
		.imageTable = &m_bindlessImageTable,
		.bufferTable = &m_globalAddressTable,
		.atmosphereState = &m_atmosphereState,
		.worldProbePush = &m_worldProbesPush,
		.scene = &scene,
		.frameState = &m_renderGraphState,
		.swapchain = &m_swapchain,
		.NRDReflect = &m_nrdReflectContext,
		.NRDShadow = &m_nrdShadowContext,
		.descriptors = m_descriptorManager.get(),
		.pipelines = m_pipelineManager.get()
	};

	m_renderGraph.SetAsyncComputeEnabled(m_profiler.enableAsyncCompute);

	m_renderGraph.Sync(m_renderGraphState, m_renderPassExecutionContext);

	const auto& schedule = m_renderGraph.GetSchedule();
	auto& async = m_profiler.asyncStats;

	async.bDedicatedQueue = m_renderGraph.HasDedicatedComputeQueue();
	async.bActiveThisFrame = schedule.bUsesAsyncCompute;
	async.graphicsBatchCount = schedule.graphicsBatchCount;
	async.asyncPassCount = static_cast<uint32_t>(schedule.Get(BatchId::C0).passes.size());
	async.overlapPassCount = static_cast<uint32_t>(schedule.Get(BatchId::G1).passes.size());
}


// =============================================
// Start of the frame
// === CLEAR DATA AND AQUIRE SWAPCHAIN INDEX ===
bool Renderer::PrepareFrame()
{
	auto& frameCtx = GetCurrentFrame();

	if (m_resize.IsPending()) return true;

	// Must always wait first
	auto fenceResult = m_swapchain.WaitOnInFlightFence(frameCtx.m_frameIndex);
	if (fenceResult == VK_ERROR_DEVICE_LOST) m_device->DumpDeviceState("Wait_On_In_Flight_Fence");

	const VkPipeline previousAtmospherePipeline =
		m_pipelineManager->GetHandle(
			RD::Renderer_Pipeline::AtmosphereTransmittance).pipeline;

	m_pipelineManager->TickFrame(
		m_device->GetContext().device, m_frameNumber, m_framesInFlight);

	m_shaderHotReload.Publish(*m_pipelineManager);

	const VkPipeline currentAtmospherePipeline =
		m_pipelineManager->GetHandle(
			RD::Renderer_Pipeline::AtmosphereTransmittance).pipeline;

	if (currentAtmospherePipeline != previousAtmospherePipeline)
	{
		m_atmosphereState.Invalidate();
	}

	m_device->ResetCrashMarkers(frameCtx.m_frameIndex);

	// Gpu timings
	if (m_device->GetGraphicsQueue().SupportsTimestamps() &&
		frameCtx.m_graphicsTimestampPool != VK_NULL_HANDLE &&
		frameCtx.m_bHasTimestampResultsPending)
	{
		auto results = m_device->GetGraphicsQueue().ReadTimestamps(
			frameCtx.m_graphicsTimestampPool,
			frameCtx.m_passTimestampRanges,
			frameCtx.m_timestampPassUsed,
			m_device->GetTimestampPeriod(),
			true);

		for (uint32_t passIndex = 0; passIndex < TIMESTAMP_PASS_COUNT; ++passIndex)
		{
			if (!results.passResults[passIndex].valid) continue;

			m_profiler.AddGpuPassTime(
				static_cast<RD::Renderer_Pass>(passIndex),
				results.passResults[passIndex].gpuMs);
		}

		if (results.frameResult.valid)
		{
			auto& stats = m_profiler.getStats();
			stats.gpuFrameTimeRawMs = results.frameResult.gpuMs;
			stats.gpuFrameTime.Add(results.frameResult.gpuMs);
		}
	}

	if (m_device->GetComputeQueue().SupportsTimestamps() &&
		frameCtx.m_computeTimestampPool != VK_NULL_HANDLE &&
		frameCtx.m_bHasComputeTimestampsPending.load(std::memory_order_relaxed))
	{
		auto results = m_device->GetComputeQueue().ReadTimestamps(
			frameCtx.m_computeTimestampPool,
			frameCtx.m_passTimestampRanges,
			frameCtx.m_timestampPassUsedCompute,
			m_device->GetTimestampPeriod(),
			false);

		for (uint32_t passIndex = 0; passIndex < TIMESTAMP_PASS_COUNT; ++passIndex)
		{
			if (!results.passResults[passIndex].valid) continue;

			m_profiler.AddGpuPassTime(
				static_cast<RD::Renderer_Pass>(passIndex),
				results.passResults[passIndex].gpuMs);
		}
	}

	if (frameCtx.m_graphicsTimestampPool != VK_NULL_HANDLE)
	{
		vkResetQueryPool(
			m_device->GetContext().device,
			frameCtx.m_graphicsTimestampPool,
			0u,
			TIMESTAMP_QUERY_COUNT);

		frameCtx.m_timestampPassUsed.fill(false);
		frameCtx.m_bHasTimestampResultsPending = false;
	}

	if (frameCtx.m_computeTimestampPool != VK_NULL_HANDLE)
	{
		vkResetQueryPool(
			m_device->GetContext().device,
			frameCtx.m_computeTimestampPool,
			0u,
			TIMESTAMP_QUERY_COUNT);

		frameCtx.m_timestampPassUsedCompute.fill(false);
		frameCtx.m_bHasComputeTimestampsPending.store(false, std::memory_order_relaxed);
	}

	// Next swapchain image
	auto swapResult = m_swapchain.AcquireNextImage(frameCtx.m_frameIndex);
	if (swapResult == VK_ERROR_DEVICE_LOST) m_device->DumpDeviceState("Acquire_Next_Image");

	// This condition should basically never occur
	if (swapResult == VK_ERROR_OUT_OF_DATE_KHR)
	{
		m_resize.Request(ResizeReason::AcquireOutOfDate);
		return true;
	}

	INVARIANT(swapResult == VK_SUCCESS || swapResult == VK_SUBOPTIMAL_KHR);

	// In use swapchain image
	m_swapchain.MarkInFlightFrameIndex(frameCtx.m_frameIndex);

	vmaSetCurrentFrameIndex(m_allocator.GetVma(), static_cast<uint32_t>(m_frameNumber));

	frameCtx.FreeStashedCmds(m_device->GetContext());

	// Primarly uniform buffer cleanup
	frameCtx.m_cpuDeletionQueue.Flush();

	// =================================
	// The safe zone now to do whatever
	// =================================

	const auto& debug = m_profiler.debugToggles;

	const bool wantsDebug =
		debug.showOpaqueOBBs ||
		debug.showTransparentOBBs;

	if (wantsDebug != frameCtx.m_bDebugLineRendering)
	{
		if (wantsDebug)
			frameCtx.CreateDebugBuffers(m_allocator);
		else
			frameCtx.DestroyDebugBuffers(m_allocator);

		frameCtx.m_bDebugLineRendering = wantsDebug;
	}

	if (frameCtx.DoesCachedExtentNeedUpdate(m_renderExtent.Width(), m_renderExtent.Height()))
	{
		frameCtx.CreateClusterBuffers(m_clusterBufferSizes, m_allocator);
		frameCtx.CreateRTRayListBuffer(m_rtRayListLayout, m_allocator);
	}

	frameCtx.SwapMeshletVisibility();

	// Handles initialization and any updates during runtime
	if (frameCtx.m_gpuAddressTable.IsTableDirty())
	{
		frameCtx.m_gpuAddressTable.UpdateCpuVersion();
	}

	if (m_profiler.debugToggles.enableProfilerView && frameCtx.m_statsMapped)
	{
		vmaInvalidateAllocation(m_allocator.GetVma(), frameCtx.m_statsReadback.m_allocation, 0, sizeof(GPUStats));
		m_profiler.gpuStats = *frameCtx.m_statsMapped;
	}

	if (m_luminanceMapped)
	{
		vmaInvalidateAllocation(m_allocator.GetVma(), m_luminanceReadbackBuffer.m_allocation, 0, sizeof(m_luminanceSums));
		m_luminanceSumsReadback = *m_luminanceMapped;
	}

	return m_resize.IsPending();
}

// ===============================================
// === SYNC FRAME SEMAPHORES AND PRESENT FRAME ===
bool Renderer::SubmitFrame()
{
	auto& frameCtx = GetCurrentFrame();

	const uint64_t transferWaitForG1 = frameCtx.transferWaitValue;

	auto& graphicsQ = m_device->GetGraphicsQueue();
	auto& transferQ = m_device->GetTransferQueue();
	auto& computeQ = m_device->GetComputeQueue();
	auto& presentQ = m_device->GetPresentQueue();

	VkSemaphore presentSem = m_swapchain.GetAvailableSemaphore();
	VkSemaphore renderSem = m_swapchain.GetFinishedSemaphore();
	VkFence     fence = m_swapchain.GetInFlightFence();

	const auto& schedule = m_renderGraph.GetSchedule();

	std::vector<VkSemaphoreSubmitInfo> firstBatchWaits;

	if (frameCtx.transferWaitValue != UINT64_MAX)
	{
		ASSERT(frameCtx.transferWaitValue <= transferQ.GetCurrentSignalValue(),
			"Transfer wait %llu ahead of signalled %llu.",
			frameCtx.transferWaitValue, transferQ.GetCurrentSignalValue());

		firstBatchWaits.emplace_back(TimelineWait(
			transferQ.GetTimelineSemaphore(),
			frameCtx.transferWaitValue,
			VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT));

		frameCtx.transferWaitValue = UINT64_MAX;
	}

	const VkPipelineStageFlags2 kAcquireStages =
		VK_PIPELINE_STAGE_2_BLIT_BIT |
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

	if (!schedule.bUsesAsyncCompute)
	{
		// ================= single submit =================
		ASSERT(schedule.graphicsBatchCount == 1u);

		firstBatchWaits.emplace_back(BinaryWait(presentSem, kAcquireStages));

		VK_CHECK(vkResetFences(
			m_device->GetContext().device,
			1,
			&fence));

		auto gfxResult = graphicsQ.SubmitFrame(
			firstBatchWaits,
			frameCtx.GetGraphicsPrimary(0u),
			renderSem,
			fence);

		if (gfxResult == VK_ERROR_DEVICE_LOST)
			m_device->DumpDeviceState("Graphics_Submit_Single");

		if (gfxResult == VK_SUCCESS)
		{
			m_atmosphereState.MarkSubmitted(m_frameNumber);
			OnWorldProbesSubmitted();
		}
		else
		{
			m_atmosphereState.MarkAborted();
			VK_CHECK(gfxResult);
		}
	}
	else
	{
		// ============ G0 -> C0 || G1 -> G2 ============
		ASSERT(schedule.graphicsBatchCount == MAX_GRAPHICS_PRIMARIES,
			"Async path expects exactly %u graphics batches, got %u.",
			MAX_GRAPHICS_PRIMARIES, schedule.graphicsBatchCount);

		// ---- G0 ----
		const uint64_t g0 = graphicsQ.AdvanceTimeline();
		{
			const VkSemaphoreSubmitInfo signals[] = {
				TimelineSignal(graphicsQ.GetTimelineSemaphore(), g0)
			};

			graphicsQ.Submit2(
				firstBatchWaits,
				frameCtx.GetGraphicsPrimary(0u),
				signals,
				VK_NULL_HANDLE);
		}

		// ---- C0 ----
		const uint64_t c0 = computeQ.AdvanceTimeline();
		{
			const VkSemaphoreSubmitInfo waits[] = {
				TimelineWait(
					graphicsQ.GetTimelineSemaphore(),
					g0,
					VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT)
			};

			const VkSemaphoreSubmitInfo signals[] = {
				TimelineSignal(
					computeQ.GetTimelineSemaphore(),
					c0,
					VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT)
			};

			computeQ.Submit2(
				waits,
				frameCtx.GetAsyncComputePrimary(),
				signals,
				VK_NULL_HANDLE);
		}

		// ---- G1 ----
		{
			std::vector<VkSemaphoreSubmitInfo> g1Waits;

			if (transferWaitForG1 != UINT64_MAX)
			{
				g1Waits.emplace_back(TimelineWait(
					transferQ.GetTimelineSemaphore(),
					transferWaitForG1,
					VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT));
			}

			graphicsQ.Submit2(
				g1Waits,
				frameCtx.GetGraphicsPrimary(1u),
				{},
				VK_NULL_HANDLE);
		}

		// ---- G2  ----
		{
			const VkSemaphoreSubmitInfo waits[] = {
				TimelineWait(
					computeQ.GetTimelineSemaphore(), c0,
					VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT),
				BinaryWait(presentSem, kAcquireStages)
			};

			VK_CHECK(vkResetFences(
				m_device->GetContext().device,
				1,
				&fence));

			auto gfxResult = graphicsQ.SubmitFrame(
				std::vector<VkSemaphoreSubmitInfo>(std::begin(waits), std::end(waits)),
				frameCtx.GetGraphicsPrimary(2u),
				renderSem,
				fence);

			if (gfxResult == VK_ERROR_DEVICE_LOST)
				m_device->DumpDeviceState("Graphics_Submit_Async");

			if (gfxResult == VK_SUCCESS)
			{
				m_atmosphereState.MarkSubmitted(m_frameNumber);
				OnWorldProbesSubmitted();
			}
			else
			{
				m_atmosphereState.MarkAborted();
				VK_CHECK(gfxResult);
			}
		}
	}

	// ---- present + resize handling: ----
	auto presentResult = presentQ.Present(
		m_swapchain.GetSwapchainHandle(),
		m_swapchain.GetCurrentSwapchainImageIndex(),
		renderSem);
	if (presentResult == VK_ERROR_DEVICE_LOST) m_device->DumpDeviceState("Swapchain_Present");

	// --- Checking resize events ---
	if (presentResult == VK_ERROR_OUT_OF_DATE_KHR)
	{
		m_resize.Request(ResizeReason::PresentOutOfDate);
		CheckCSMAtlasExtentUpdate();
		m_frameNumber++;
		return true;
	}

	INVARIANT(presentResult == VK_SUCCESS || presentResult == VK_SUBOPTIMAL_KHR);

	CheckCSMAtlasExtentUpdate();
	m_frameNumber++;
	return m_resize.IsPending();
}

void Renderer::TickVramUsage()
{
	if (m_profiler.debugToggles.enableProfilerView)
	{
		m_profiler.SetVRAMUsage(m_allocator.GetTotalVRAMUsage());
	}
}

bool Renderer::ResolveResize(Extents2D liveExtent)
{
	//RESIZE_TRACE("[Resize] enter phase={} reason={} coalesced={} live={}x{} draw={}x{} gen={}",
	//	ResizeCoordinator::ToString(m_resize.GetPhase()),
	//	ResizeCoordinator::ToString(m_resize.GetReason()),
	//	m_resize.GetCoalesced(),
	//	liveExtent.Width(), liveExtent.Height(),
	//	m_renderExtent.Width(), m_renderExtent.Height(),
	//	m_resize.GetGeneration());

	if (!m_resize.IsPending())
	{
		if (liveExtent.Width() == m_renderExtent.Width() &&
			liveExtent.Height() == m_renderExtent.Height())
			return true;

		RESIZE_TRACE("[Resize] extent mismatch, self-requesting");
		m_resize.Request(ResizeReason::WindowEvent);
	}

	if (!m_resize.CanApply(liveExtent))
	{
		RESIZE_TRACE("[Resize] BLOCKED phase={} live={}x{}",
			ResizeCoordinator::ToString(
				m_resize.GetPhase()), liveExtent.Width(), liveExtent.Height());
		return false;
	}

	m_resize.EnterDrain();
	RESIZE_TRACE("[Resize] drain begin");
	DrainFrameContexts();
	RESIZE_TRACE("[Resize] drain end");

	m_resize.EnterApply();
	RESIZE_TRACE("[Resize] apply begin -> {}x{}", liveExtent.Width(), liveExtent.Height());
	UpdateDisplayExtent(liveExtent);
	RESIZE_TRACE("[Resize] extent applied, rebuilding contexts");
	RebuildFrameContexts();
	RESIZE_TRACE("[Resize] rebuild end");

	m_resize.Complete(m_renderExtent);
	RESIZE_TRACE("[Resize] complete gen={} phase={} coalesced={}",
		m_resize.GetGeneration(),
		ResizeCoordinator::ToString(m_resize.GetPhase()),
		m_resize.GetCoalesced());

	ValidateExtentCoherence();
	RESIZE_TRACE("[Resize] validated");

	return true;
}

void Renderer::DrainFrameContexts()
{
	const auto& ctxDevice = m_device->GetContext();

	StallDevice();

	for (uint32_t i = 0; i < m_framesInFlight; ++i)
	{
		auto& frameCtx = m_frameContexts[i];

		frameCtx.FreeStashedCmds(ctxDevice);
		frameCtx.m_cpuDeletionQueue.Flush();

		frameCtx.InvalidateMeshletVisibility();

		frameCtx.ResetDrawExtentCache();

		frameCtx.m_bHasTimestampResultsPending = false;
		frameCtx.m_bHasComputeTimestampsPending.store(false, std::memory_order_relaxed);
		frameCtx.m_timestampPassUsed.fill(false);
		frameCtx.m_timestampPassUsedCompute.fill(false);

		if (frameCtx.m_graphicsTimestampPool != VK_NULL_HANDLE)
			vkResetQueryPool(ctxDevice.device, frameCtx.m_graphicsTimestampPool, 0u, TIMESTAMP_QUERY_COUNT);

		if (frameCtx.m_computeTimestampPool != VK_NULL_HANDLE)
			vkResetQueryPool(ctxDevice.device, frameCtx.m_computeTimestampPool, 0u, TIMESTAMP_QUERY_COUNT);

		frameCtx.transferWaitValue = UINT64_MAX;
	}
}

void Renderer::RebuildFrameContexts()
{
	for (uint32_t i = 0; i < m_framesInFlight; ++i)
	{
		auto& frameCtx = m_frameContexts[i];
		frameCtx.SetTemporalResult(false);
		frameCtx.SetHiZValidResult(false);
	}

	m_renderGraphState.UpdateTemporal(false, false);

	m_aoRanPreviousFrame = false;
	m_profiler.ssgiSettings.aoHistoryValid = 0u;
}

void Renderer::ValidateExtentCoherence()
{
	const uint32_t w = m_renderExtent.Width();
	const uint32_t h = m_renderExtent.Height();

	INVARIANT(w > 0u && h > 0u);

	if (!m_bindlessImageTable.IsShadowAtlasCached())
	{
		const auto& csmAtlas = m_bindlessImageTable.GetRenderTarget(
			RD::Renderer_RenderTarget::DirectionalCSMAtlas);
		const auto& csmWidth = World::GetScene().GetCSMAtlasWidth();
		const auto& csmHeight = World::GetScene().GetCSMAtlasHeight();

		ASSERT(csmAtlas.Width() == csmWidth,
			"CSM atlas is %u wide, scene believes %u.",
			csmAtlas.Width(), csmHeight);
	}

	ASSERT(m_rtRayListLayout.halfWidth == (w + 1u) / 2u &&
		m_rtRayListLayout.halfHeight == (h + 1u) / 2u,
		"RTRayListLayout %ux%u stale against extent %ux%u.",
		m_rtRayListLayout.halfWidth, m_rtRayListLayout.halfHeight, w, h);

	const auto& hdrScene = m_bindlessImageTable.GetRenderTarget(RD::Renderer_RenderTarget::HDRScene);
	ASSERT(hdrScene.Width() == w && hdrScene.Height() == h,
		"Opaque target %ux%u stale against extent %ux%u.",
		hdrScene.Width(), hdrScene.Height(), w, h);
}


void Renderer::UpdateDisplayExtent(Extents2D newWindowExtent)
{
	SetRenderExtent(newWindowExtent);

	SetDisplayExtent(newWindowExtent);

	const uint32_t width = m_renderExtent.Width();
	const uint32_t height = m_renderExtent.Height();

	RESIZE_TRACE("RESIZE before swapchain");

	m_swapchain.ResizeSwapchain(
		m_device->GetContext(),
		m_device->GetSurface(),
		m_device->GetSwapchainSupportDetails(),
		newWindowExtent);

	RESIZE_TRACE("RESIZE swapchain complete");

	ASSERT(
		m_swapchain.GetImageCount() == m_framesInFlight,
		"Swapchain image count changed on resize (%u -> %u); frame contexts are not sized for this.",
		m_framesInFlight,
		m_swapchain.GetImageCount());

	m_rtRayListLayout.Update(width, height);
	m_clusterBufferSizes.UpdateClusterBufferSizes(width, height);
	m_renderGraph.SetRenderExtent(m_renderExtent);
	m_renderGraph.SetDisplayExtent(m_displayExtent);

	m_bindlessImageTable.UpdateRenderTargets({ width, height, 1u }, m_allocator);

	{
		auto cmdGfxPool = m_device->GetThreadCommandPool(JobSystem::RENDER_THREAD, QueueType::Graphics);

		m_device->RecordDeferredCommand([&](VkCommandBuffer cmd)
			{
				m_bindlessImageTable.TransitionRenderTargetsFromUndefined(cmd);
			}, cmdGfxPool, QueueType::Graphics);

		m_device->SubmitDeferredCommands(QueueType::Graphics);
		m_device->GetGraphicsQueue().WaitIdle();
	}

	m_nrdReflectContext.Resize(
		m_allocator,
		{ (width + 1u) / 2u, (height + 1u) / 2u });

	m_nrdShadowContext.Resize(
		m_allocator,
		m_renderExtent);

	{
		auto cmdCompPool = m_device->GetThreadCommandPool(JobSystem::RENDER_THREAD, QueueType::Compute);

		m_device->RecordDeferredCommand([&](VkCommandBuffer cmd)
			{
				m_nrdReflectContext.RecordPoolInit(cmd);
				m_nrdShadowContext.RecordPoolInit(cmd);
			}, cmdCompPool, QueueType::Compute);

		m_device->SubmitDeferredCommands(QueueType::Compute);
		m_device->GetComputeQueue().WaitIdle();
	}

	m_renderGraph.InvalidateTrackedLayouts();

	m_renderGraph.NotifyLayout(
		RD::Renderer_RenderTarget::AtmosphereTransmittance,
		RD::ImageAccess::Undefined);

	m_atmosphereState.Invalidate();
}

void Renderer::TimestampPoolStart(FrameContext& frameCtx, VkCommandBuffer cmd)
{
	if (!m_device->GetGraphicsQueue().SupportsTimestamps()) return;
	if (frameCtx.m_graphicsTimestampPool == VK_NULL_HANDLE) return;

	vkCmdWriteTimestamp2(
		cmd,
		VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
		frameCtx.m_graphicsTimestampPool,
		FRAME_BEGIN_QUERY);
}

void Renderer::TimestampPoolEnd(FrameContext& frameCtx, VkCommandBuffer cmd)
{
	if (m_device->GetGraphicsQueue().SupportsTimestamps())
	{
		vkCmdWriteTimestamp2(
			cmd,
			VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
			frameCtx.m_graphicsTimestampPool,
			FRAME_END_QUERY);
	}

	m_profiler.CollectTracyGraphics(cmd);

	if (m_device->GetGraphicsQueue().SupportsTimestamps() &&
		frameCtx.m_graphicsTimestampPool != VK_NULL_HANDLE)
	{
		frameCtx.m_bHasTimestampResultsPending = true;
	}
}

void Renderer::BarrierDynamicBuffers(FrameContext& frameCtx, VkCommandBuffer cmd)
{
	auto& addrTable = frameCtx.m_gpuAddressTable;

	if (frameCtx.m_bTransformsBufferUploadNeeded)
	{
		if (frameCtx.IsTemporalValid() && m_renderGraphState.IsTaaOn())
		{
			BufferBarriers::TransferWriteToComputeRead(
				cmd,
				addrTable.GetGPUBuffer(RD::Renderer_Buffer::MotionMatrices),
				m_device->GetContext());
		}

		BufferBarriers::TransferWriteToComputeRead(
			cmd,
			addrTable.GetGPUBuffer(RD::Renderer_Buffer::DynamicTransforms),
			m_device->GetContext());

		frameCtx.ClearTransformsUploadFlag();
	}

	if (frameCtx.m_bLightsBufferUploadNeeded)
	{
		BufferBarriers::TransferWriteToComputeRead(
			cmd,
			addrTable.GetGPUBuffer(RD::Renderer_Buffer::Lights),
			m_device->GetContext());

		frameCtx.ClearLightsUploadFlag();
	}

	if (frameCtx.m_bInstanceInputUploadNeeded)
	{
		BufferBarriers::TransferWriteToComputeRead(
			cmd,
			m_globalAddressTable.GetGPUBuffer(RD::Renderer_Buffer::InstanceInputs),
			m_device->GetContext());

		BufferBarriers::TransferWriteToComputeRead(
			cmd,
			m_globalAddressTable.GetGPUBuffer(RD::Renderer_Buffer::RTRows),
			m_device->GetContext());

		BufferBarriers::TransferWriteToComputeRead(
			cmd,
			m_globalAddressTable.GetGPUBuffer(RD::Renderer_Buffer::DrawBinKeys),
			m_device->GetContext());

		BufferBarriers::TransferWriteToComputeRead(
			cmd,
			m_globalAddressTable.GetTableBuffer(),
			m_device->GetContext());

		frameCtx.ClearInstanceInputUploadFlag();
	}

	if (frameCtx.m_gpuAddressTable.IsTableDirty())
	{
		BufferBarriers::TransferWriteToComputeRead(
			cmd,
			frameCtx.m_gpuAddressTable.GetTableBuffer(),
			m_device->GetContext());

		frameCtx.m_gpuAddressTable.ClearDirty();
	}

	if (frameCtx.IsLuminanceResetNeeded())
	{
		BufferBarriers::TransferWriteToComputeWrite(
			cmd,
			m_globalAddressTable.GetGPUBuffer(RD::Renderer_Buffer::Luminance),
			m_device->GetContext());

		frameCtx.ClearLuminanceResetFlag();
	}
}

void Renderer::RecordRenderCommand(JobSystem& jobSystem)
{
	ValidateExtentCoherence();

	auto& frameCtx = GetCurrentFrame();
	auto& frameAddrTable = frameCtx.m_gpuAddressTable;

	CheckGlobalDescriptorSetSync();

	// Catastrophic if version mismatch
	frameAddrTable.IsVersionMismatched();

	// Frame descriptor updates
	frameCtx.TickDescriptorWrites(m_mainWriter);
	m_mainWriter.UpdateSet(m_device->GetContext().device, frameCtx.m_frameSet);

	const bool bBindIndexBuffer =
		m_profiler.assetCounts.totalIndexCount > 0 &&
		m_renderGraphState.InstancesActive() &&
		!World::_loadedScenes.empty();

	RecordHooks hooks;

	m_checkpointPassCounter.store(0, std::memory_order_relaxed);

	hooks.onFrameBegin = [&](VkCommandBuffer cmd)
		{
			m_device->SetCheckpoint(cmd, "Frame_Begin");
			TimestampPoolStart(frameCtx, cmd);
			BarrierDynamicBuffers(frameCtx, cmd);
		};

	hooks.onFrameEnd = [&](VkCommandBuffer cmd)
		{
			TimestampPoolEnd(frameCtx, cmd);
			m_device->SetCheckpoint(cmd, "Frame_End");
		};

	hooks.onAsyncBatchEnd = [&](VkCommandBuffer cmd)
		{
			m_profiler.CollectTracyCompute(cmd);
			m_device->SetCheckpoint(cmd, "Async_Batch_End");
		};

	hooks.bindPrologue = [&](VkCommandBuffer cmd, PassQueue queue)
		{
			const uint32_t idx = m_checkpointPassCounter.fetch_add(1, std::memory_order_relaxed);
			const QueueType qType =
				(queue == PassQueue::Graphics) ? QueueType::Graphics : QueueType::Compute;

			m_device->MarkPassBegin(cmd, qType, frameCtx.m_frameIndex, idx);
			m_device->MarkPassEnd(cmd, qType, frameCtx.m_frameIndex, idx);

			if (queue == PassQueue::Graphics)
			{
				m_descriptorManager->BindDescriptorSetsGraphics(
					cmd,
					frameCtx.m_frameSet,
					m_pipelineManager->GetGlobalLayout());

				m_descriptorManager->BindDescriptorSetsCompute(
					cmd,
					frameCtx.m_frameSet,
					m_pipelineManager->GetGlobalLayout());
			}
			else
			{
				m_descriptorManager->BindDescriptorSetsCompute(
					cmd,
					frameCtx.m_frameSet,
					m_pipelineManager->GetGlobalLayout());
			}

			if (queue == PassQueue::Graphics && bBindIndexBuffer)
			{
				const auto indexBuffer =
					m_globalAddressTable.GetGPUBuffer(RD::Renderer_Buffer::Index).m_buffer;
				vkCmdBindIndexBuffer(cmd, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
			}
		};

	m_renderGraph.RecordFrame(
		m_renderPassExecutionContext,
		jobSystem,
		frameCtx,
		hooks);
}

void Renderer::UpdateShadowMode()
{
	// Physical CSM residency follows the CURRENT requested shadow mode,
	// not the previous frame's RenderStateInfo.
	const bool wantsRTShadows =
		m_profiler.debugToggles.enableShadows != 0u &&
		m_profiler.debugToggles.sunShadowFilter ==
		static_cast<uint32_t>(RD::SunShadowFilter::RT_SOFT);

	const bool csmAtlasCached = m_bindlessImageTable.IsShadowAtlasCached();

	if (wantsRTShadows && !csmAtlasCached)
	{
		StallDevice();

		m_bindlessImageTable.FreeCSMAtlas(m_allocator);

		m_renderGraph.NotifyLayout(
			RD::Renderer_RenderTarget::DirectionalCSMAtlas,
			RD::ImageAccess::Undefined);
	}
	else if (!wantsRTShadows && csmAtlasCached)
	{
		StallDevice();

		m_bindlessImageTable.RecreateCSMAtlas(m_allocator);

		m_renderGraph.NotifyLayout(
			RD::Renderer_RenderTarget::DirectionalCSMAtlas,
			RD::ImageAccess::Undefined);

		const auto& csmAtlas = m_bindlessImageTable.GetRenderTarget(
			RD::Renderer_RenderTarget::DirectionalCSMAtlas);
		World::GetScene().InitCSMInfo(csmAtlas.Width(), csmAtlas.Height(), csmAtlas.m_bindlessID);
	}

	// Now expose the FINAL physical state for this frame.
	m_profiler.debugToggles.csmAtlasCached = m_bindlessImageTable.IsShadowAtlasCached();
}

void Renderer::StallDevice()
{
	const VkResult result = vkDeviceWaitIdle(m_device->GetContext().device);

	if (result == VK_ERROR_DEVICE_LOST) m_device->DumpDeviceState("Stall_Device");

	VK_CHECK(result);
}

void Renderer::FreeAllAssetTextures()
{
	const auto span = m_bindlessImageTable.GetAssetTextureSpan();
	for (uint32_t i = 0; i < static_cast<uint32_t>(span.size()); ++i)
	{
		if (!span[i].IsValid()) continue;
		AllocatedImage& img = m_bindlessImageTable.GetAssetTextureMutable(i);
		m_allocator.FreeImage(img);
		m_bindlessImageTable.FreeAssetTexture(i);
	}
}

void Renderer::UnloadAllScenes()
{
	FreeAllAssetTextures();
	m_registeredMeshes = MeshRegistry{};
	m_materials.clear();
	m_materialFlagsIDs.clear();
	m_blasAddresses.clear();
	m_globalAddressTable.ClearAssetBuffers(m_allocator);
	m_profiler.assetCounts.Clear();

	fmt::println("[Renderer] All scenes unloaded.");
}

void Renderer::Cleanup()
{
	LightingSystem::Cleanup();
	World::Cleanup();

#ifdef TRACY_ENABLE
	m_profiler.ShutdownTracyGPU();
#endif

	DestroyRenderGraph();

	m_nrdReflectContext.Shutdown(m_device->GetContext().device, m_allocator);
	m_nrdShadowContext.Shutdown(m_device->GetContext().device, m_allocator);

	for (auto as : m_blasHandles)
		vkDestroyAccelerationStructureKHR(m_device->GetContext().device, as, nullptr);
	m_blasHandles.clear();
	m_allocator.FreeBuffer(m_blasStorage);

	m_allocator.FreeBuffer(m_atmosphereResources_UBO);

	if (m_luminanceMapped)
	{
		vmaUnmapMemory(m_allocator.GetVma(), m_luminanceReadbackBuffer.m_allocation);
		m_luminanceMapped = nullptr;
	}
	m_allocator.FreeBuffer(m_luminanceReadbackBuffer);

	m_bindlessImageTable.Shutdown(m_device->GetContext().device, m_allocator);
	m_globalAddressTable.Shutdown(m_allocator);

	CleanupFrameResources();

	m_descriptorManager->CleanupDescriptors(m_device->GetContext().device);
	m_shaderHotReload.Shutdown();
	m_pipelineManager->Shutdown(m_device->GetContext().device);

	m_allocator.Shutdown();
	m_swapchain.Cleanup();
	m_device->Cleanup();
}
