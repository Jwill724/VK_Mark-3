#pragma once

#include "../backend/VulkanForward.h"
#include "../RendererDefinitions.h"

#include <array>
#include <bitset>
#include <cstdint>
#include <functional>
#include <vector>

namespace RD = RendererDefinitions;

using TargetSet = std::bitset<RD::RENDER_TARGET_COUNT>;

enum class RenderPhase : uint32_t
{
	Visibility,
	Prepass,
	AsyncWindow,
	Lighting,

	Temporal,
	PostProcess,
	Present,

	Count
};

enum class PassQueue : uint8_t
{
	Graphics     = 0,
	AsyncCompute = 1
};

enum class BatchId : uint8_t
{
	G0 = 0,
	C0 = 1,
	G1 = 2,
	G2 = 3,

	Count
};

inline constexpr uint32_t MAX_SUBMIT_BATCHES     = static_cast<uint32_t>(BatchId::Count);
inline constexpr uint32_t MAX_GRAPHICS_PRIMARIES = 3u;   // G0, G1, G2

// ---------------------------------------------------------------------
// A transition decided at compile time, replayed from a PRIMARY.
// Graph-emitted barriers never live inside a secondary
// ---------------------------------------------------------------------
struct BakedImageBarrier
{
	RD::Renderer_RenderTarget target;
	RD::ImageAccess           oldAccess;
	RD::ImageAccess           newAccess;
	uint32_t                  baseMip;
	uint32_t                  mipCount;
};

struct PassScheduleInfo
{
	uint32_t  passIndex = UINT32_MAX;
	PassQueue queue     = PassQueue::Graphics;

	// Cleared, never freed — capacity persists after frame 1, so steady
	// state costs no allocation.
	std::vector<BakedImageBarrier> enterBarriers;
	std::vector<BakedImageBarrier> exitBarriers;

	// Baked at compile time, so it is stable and safe to query from a record lambda.
	TargetSet firstWriteTargets;

	// Async passes only. Written by the record job, read by the
	// assembling thread AFTER the RunParallel join — no atomics needed.
	VkCommandBuffer recordedCmd = VK_NULL_HANDLE;
};

struct SubmitBatch
{
	std::vector<PassScheduleInfo> passes;

	// Handoff transitions emitted at the END of this batch's primary.
	// Used to fold C0's first-touch transitions back into G0, where the
	// graphics-legal stage masks are valid and the timeline signal makes
	// them visible to the waiting compute submit.
	std::vector<BakedImageBarrier> tailBarriers;

	PassQueue queue   = PassQueue::Graphics;
	bool      bActive = false;

	// Every target any pass in this batch declares. Debug only, for the
	// C0 / G1 concurrency check.
	TargetSet touchedTargets;

	void Reset()
	{
		for (auto& p : passes)
		{
			p.enterBarriers.clear();
			p.exitBarriers.clear();
			p.firstWriteTargets.reset();
			p.recordedCmd = VK_NULL_HANDLE;
		}
		passes.clear();
		tailBarriers.clear();
		touchedTargets.reset();
		bActive = false;
	}
};

struct CompiledSchedule
{
	std::array<SubmitBatch, MAX_SUBMIT_BATCHES> batches{};

	// Flat list of C0 pass slots, for the single RunParallel dispatch.
	// Indices into batches[C0].passes.
	std::vector<uint32_t> asyncRecordList;

	uint64_t activeMask         = 0ull;
	uint32_t graphicsBatchCount = 0u;
	bool     bUsesAsyncCompute  = false;
	bool     bValid             = false;

	const SubmitBatch& Get(BatchId id) const noexcept
	{
		return batches[static_cast<size_t>(id)];
	}

	SubmitBatch& Get(BatchId id) noexcept
	{
		return batches[static_cast<size_t>(id)];
	}
};

struct RecordHooks
{
	// First graphics primary (G0) only.
	std::function<void(VkCommandBuffer)> onFrameBegin;

	// Last graphics primary only.
	std::function<void(VkCommandBuffer)> onFrameEnd;

	// Async compute primary only.
	std::function<void(VkCommandBuffer)> onAsyncBatchEnd;

	// Command buffer state does not inherit, so descriptor sets and the
	// index buffer must be rebound per command buffer. Called exactly:
	//   - once at the top of each graphics primary (nothing invalidates
	//     it now that graphics primaries contain no ExecuteCommands)
	//   - once at the top of each compute secondary
	// The compute PRIMARY needs none: it holds only ExecuteCommands.
	std::function<void(VkCommandBuffer, PassQueue)> bindPrologue;
};

inline bool IsFirstGraphicsWriteThisFrame(
	const PassScheduleInfo* info,
	RD::Renderer_RenderTarget target) noexcept
{
	if (info == nullptr) return false;
	return info->firstWriteTargets.test(static_cast<size_t>(target));
}

struct ImageUse
{
	RD::ImageAccess access;
	bool            bWrite;
	const char* passName;
};
