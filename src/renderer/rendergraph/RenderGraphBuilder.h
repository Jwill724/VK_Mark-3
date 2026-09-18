#pragma once

#include "renderer/RendererDefinitions.h"
namespace RD = RendererDefinitions;

#include "../backend/VulkanTypes.h"
#include "../backend/descriptors/DescriptorWriter.h"

#include <variant>
#include <functional>
#include "scopes/ComputeScope.h"
#include "scopes/GraphicsScope.h"
#include "RenderGraphResources.h"
#include "RenderGraphSchedule.h"

using ScopeVariant = std::variant<GraphicsScope, ComputeScope>;

struct RenderPassDesc
{
	std::string passName{};

	// Hard locked order of execution
	std::vector<RD::Renderer_Pipeline> declaredPipelines;

	ScopeVariant scope;

	std::vector<RenderResourceUsage> resources;

	PushDescriptorWriter pushWriter;

	// --- scheduling ---
	RenderPhase phase = RenderPhase::Lighting;

	// The single scheduling flag. Implies BOTH:
	//   - executes on the compute queue (GPU overlap)
	//   - recorded into a secondary on a worker (CPU overlap)
	bool bAsyncCompute = false;

	bool bForceExecution = false;

	std::function<bool(const RenderPassExecutionContext&)> shouldExecute;

	std::function<void(RenderPassExecutionContext&, RenderPassDesc&)> record;
};

class RenderPassBuilder
{
public:
	RenderPassBuilder(RenderPassDesc& desc) : m_desc(desc) {}

	// Graph emits the enter transition; no exit.
	RenderPassBuilder& ReadResource(
		RD::Renderer_RenderTarget target,
		RD::ImageAccess access,
		uint32_t baseMip = 0,
		uint32_t mipCount = 1);

	// Graph emits BOTH transitions. Use when the pass touches the target
	// in one uniform state throughout.
	RenderPassBuilder& WriteResource(
		RD::Renderer_RenderTarget target,
		RD::ImageAccess enterAccess,
		RD::ImageAccess exitAccess,
		uint32_t baseMip = 0,
		uint32_t mipCount = 1);

	// Graph emits the ENTER transition only. The pass owns everything in between
	RenderPassBuilder& InternalResource(
		RD::Renderer_RenderTarget target,
		RD::ImageAccess enterAccess,
		RD::ImageAccess declaredExitAccess,
		uint32_t baseMip = 0,
		uint32_t mipCount = 1);

	RenderPassBuilder& SetPhase(RenderPhase phase);

	// Ping-pong history pair.
	RenderPassBuilder& HistoryResource(
		RD::Renderer_RenderTarget slotA,
		RD::Renderer_RenderTarget slotB,
		RD::ImageAccess enterAccess,
		RD::ImageAccess exitAccess,
		bool bIsWrite,
		bool bManualExitTransition = false);

	// Declares that the resource must already be in expectedAccess.
	// Emits no transition. Used for persistent/global resources whose producer
	// establishes their final read state before this pass executes.
	RenderPassBuilder& RequireResource(
		RD::Renderer_RenderTarget target,
		RD::ImageAccess expectedAccess,
		uint32_t baseMip = 0,
		uint32_t mipCount = 1);

	RenderPassBuilder& RequireResourceIf(
		RD::Renderer_RenderTarget target,
		RD::ImageAccess requiredAccess,
		std::function<bool(const RD::RenderStateInfo&)> condition,
		uint32_t baseMip = 0,
		uint32_t mipCount = 1);

	// Marks the pass async-capable. Sets the phase itself, so async passes never need SetPhase.
	RenderPassBuilder& RunOnAsyncCompute()
	{
		m_desc.bAsyncCompute = true;
		m_desc.phase         = RenderPhase::AsyncWindow;
		return *this;
	}

	RenderPassBuilder& SetRecord(
		std::function<void(RenderPassExecutionContext&, RenderPassDesc&)> fn)
	{
		m_desc.record = std::move(fn);
		return *this;
	}

	RenderPassBuilder& SetExecutionCondition(
		std::function<bool(const RenderPassExecutionContext&)> fn)
	{
		m_desc.shouldExecute = std::move(fn);
		return *this;
	}

	RenderPassBuilder& ForceExecution()
	{
		m_desc.bForceExecution = true;
		return *this;
	}

private:
	RenderPassDesc& m_desc;
};
