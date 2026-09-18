#pragma once

#include "backend/memory/ResourceAllocator.h"
#include "renderer/backend/descriptors/DescriptorWriter.h"
#include "backend/memory/BindlessBDATable.h"
#include "backend/memory/BindlessImageTable.h"
#include "backend/Swapchain.h"
#include "backend/nrd/NRDContext.h"
#include "frame/FrameContext.h"
#include "frame/ResizeCoordinator.h"
#include "Material.h"
#include "Mesh.h"
#include "RendererDefinitions.h"
#include "profiler/Profiler.h"
#include "rendergraph/RenderGraph.h"
#include "rendergraph/RenderGraphResources.h"
#include "backend/shaders/ShaderCache.h"
#include "backend/shaders/ShaderHotReload.h"
#include "scene/AtmosphereState.h"
#include "scene/WorldProbeTypes.h"

namespace RD = RendererDefinitions;

class PipelineManager;
class DescriptorManager;
class Device;
class Window;
class JobSystem;
struct FrameStats;
class Scene;
struct GLFWwindow;
struct SceneUploadBatch;
struct ModelAsset;

// Manages core vulkan state and memory allocation.
// All primary resources are stored here, gpu buffers, image, frame data.
// Bindless textures, push descriptors for render targets, indirect buffer table for bindless gpu buffers.
class Renderer
{
public:
	void Init(
		const Window& window,
		JobSystem& jobSystem);
	void Cleanup();

	void StallDevice();

	bool ShouldRenderImgui() const noexcept
	{
		const auto& debug = m_profiler.debugToggles;
		return debug.enableProfilerView || debug.enableSettings;
	}

	void UploadScenes(std::vector<SceneUploadBatch>&& batches);
	void UnloadAllScenes();

	void RecordRenderCommand(JobSystem& jobSystem);

	bool PrepareFrame(); // Returns false if no resize occured
	bool SubmitFrame();

	void BeginFrameTimer() { m_profiler.BeginFrame(); }
	void EndFrameTimer() { m_profiler.EndFrame(); }

	Profiler& GetProfiler() { return m_profiler; }
	RD::RenderToggles& GetRenderToggles() { return m_profiler.debugToggles; }
	FrameStats& GetFrameStats() { return m_profiler.getStats(); }

	PipelineManager& GetPipelineManager() { return *m_pipelineManager; }

	const DescriptorManager& GetDescriptorManager() { return *m_descriptorManager; }
	const Device& GetDevice() { return *m_device; }
	const Swapchain& GetSwapchain() { return m_swapchain; }

	// Updates swapchain size and any resources that depend on draw extent size.
	void UpdateDisplayExtent(Extents2D newWindowExtent);

	void TickVramUsage();

	Extents2D GetRenderExtent() const { return m_renderExtent; }

	uint32_t GetFrameNumber() const { return m_frameNumber; }
	bool IsFirstFrame() const noexcept { return m_frameNumber == 0; }

	const std::vector<uint32_t>& GetMaterialFlagsById() { return m_materialFlagsIDs; }
	const std::vector<uint64_t>& GetBlasAddresses() { return m_blasAddresses; }

	void UpdateRendererContext(GLFWwindow* window);

	void StartTimer() { m_profiler.StartTimer(); }
	void EndAssetTimer();
	void EndSceneUpdateTimer() { m_profiler.getStats().sceneUpdateTime.Add(m_profiler.EndTimerMS()); }
	void EndDrawTimer() { m_profiler.getStats().drawTime.Add(m_profiler.EndTimerMS()); }

	void RequestResize(ResizeReason reason) { m_resize.Request(reason); }
	bool IsResizePending() const { return m_resize.IsPending(); }
	uint64_t GetResizeGeneration() const { return m_resize.GetGeneration(); }

	// Returns false when the renderer is not presentable this frame.
	bool ResolveResize(Extents2D liveExtent);

	ShaderCache& GetShaderCache() { return m_shaderCache; }
	ShaderHotReload& GetShaderHotReload() { return m_shaderHotReload; }

	float GetAdaptedEV100() const;

	void RequestWorldProbeReset() noexcept;
	void OnWorldProbesSubmitted() noexcept;

	const WorldProbeHeader& GetWorldProbeHeader() const noexcept
	{
		return m_worldProbesHeader;
	}

private:
	uint32_t m_frameNumber = 0;
	uint32_t m_framesInFlight = 0;

	Extents2D m_renderExtent;
	Extents2D m_displayExtent;
	void SetRenderExtent(Extents2D extent) { m_renderExtent = extent; }
	void SetDisplayExtent(Extents2D extent) { m_displayExtent = extent; }

	RenderPassExecutionContext m_renderPassExecutionContext;
	RD::RenderStateInfo m_renderGraphState;
	RenderGraph m_renderGraph;

	void CreateRenderGraph();
	void DestroyRenderGraph();

	void InitFrameResources(JobSystem& jobSystem);
	void CleanupFrameResources();

	void CheckGlobalDescriptorSetSync();

