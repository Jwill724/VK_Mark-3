#include "pch.h"

#include "Profiler.h"
#include "renderer/Frame/FrameContext.h"
#include "../renderer/backend/VulkanTypes.h"
#include "../renderer/backend/Device.h"

// -----------------------------------------------------------------------------
// Internal helpers
// -----------------------------------------------------------------------------
namespace
{
	static int64_t queryPerformanceCounterTicks()
	{
		LARGE_INTEGER counter{};
		QueryPerformanceCounter(&counter);
		return counter.QuadPart;
	}

	static int64_t queryPerformanceFrequencyTicks()
	{
		LARGE_INTEGER frequency{};
		QueryPerformanceFrequency(&frequency);
		return frequency.QuadPart;
	}

	constexpr float kLabelGraphics[4] = { 0.35f, 0.62f, 0.90f, 1.0f };
	constexpr float kLabelAsync[4] = { 0.95f, 0.65f, 0.25f, 1.0f };
}

// -----------------------------------------------------------------------------
// Profiler lifetime
// -----------------------------------------------------------------------------
Profiler::Profiler()
{
	EnablePlatformTimerPrecision();
	ResetPassStats();
}

Profiler::~Profiler()
{
	ShutdownTracyGPU();
	DisablePlatformTimerPrecision();
}

void Profiler::EnablePlatformTimerPrecision()
{
	timeBeginPeriod(1);
	m_qpcFrequency = queryPerformanceFrequencyTicks();
	m_qpcInverse   = 1.0 / static_cast<double>(m_qpcFrequency);
}

void Profiler::DisablePlatformTimerPrecision()
{
	timeEndPeriod(1);
}

// -----------------------------------------------------------------------------
// Frame lifecycle
// -----------------------------------------------------------------------------
void Profiler::BeginFrame()
{
	ResetPassStats();

#ifdef TRACY_ENABLE
	FrameMarkStart("Renderer Frame");
#endif

	if (m_stats.capFramerate && m_stats.targetFrameRate > 0.0f)
	{
		m_framePeriodTicks = llround(
			static_cast<double>(m_qpcFrequency) / static_cast<double>(m_stats.targetFrameRate)
		);

		if (m_nextFrameTick == 0)
		{
			m_nextFrameTick = queryPerformanceCounterTicks() + m_framePeriodTicks;
		}
	}

	const int64_t nowTicks  = queryPerformanceCounterTicks();
	m_frameStartTime        = static_cast<double>(nowTicks) * m_qpcInverse;

	const double deltaSeconds       = m_frameStartTime - m_lastFrameTime;
	m_lastFrameTime                 = m_frameStartTime;

	m_stats.deltaSecondsRaw = static_cast<float>(std::clamp(deltaSeconds, 0.001, 0.1));

	m_stats.deltaTime.Add(m_stats.deltaSecondsRaw);

	rendererWasStalled = (deltaSeconds > 0.05);
}

