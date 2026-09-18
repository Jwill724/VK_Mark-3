#include "pch.h"

#include "RenderGraphBuilder.h"
#include "RenderGraphResources.h"
#include "scopes/ComputeScope.h"
#include "scopes/GraphicsScope.h"

RenderPassBuilder& RenderPassBuilder::ReadResource(
	RD::Renderer_RenderTarget target,
	RD::ImageAccess access,
	uint32_t baseMip,
	uint32_t mipCount)
{
	m_desc.resources.emplace_back(RenderResourceUsage{
		.target = target,
		.enterAccess = access,
		.exitAccess = access,
		.baseMip = baseMip,
		.mipCount = mipCount,
		.bIsWrite = false,
		.bManualExitTransition = false,
		.bRequireExistingState = false
	});

	return *this;
}

RenderPassBuilder& RenderPassBuilder::WriteResource(
	RD::Renderer_RenderTarget target,
	RD::ImageAccess enterAccess,
	RD::ImageAccess exitAccess,
	uint32_t baseMip,
	uint32_t mipCount)
{
	m_desc.resources.emplace_back(RenderResourceUsage{
		.target = target,
		.enterAccess = enterAccess,
		.exitAccess = exitAccess,
		.baseMip = baseMip,
		.mipCount = mipCount,
		.bIsWrite = true,
		.bManualExitTransition = false,
		.bRequireExistingState = false
	});

	return *this;
}

RenderPassBuilder& RenderPassBuilder::InternalResource(
	RD::Renderer_RenderTarget target,
	RD::ImageAccess enterAccess,
	RD::ImageAccess declaredExitAccess,
	uint32_t baseMip,
	uint32_t mipCount)
{
	m_desc.resources.emplace_back(RenderResourceUsage{
		.target = target,
		.enterAccess = enterAccess,
		.exitAccess = declaredExitAccess,
		.baseMip = baseMip,
		.mipCount = mipCount,
		.bIsWrite = true,
		.bManualExitTransition = true,
		.bRequireExistingState = false
	});

	return *this;
}

RenderPassBuilder& RenderPassBuilder::HistoryResource(
	RD::Renderer_RenderTarget slotA,
	RD::Renderer_RenderTarget slotB,
	RD::ImageAccess enterAccess,
	RD::ImageAccess exitAccess,
	bool bIsWrite,
	bool bManualExitTransition)
{
	ASSERT(slotA != slotB && "History pair needs two distinct slots");

	const RD::Renderer_RenderTarget slots[2] = { slotA, slotB };

	for (const auto target : slots)
	{
		m_desc.resources.emplace_back(RenderResourceUsage{
			.target = target,
			.enterAccess = enterAccess,
			.exitAccess = exitAccess,
			.baseMip = 0,
			.mipCount = 1,
			.bIsWrite = bIsWrite,
			.bManualExitTransition = bManualExitTransition,
			.bRequireExistingState = false
		});
	}

	return *this;
}

RenderPassBuilder& RenderPassBuilder::RequireResource(
	RD::Renderer_RenderTarget target,
	RD::ImageAccess expectedAccess,
	uint32_t baseMip,
	uint32_t mipCount)
{
	m_desc.resources.emplace_back(RenderResourceUsage{
		.target = target,
		.enterAccess = expectedAccess,
		.exitAccess = expectedAccess,
		.baseMip = baseMip,
		.mipCount = mipCount,
		.bIsWrite = false,
		.bManualExitTransition = false,
		.bRequireExistingState = true
		});

	return *this;
}

RenderPassBuilder& RenderPassBuilder::RequireResourceIf(
	RD::Renderer_RenderTarget target,
	RD::ImageAccess requiredAccess,
	std::function<bool(const RD::RenderStateInfo&)> condition,
	uint32_t baseMip,
	uint32_t mipCount)
{
	ASSERT(condition);

	m_desc.resources.emplace_back(RenderResourceUsage{
		.target = target,
		.enterAccess = requiredAccess,
		.exitAccess = requiredAccess,
		.baseMip = baseMip,
		.mipCount = mipCount,
		.bIsWrite = false,
		.bManualExitTransition = false,
		.bRequireExistingState = true,
		.condition = std::move(condition)
		});

	return *this;
}

RenderPassBuilder& RenderPassBuilder::SetPhase(RenderPhase phase)
{
	ASSERT(!m_desc.bAsyncCompute);

	m_desc.phase = phase;
	return *this;
}