	void CheckCSMAtlasExtentUpdate();

	void UpdateShadowMode();

	void BatchUploadTextures(
		std::vector<SceneUploadBatch>& batches,
		std::vector<std::shared_ptr<ModelAsset>>& assets,
		VkCommandBuffer cmd);
	void BatchUploadMeshes(
		std::vector<SceneUploadBatch>& batches,
		std::vector<std::shared_ptr<ModelAsset>>& assets,
		VkCommandBuffer cmd);
	void BatchUploadMaterials(
		std::vector<SceneUploadBatch>& batches,
		std::vector<std::shared_ptr<ModelAsset>>& assets);

	std::vector<uint64_t> BuildMeshBLAS(VkCommandBuffer cmd, AllocatedBuffer& outScratch);

	void FreeAllAssetTextures();

	void TimestampPoolStart(FrameContext& frameCtx, VkCommandBuffer cmd);
	void TimestampPoolEnd(FrameContext& frameCtx, VkCommandBuffer cmd);

	void BarrierDynamicBuffers(FrameContext& frameCtx, VkCommandBuffer cmd);

	FrameContext& GetCurrentFrame()
	{
		return m_frameContexts[m_frameNumber % m_framesInFlight];
	}

	FrameContext& GetLastFrame()
	{
		uint32_t lastFrameNumber = m_frameNumber + m_framesInFlight - 1u;
		return m_frameContexts[lastFrameNumber % m_framesInFlight];
	}

	// Will vary, vsync is 2 contexts. When vsync off its 3.
	std::array<FrameContext, RD::MAX_FRAMES_IN_FLIGHT> m_frameContexts;

	BindlessBDATable m_globalAddressTable;
	std::vector<Material> m_materials;
	MeshRegistry m_registeredMeshes;
	BinTableBuild m_drawBinTableBuild;

	BindlessImageTable m_bindlessImageTable;

	std::vector<uint32_t> m_materialFlagsIDs;

	DescriptorWriter m_mainWriter;

	ShaderCache     m_shaderCache;
	ShaderHotReload m_shaderHotReload;

	std::unique_ptr<DescriptorManager> m_descriptorManager;
	std::unique_ptr<PipelineManager> m_pipelineManager;
	std::unique_ptr<Device> m_device;
	Swapchain m_swapchain;
	Allocator m_allocator;

	ClusterBufferSizes m_clusterBufferSizes;
	RTRayListLayout m_rtRayListLayout;

	NRDContext m_nrdReflectContext;
	NRDContext m_nrdShadowContext;

	AllocatedBuffer m_blasStorage;
	std::vector<VkAccelerationStructureKHR> m_blasHandles;

	std::vector<uint64_t> m_blasAddresses;

	Profiler m_profiler;

	RD::ShadowQuality m_currentShadowQuality;

	void InitRenderSettings(
		bool enableLensFlare,
		bool enableChromaticAberration,
		bool enableBloom,
		bool enableShadows,
		bool enableSSS,
		bool enableVolumetrics,
		bool enableRTReflections,
		RD::AntiAliasingMethod aaMode,
		RD::GIMethod giMode,
		RD::ShadowQuality shadowQuality,
		RD::SunShadowFilter sunShadowFilter,
		bool enableProfilerView,
		bool enableSettings);

	void ApplyPushConstantDefaults();

	ResizeCoordinator m_resize;

	void DrainFrameContexts();
	void RebuildFrameContexts();
	void ValidateExtentCoherence();

	// Must always initialize to read states
	bool m_bRenderTargetsLayoutsTransitioned = false;

	uint32_t m_activeEnvSet = UINT32_MAX;

	void RequestLuminanceReset() noexcept { m_bLuminanceResetPending = true; }
	bool m_bLuminanceResetPending = true;
	glm::vec4 m_defaultLuminance{};

	std::array<glm::vec4, RD::MAX_LUMINANCE_GROUPS> m_luminanceSums = { glm::vec4(0.0f) };
	glm::vec3 m_shIrradiance[RD::MAX_ENVIRONMENT_SETS] = { glm::vec3(0.0f) };

	std::atomic<uint32_t> m_checkpointPassCounter{ 0 };

	uint32_t m_lastManualExposure = 0;

	AllocatedBuffer m_luminanceReadbackBuffer;
	const std::array<glm::vec4, RD::MAX_LUMINANCE_GROUPS>* m_luminanceMapped = nullptr;
	std::array<glm::vec4, RD::MAX_LUMINANCE_GROUPS> m_luminanceSumsReadback{};

	AllocatedBuffer m_atmosphereResources_UBO;
	AtmosphereState m_atmosphereState;

	void UpdateWorldProbes(
		bool geometryDirty,
		bool ssgiValid,
		float deltaSeconds);

	WorldProbeHeader m_worldProbesHeader{};
	WorldProbePush m_worldProbesPush{};
	WorldProbeRuntimeState m_worldProbesState{};

	bool m_aoRanPreviousFrame = false;
};
