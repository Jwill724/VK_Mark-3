#pragma once

#include "renderer/backend/VulkanForward.h"
#include "profiler/ProfilerTypes.h"
#include "renderer/RendererDefinitions.h"
#include "renderer/frame/FrameResources.h"
#include "renderer/rendergraph/RenderGraphSchedule.h"
#include "renderer/scene/AtmosphereTypes.h"
#include "renderer/scene/WorldProbeTypes.h"

#include <array>
#include <mutex>
#include <string>
#include <vector>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#include <tracy/TracyVulkan.hpp>
#endif

namespace RD = RendererDefinitions;

class FrameContext;
class Device;

class Profiler
{
public:
	class ScopedPass
	{
	public:
		ScopedPass() = default;

		ScopedPass(
			Profiler&         profiler,
			FrameContext&     frameCtx,
			VkCommandBuffer   cmd,
			RD::Renderer_Pass trackingID,
			std::string_view  passName,
			uint32_t          threadSlot = 0u,
			PassQueue         queue      = PassQueue::Graphics);

		ScopedPass(const ScopedPass&)            = delete;
		ScopedPass& operator=(const ScopedPass&) = delete;

		ScopedPass(ScopedPass&&) noexcept;
		ScopedPass& operator=(ScopedPass&&) noexcept;

		~ScopedPass();

	private:
		Profiler*         m_profiler      = nullptr;
		FrameContext*     m_frameCtx      = nullptr;
		VkCommandBuffer   m_cmd           = VK_NULL_HANDLE;
		RD::Renderer_Pass m_trackingID    = RD::Renderer_Pass::Count;
		void*             m_gpuZone       = nullptr;
		int64_t           m_cpuStartTicks = 0;

		VkQueryPool       m_timestampPool = VK_NULL_HANDLE;

		bool              m_bTimestampWritten = false;

		bool              m_bLabelBegun = false;

		uint32_t          m_threadSlot = 0u;
		PassQueue         m_queue      = PassQueue::Graphics;
	};

	Profiler();
	~Profiler();

	void BeginFrame();
	void EndFrame();

	[[nodiscard]] ScopedPass ProfilePass(
		FrameContext&     frameCtx,
		VkCommandBuffer   cmd,
		RD::Renderer_Pass trackingID,
		std::string_view  passName,
		uint32_t          threadSlot = 0u,
		PassQueue         queue      = PassQueue::Graphics);

	void AddGpuPassTime(RD::Renderer_Pass trackingID, float milliseconds);

	void ResetPassStats();

	const std::array<PassTimingStats, RD::PASS_COUNT>& GetAllPassStats() const { return m_passStats; }
	const PassTimingStats& GetPassStats(RD::Renderer_Pass trackingID) const;
	PassTimingStats&       GetPassStats(RD::Renderer_Pass trackingID);

	const char* GetPassName(RD::Renderer_Pass trackingID) const
	{
		return m_passStats[static_cast<size_t>(trackingID)].name.c_str();
	}

	bool IsPassActive(RD::Renderer_Pass trackingID) const noexcept
	{
		return m_passStats[static_cast<size_t>(trackingID)].activeThisFrame;
	}

	void SetDevice(const Device* device) noexcept { m_device = device; }

	void InitTracyGraphics(
		VkPhysicalDevice physicalDevice,
		VkDevice         device,
		VkQueue          queue,
		VkCommandBuffer  cmd);

	void InitTracyCompute(
		VkPhysicalDevice physicalDevice,
		VkDevice         device,
		VkQueue          queue,
		VkCommandBuffer  cmd,
		uint32_t         threadSlotCount);

	void CollectTracyGraphics(VkCommandBuffer cmd);

	void CollectTracyCompute(VkCommandBuffer cmd);

	void ShutdownTracyGPU();

	bool IsTracyGraphicsActive() const noexcept;
	bool IsTracyComputeActive()  const noexcept;
	bool IsTracyCompiledIn()     const;

	VkCommandBuffer GetTracyGraphicsCmd() const            { return m_tracyGraphicsCmdBuffer; }
	void            SetTracyGraphicsCmd(VkCommandBuffer c) { m_tracyGraphicsCmdBuffer = c; }

	VkCommandBuffer GetTracyComputeCmd() const             { return m_tracyComputeCmdBuffer; }
	void            SetTracyComputeCmd(VkCommandBuffer c)  { m_tracyComputeCmdBuffer = c; }

	void  StartTimer();
	float EndTimerMS()  const;
	float EndTimerSec() const;

	FrameStats&       getStats();
	const FrameStats& getStats() const;

	void SetGPUName(std::string name)  { m_stats.gpuName = std::move(name); }
	void SetVRAMUsage(VRAMStats stats) { m_stats.vramStats = stats; }

	void EnablePlatformTimerPrecision();
	void DisablePlatformTimerPrecision();

	bool rendererWasStalled = false;

	glm::vec3  cameraPos{};
	std::mutex camMutex;
	bool enableShaderEditor = false;
	bool enableWireframeView = false;
	bool enableAsyncCompute = true;
	bool enableSharpening = true;
	AsyncComputeStats asyncStats;

	RD::ShadowQuality shadowQuality;
	TotalAssetDataCounts assetCounts;
	RD::RenderToggles   debugToggles;

	// TODO: Should generalize this better.
	SSGIPush            ssgiSettings{};
	TAAPush             taaSettings{};
	FroxelPush          froxelSettings{};
	CompositePush       compositePush{};
	LensFlarePush       lensFlareSettings{};
	SSSPush             contactShadowsSettings{};
	LumaExposurePush    lumaExposureSettings{};
	BloomPush           bloomPush{};
	ChromaticAberrationPush caPush{};
	ForwardPush         forwardPush{};
	BindlessAccessPush  smaaTexturesIds{};
	ReflectPush         reflectPush{};
	NRDPush             nrdReflectPush{};
	NRDPush             nrdShadowPush{};
	RTShadowPush        rtShadowPush{};
	CASPush             casSettings{};
	AtmosphereSettings  atmosphereSettings{};
	AtmosphereSkySettings atmosphereSkySettings{};
	WorldProbeSettings worldProbeSettings{};

	GPUStats            gpuStats;

private:
	void* BeginTracyGpuZone(
		VkCommandBuffer   cmd,
		RD::Renderer_Pass trackingID,
		uint32_t          threadSlot,
		PassQueue         queue);

	void  EndTracyGpuZone(void* zone);

#ifdef TRACY_ENABLE
	struct TracyPassEntry
	{
		std::string name;
		std::unique_ptr<tracy::SourceLocationData> srcLoc;
	};

	std::array<TracyPassEntry, RD::PASS_COUNT> m_tracySourceLocations{};
#endif

	std::array<PassTimingStats, RD::PASS_COUNT> m_passStats{};

	VkCommandBuffer m_tracyGraphicsCmdBuffer = VK_NULL_HANDLE;
	VkCommandBuffer m_tracyComputeCmdBuffer  = VK_NULL_HANDLE;

	int64_t m_qpcFrequency     = 0;
	int64_t m_framePeriodTicks = 0;
	int64_t m_nextFrameTick    = 0;
	int64_t m_startTimerTick   = 0;

	double m_qpcInverse     = 0.0;
	double m_frameStartTime = 0.0;
	double m_lastFrameTime  = 0.0;

	void*              m_tracyGraphicsContext = nullptr;
	std::vector<void*> m_tracyComputeContexts;

	const Device* m_device = nullptr;

	FrameStats m_stats{};
};