void Profiler::EndFrame()
{
	const int64_t renderEndTicks  = queryPerformanceCounterTicks();
	const double  renderEndTime   = static_cast<double>(renderEndTicks) * m_qpcInverse;

	m_stats.frameTimeRawMs = static_cast<float>(renderEndTime - m_frameStartTime) * 1000.0f;
	m_stats.frameTimeRaw.Add(m_stats.frameTimeRawMs);

	const bool capEnabled = (m_stats.capFramerate && m_stats.targetFrameRate > 0.0f);

	if (capEnabled)
	{
		int64_t nowTicks = renderEndTicks;

		const int64_t twoMsTicks = m_qpcFrequency / 500;
		const int64_t oneMsTicks = m_qpcFrequency / 1000;

		const int64_t earlyTicks = m_nextFrameTick - nowTicks;

		if (earlyTicks > twoMsTicks)
		{
			const int64_t sleepTicks = earlyTicks - oneMsTicks;

			if (sleepTicks > 0)
			{
				const DWORD sleepMs = static_cast<DWORD>((sleepTicks * 1000) / m_qpcFrequency);
				if (sleepMs > 0)
					Sleep(sleepMs);

				nowTicks = queryPerformanceCounterTicks();
			}
		}

		if (nowTicks >= m_nextFrameTick)
		{
			m_nextFrameTick = nowTicks + m_framePeriodTicks;
		}
		else
		{
			do {
				_mm_pause();
				nowTicks = queryPerformanceCounterTicks();
			} while (nowTicks < m_nextFrameTick);

			m_nextFrameTick += m_framePeriodTicks;
		}
	}

	const int64_t presentedTicks = queryPerformanceCounterTicks();
	const double  presentedTime  = static_cast<double>(presentedTicks) * m_qpcInverse;

	float displayedSeconds = static_cast<float>(presentedTime - m_frameStartTime);

	if (capEnabled)
	{
		const float targetSeconds = 1.0f / m_stats.targetFrameRate;
		if (displayedSeconds < targetSeconds * 0.999f)
			displayedSeconds = targetSeconds;
	}

	m_stats.frameTime.Add(displayedSeconds * 1000.0f);
	m_stats.fps.Add(1.0f / std::max(displayedSeconds, 0.00001f));

#ifdef TRACY_ENABLE
	FrameMarkEnd("Renderer Frame");
#endif
}

Profiler::ScopedPass::ScopedPass(
	Profiler&         profiler,
	FrameContext&     frameCtx,
	VkCommandBuffer   cmd,
	RD::Renderer_Pass ID,
	std::string_view  passName,
	uint32_t          threadSlot,
	PassQueue         queue)
	: m_profiler(&profiler)
	, m_frameCtx(&frameCtx)
	, m_cmd(cmd)
	, m_trackingID(ID)
	, m_threadSlot(threadSlot)
	, m_queue(queue)
{
	const size_t idx = static_cast<size_t>(ID);

	// Distinct index per concurrent pass — no race between workers.
	auto& stats               = m_profiler->m_passStats[idx];
	stats.activeThisFrame     = true;
	stats.name                = passName;
	stats.asyncQueueThisFrame = (queue == PassQueue::AsyncCompute);

	m_cpuStartTicks = queryPerformanceCounterTicks();

	if (m_cmd != VK_NULL_HANDLE && m_profiler->m_device != nullptr)
	{
		m_profiler->m_device->BeginDebugLabel(
			m_cmd,
			stats.name.c_str(),
			(queue == PassQueue::AsyncCompute) ? kLabelAsync : kLabelGraphics);

		m_bLabelBegun = true;
	}

	m_gpuZone = m_profiler->BeginTracyGpuZone(cmd, ID, threadSlot, queue);

	if (m_frameCtx == nullptr || m_cmd == VK_NULL_HANDLE) return;

	const auto& range = m_frameCtx->m_passTimestampRanges[idx];
	ASSERT(range.beginQuery < range.endQuery);

	const bool bAsync = (queue == PassQueue::AsyncCompute);

	m_timestampPool = bAsync
		? m_frameCtx->m_computeTimestampPool
		: m_frameCtx->m_graphicsTimestampPool;

	// Null is legal for the compute pool: some vendors report
	// timestampValidBits == 0 on compute-only families, in which case the
	// pass still gets CPU timing and a Tracy zone, just no GPU number.
	if (m_timestampPool == VK_NULL_HANDLE)
		return;

	if (bAsync)
	{
		m_frameCtx->m_timestampPassUsedCompute[idx] = true;

		// Written from every recording worker. Relaxed atomic: all
		// writers store the same value, we only need it to not be a
		// formal data race.
		m_frameCtx->m_bHasComputeTimestampsPending.store(
			true, std::memory_order_relaxed);
	}
	else
	{
		m_frameCtx->m_timestampPassUsed[idx] = true;
		m_frameCtx->m_bHasTimestampResultsPending = true;
	}

	vkCmdWriteTimestamp2(
		m_cmd,
		VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
		m_timestampPool,
		range.beginQuery);

	m_bTimestampWritten = true;
}

Profiler::ScopedPass::~ScopedPass()
{
	if (m_profiler == nullptr) return;

	// Guarded on the flag, not on the pool handle: if the constructor
	// skipped the begin write, writing an end here would leave a lone
	// unmatched query that ReadTimestamps would pair with garbage.
	if (m_bTimestampWritten)
	{
		ASSERT(m_timestampPool != VK_NULL_HANDLE);
		ASSERT(m_frameCtx != nullptr && m_cmd != VK_NULL_HANDLE);

		const size_t idx   = static_cast<size_t>(m_trackingID);
		const auto&  range = m_frameCtx->m_passTimestampRanges[idx];

		ASSERT(range.beginQuery < range.endQuery);

		vkCmdWriteTimestamp2(
			m_cmd,
			VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
			m_timestampPool,
			range.endQuery);
	}

	if (m_cpuStartTicks != 0)
	{
		const int64_t endTicks = queryPerformanceCounterTicks();
		const float   cpuMs    =
			(static_cast<float>(endTicks - m_cpuStartTicks) * 1000.0f) /
			static_cast<float>(m_profiler->m_qpcFrequency);

		auto& stats    = m_profiler->m_passStats[static_cast<size_t>(m_trackingID)];
		stats.cpuMsRaw = cpuMs;
		stats.cpuMsAverage.Add(cpuMs);
	}

	m_profiler->EndTracyGpuZone(m_gpuZone);

	if (m_bLabelBegun)
	{
		ASSERT(m_profiler->m_device != nullptr);
		m_profiler->m_device->EndDebugLabel(m_cmd);
	}
}

Profiler::ScopedPass::ScopedPass(ScopedPass&& other) noexcept
	: m_profiler(other.m_profiler)
	, m_frameCtx(other.m_frameCtx)
	, m_cmd(other.m_cmd)
	, m_trackingID(other.m_trackingID)
	, m_gpuZone(other.m_gpuZone)
	, m_cpuStartTicks(other.m_cpuStartTicks)
	, m_timestampPool(other.m_timestampPool)
	, m_bTimestampWritten(other.m_bTimestampWritten)
	, m_threadSlot(other.m_threadSlot)
	, m_queue(other.m_queue)
	, m_bLabelBegun(other.m_bLabelBegun)
{
	other.m_profiler          = nullptr;
	other.m_frameCtx          = nullptr;
	other.m_cmd               = VK_NULL_HANDLE;
	other.m_trackingID        = RD::Renderer_Pass::Count;
	other.m_gpuZone           = nullptr;
	other.m_cpuStartTicks     = 0;
	other.m_timestampPool     = VK_NULL_HANDLE;
	other.m_bTimestampWritten = false;
	other.m_threadSlot        = 0u;
	other.m_queue             = PassQueue::Graphics;
	other.m_bLabelBegun       = false;
}

Profiler::ScopedPass& Profiler::ScopedPass::operator=(ScopedPass&& other) noexcept
{
	if (this == &other) return *this;

	this->~ScopedPass();

	m_profiler          = other.m_profiler;
	m_frameCtx          = other.m_frameCtx;
	m_cmd               = other.m_cmd;
	m_trackingID        = other.m_trackingID;
	m_gpuZone           = other.m_gpuZone;
	m_cpuStartTicks     = other.m_cpuStartTicks;
	m_timestampPool     = other.m_timestampPool;
	m_bTimestampWritten = other.m_bTimestampWritten;
	m_threadSlot        = other.m_threadSlot;
	m_queue             = other.m_queue;
	m_bLabelBegun       = other.m_bLabelBegun;

	other.m_profiler          = nullptr;
	other.m_frameCtx          = nullptr;
	other.m_cmd               = VK_NULL_HANDLE;
	other.m_trackingID        = RD::Renderer_Pass::Count;
	other.m_gpuZone           = nullptr;
	other.m_cpuStartTicks     = 0;
	other.m_timestampPool     = VK_NULL_HANDLE;
	other.m_bTimestampWritten = false;
	other.m_threadSlot        = 0u;
	other.m_queue             = PassQueue::Graphics;
	other.m_bLabelBegun       = false;

	return *this;
}

Profiler::ScopedPass Profiler::ProfilePass(
	FrameContext&     frameCtx,
	VkCommandBuffer   cmd,
	RD::Renderer_Pass trackingID,
	std::string_view  passName,
	uint32_t          threadSlot,
	PassQueue         queue)
{
	return ScopedPass(*this, frameCtx, cmd, trackingID, passName, threadSlot, queue);
}

// -----------------------------------------------------------------------------
// Tracy GPU zone — indexed by Renderer_PassTracking
// -----------------------------------------------------------------------------
void* Profiler::BeginTracyGpuZone(
	VkCommandBuffer   cmd,
	RD::Renderer_Pass trackingID,
	uint32_t          threadSlot,
	PassQueue         queue)
{
#ifdef TRACY_ENABLE
	if (cmd == VK_NULL_HANDLE) return nullptr;

	void* ctx = nullptr;

	if (queue == PassQueue::AsyncCompute)
	{
		// Per-thread: a TracyVkCtx cannot have zones opened on it from
		// two threads at once, and async passes record in parallel.
		if (threadSlot >= m_tracyComputeContexts.size())
			return nullptr;

		ctx = m_tracyComputeContexts[threadSlot];
	}
	else
	{
		// Single context is fine — graphics records inline on the render
		// thread only.
		ctx = m_tracyGraphicsContext;
	}

	if (ctx == nullptr) return nullptr;

	const size_t    idx   = static_cast<size_t>(trackingID);
	TracyPassEntry& entry = m_tracySourceLocations[idx];

	// Distinct element per concurrent pass, and a pass is never recorded
	// twice in a frame, so this lazy init needs no lock.
	if (!entry.srcLoc)
	{
		entry.name   = m_passStats[idx].name;
		entry.srcLoc = std::make_unique<tracy::SourceLocationData>(
			tracy::SourceLocationData{ entry.name.c_str(), "RenderPass", __FILE__, 0, 0 }
		);
	}

	return new tracy::VkCtxScope(
		static_cast<TracyVkCtx>(ctx),   // the SELECTED context
		entry.srcLoc.get(),
		cmd,
		true
	);
#else
	(void)cmd; (void)trackingID; (void)threadSlot; (void)queue;
	return nullptr;
#endif
}

void Profiler::EndTracyGpuZone(void* zone)
{
#ifdef TRACY_ENABLE
	if (zone == nullptr) return;
	delete static_cast<tracy::VkCtxScope*>(zone);
#else
	(void)zone;
#endif
}

// =====================================================================
// Context lifetime
// =====================================================================
void Profiler::InitTracyGraphics(
	VkPhysicalDevice physicalDevice,
	VkDevice         device,
	VkQueue          queue,
	VkCommandBuffer  cmd)
{
#ifdef TRACY_ENABLE
	if (m_tracyGraphicsContext != nullptr) return;
	m_tracyGraphicsContext = TracyVkContext(physicalDevice, device, queue, cmd);
#else
	(void)physicalDevice; (void)device; (void)queue; (void)cmd;
#endif
}

void Profiler::InitTracyCompute(
	VkPhysicalDevice physicalDevice,
	VkDevice         device,
	VkQueue          queue,
	VkCommandBuffer  cmd,
	uint32_t         threadSlotCount)
{
#ifdef TRACY_ENABLE
	if (!m_tracyComputeContexts.empty()) return;

	ASSERT(threadSlotCount > 0u);

	m_tracyComputeContexts.resize(threadSlotCount, nullptr);

	for (uint32_t i = 0; i < threadSlotCount; ++i)
	{
		VK_CHECK(vkResetCommandBuffer(cmd, 0));

		m_tracyComputeContexts[i] =
			TracyVkContext(physicalDevice, device, queue, cmd);
	}
#else
	(void)physicalDevice; (void)device; (void)queue; (void)cmd; (void)threadSlotCount;
#endif
}

void Profiler::CollectTracyGraphics(VkCommandBuffer cmd)
{
#ifdef TRACY_ENABLE
	if (m_tracyGraphicsContext == nullptr) return;
	TracyVkCollect(static_cast<TracyVkCtx>(m_tracyGraphicsContext), cmd);
#else
	(void)cmd;
#endif
}

void Profiler::CollectTracyCompute(VkCommandBuffer cmd)
{
#ifdef TRACY_ENABLE
	if (cmd == VK_NULL_HANDLE) return;

	for (void* ctx : m_tracyComputeContexts)
	{
		if (ctx == nullptr) continue;
		TracyVkCollect(static_cast<TracyVkCtx>(ctx), cmd);
	}
#else
	(void)cmd;
#endif
}

void Profiler::ShutdownTracyGPU()
{
#ifdef TRACY_ENABLE
	if (m_tracyGraphicsContext != nullptr)
	{
		TracyVkDestroy(static_cast<TracyVkCtx>(m_tracyGraphicsContext));
		m_tracyGraphicsContext = nullptr;
	}

	for (void*& ctx : m_tracyComputeContexts)
	{
		if (ctx == nullptr) continue;
		TracyVkDestroy(static_cast<TracyVkCtx>(ctx));
		ctx = nullptr;
	}
	m_tracyComputeContexts.clear();
#endif
}

bool Profiler::IsTracyGraphicsActive() const noexcept
{
#ifdef TRACY_ENABLE
	return m_tracyGraphicsContext != nullptr;
#else
	return false;
#endif
}

bool Profiler::IsTracyComputeActive() const noexcept
{
#ifdef TRACY_ENABLE
	return !m_tracyComputeContexts.empty() &&
		   m_tracyComputeContexts[0] != nullptr;
#else
	return false;
#endif
}

// -----------------------------------------------------------------------------
// GPU timestamp accumulation — called from FrameContext timestamp resolve
// -----------------------------------------------------------------------------
void Profiler::AddGpuPassTime(RD::Renderer_Pass trackingID, float milliseconds)
{
	auto& stats    = m_passStats[static_cast<size_t>(trackingID)];
	stats.gpuMsRaw += milliseconds;
	stats.gpuMsAverage.Add(milliseconds);
}

// -----------------------------------------------------------------------------
// Stats reset
// -----------------------------------------------------------------------------
void Profiler::ResetPassStats()
{
	for (auto& stats : m_passStats)
	{
		stats.activeLastFrame     = stats.activeThisFrame;
		stats.activeThisFrame     = false;
		stats.cpuMsRaw            = 0.0f;
		stats.gpuMsRaw            = 0.0f;
		stats.asyncQueueLastFrame = stats.asyncQueueThisFrame;
		stats.asyncQueueThisFrame = false;
	}
}

// -----------------------------------------------------------------------------
// Stats accessors
// -----------------------------------------------------------------------------
const PassTimingStats& Profiler::GetPassStats(RD::Renderer_Pass trackingID) const
{
	return m_passStats[static_cast<size_t>(trackingID)];
}

PassTimingStats& Profiler::GetPassStats(RD::Renderer_Pass trackingID)
{
	return m_passStats[static_cast<size_t>(trackingID)];
}

// -----------------------------------------------------------------------------
// General-purpose CPU timer
// -----------------------------------------------------------------------------
void Profiler::StartTimer()
{
	m_startTimerTick = queryPerformanceCounterTicks();
}

float Profiler::EndTimerMS() const
{
	const int64_t elapsed = queryPerformanceCounterTicks() - m_startTimerTick;
	return (static_cast<float>(elapsed) * 1000.0f) / static_cast<float>(m_qpcFrequency);
}

float Profiler::EndTimerSec() const
{
	return EndTimerMS() / 1000.0f;
}

// -----------------------------------------------------------------------------
// Frame stats accessors
// -----------------------------------------------------------------------------
FrameStats& Profiler::getStats()             { return m_stats; }
const FrameStats& Profiler::getStats() const { return m_stats; }

// -----------------------------------------------------------------------------
// Tracy GPU context
// -----------------------------------------------------------------------------
bool Profiler::IsTracyCompiledIn() const
{
#ifdef TRACY_ENABLE
	return true;
#else
	return false;
#endif
}
