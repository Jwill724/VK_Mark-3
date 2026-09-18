#include "pch.h"

#include "BufferBarriers.h"
#include "../backend/memory/AllocatedBuffer.h"
#include "../backend/VulkanTypes.h"

static void ResolveFamilies(uint32_t& src, uint32_t& dst, bool concurrent)
{
	// If buffer is concurrent OR families match, do NOT encode a QFOT.
	if (concurrent || src == dst) {
		src = VK_QUEUE_FAMILY_IGNORED;
		dst = VK_QUEUE_FAMILY_IGNORED;
	}
}

void BufferBarriers::ComputeWriteToRead(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf)
{
	VkBufferMemoryBarrier2 b{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
	b.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
	b.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
	b.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
	b.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
	b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.buffer = buf.m_buffer;
	b.offset = 0;
	b.size = VK_WHOLE_SIZE;

	VkDependencyInfo di{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	di.bufferMemoryBarrierCount = 1;
	di.pBufferMemoryBarriers = &b;

	vkCmdPipelineBarrier2(cmd, &di);
}

void BufferBarriers::ComputeReadToWrite(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf)
{
	VkBufferMemoryBarrier2 b{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
	b.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
	b.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
	b.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
	b.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
	b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.buffer = buf.m_buffer;
	b.offset = 0;
	b.size = VK_WHOLE_SIZE;

	VkDependencyInfo di{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	di.bufferMemoryBarrierCount = 1;
	di.pBufferMemoryBarriers = &b;

	vkCmdPipelineBarrier2(cmd, &di);
}

void BufferBarriers::ComputeWriteToRW(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf)
{
	VkBufferMemoryBarrier2 b{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
	b.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
	b.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
	b.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
	b.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
	b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.buffer = buf.m_buffer;
	b.offset = 0;
	b.size = VK_WHOLE_SIZE;

	VkDependencyInfo di{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	di.bufferMemoryBarrierCount = 1;
	di.pBufferMemoryBarriers = &b;

	vkCmdPipelineBarrier2(cmd, &di);
}

void BufferBarriers::ComputeStorageRW(VkCommandBuffer cmd)
{
	VkMemoryBarrier2 b{ VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };

	b.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
	b.srcAccessMask =
		VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
		VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;

	b.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
	b.dstAccessMask =
		VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
		VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;

	VkDependencyInfo d{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	d.memoryBarrierCount = 1;
	d.pMemoryBarriers = &b;

	vkCmdPipelineBarrier2(cmd, &d);
}

void BufferBarriers::ComputeWriteToTransferRead(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf)
{
	VkBufferMemoryBarrier2 b{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
	b.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
	b.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
	b.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
	b.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
	b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.buffer = buf.m_buffer;
	b.offset = 0;
	b.size = VK_WHOLE_SIZE;

	VkDependencyInfo di{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	di.bufferMemoryBarrierCount = 1;
	di.pBufferMemoryBarriers = &b;

	vkCmdPipelineBarrier2(cmd, &di);
}

void BufferBarriers::ComputeWriteToIndirectRead(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf)
{
	VkBufferMemoryBarrier2 b{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
	b.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
	b.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
	b.dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
	b.dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
	b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.buffer = buf.m_buffer;
	b.offset = 0;
	b.size = VK_WHOLE_SIZE;

	VkDependencyInfo di{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	di.bufferMemoryBarrierCount = 1;
	di.pBufferMemoryBarriers = &b;

	vkCmdPipelineBarrier2(cmd, &di);
}

void BufferBarriers::TransferWriteToComputeRead(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf,
	const DeviceContext& dCtx)
{
	uint32_t srcFamilyIndex = dCtx.queueIndices.transferFamily.value();
	uint32_t dstFamilyIndex = dCtx.queueIndices.graphicsFamily.value();
	ResolveFamilies(srcFamilyIndex, dstFamilyIndex, buf.m_bIsConcurrent);

	VkBufferMemoryBarrier2 b{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
	b.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
	b.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
	b.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
					 VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT |
					 VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT;
	b.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
	b.srcQueueFamilyIndex = srcFamilyIndex;
	b.dstQueueFamilyIndex = dstFamilyIndex;
	b.buffer = buf.m_buffer;
	b.offset = 0;
	b.size = VK_WHOLE_SIZE;

	VkDependencyInfo di{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	di.bufferMemoryBarrierCount = 1;
	di.pBufferMemoryBarriers = &b;

	vkCmdPipelineBarrier2(cmd, &di);
}

void BufferBarriers::TransferWriteToComputeWrite(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf,
	const DeviceContext& dCtx)
{
	uint32_t srcFamilyIndex = dCtx.queueIndices.transferFamily.value();
	uint32_t dstFamilyIndex = dCtx.queueIndices.graphicsFamily.value();
	ResolveFamilies(srcFamilyIndex, dstFamilyIndex, buf.m_bIsConcurrent);

	VkBufferMemoryBarrier2 b{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
	b.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
	b.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
	b.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
	b.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
	b.srcQueueFamilyIndex = srcFamilyIndex;
	b.dstQueueFamilyIndex = dstFamilyIndex;
	b.buffer = buf.m_buffer;
	b.offset = 0;
	b.size = VK_WHOLE_SIZE;

	VkDependencyInfo di{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	di.bufferMemoryBarrierCount = 1;
	di.pBufferMemoryBarriers = &b;

	vkCmdPipelineBarrier2(cmd, &di);
}

void BufferBarriers::CmdFillToComputeAS(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf)
{
	VkBufferMemoryBarrier2 b{
		VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2
	};

	b.srcStageMask = VK_PIPELINE_STAGE_2_CLEAR_BIT;
	b.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
	b.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
	b.dstAccessMask =
		VK_ACCESS_2_SHADER_READ_BIT |
		VK_ACCESS_2_SHADER_WRITE_BIT;

	b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

	b.buffer = buf.m_buffer;
	b.offset = 0;
	b.size = VK_WHOLE_SIZE;

	VkDependencyInfo di{
		VK_STRUCTURE_TYPE_DEPENDENCY_INFO
	};

	di.bufferMemoryBarrierCount = 1;
	di.pBufferMemoryBarriers = &b;

	vkCmdPipelineBarrier2(cmd, &di);
}

void BufferBarriers::CmdFillToComputeRW(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf)
{
	VkBufferMemoryBarrier2 b{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
	b.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
	b.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
	b.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
	b.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
	b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.buffer = buf.m_buffer;
	b.offset = 0;
	b.size = VK_WHOLE_SIZE;

	VkDependencyInfo di{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	di.bufferMemoryBarrierCount = 1;
	di.pBufferMemoryBarriers = &b;

	vkCmdPipelineBarrier2(cmd, &di);
}

void BufferBarriers::CmdFillToMeshRW(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf)
{
	VkBufferMemoryBarrier2 b{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
	b.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
	b.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
	b.dstStageMask = VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT;
	b.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
	b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.buffer = buf.m_buffer;
	b.offset = 0;
	b.size = VK_WHOLE_SIZE;

	VkDependencyInfo di{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	di.bufferMemoryBarrierCount = 1;
	di.pBufferMemoryBarriers = &b;

	vkCmdPipelineBarrier2(cmd, &di);
}

void BufferBarriers::TLASInstanceReuseToCmdFill(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf)
{
	VkBufferMemoryBarrier2 b{
		VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2
	};

	// Previous TLAS build consumed the instance buffer.
	b.srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;

	b.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;

	// vkCmdFillBuffer overwrites it.
	b.dstStageMask = VK_PIPELINE_STAGE_2_CLEAR_BIT;

	b.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;

	b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

	b.buffer = buf.m_buffer;
	b.offset = 0;
	b.size = VK_WHOLE_SIZE;

	VkDependencyInfo di{
		VK_STRUCTURE_TYPE_DEPENDENCY_INFO
	};

	di.bufferMemoryBarrierCount = 1;
	di.pBufferMemoryBarriers = &b;

	vkCmdPipelineBarrier2(cmd, &di);
}

void BufferBarriers::TransferWriteToGraphicsRead(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf,
	const DeviceContext& dCtx)
{
	uint32_t srcFamilyIndex = dCtx.queueIndices.transferFamily.value();
	uint32_t dstFamilyIndex = dCtx.queueIndices.graphicsFamily.value();
	ResolveFamilies(srcFamilyIndex, dstFamilyIndex, buf.m_bIsConcurrent);

	VkBufferMemoryBarrier2 b{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
	b.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
	b.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
	b.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT   |
					 VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
					 VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT |
					 VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT;
	b.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
	b.srcQueueFamilyIndex = srcFamilyIndex;
	b.dstQueueFamilyIndex = dstFamilyIndex;
	b.buffer = buf.m_buffer;
	b.offset = 0;
	b.size = VK_WHOLE_SIZE;

	VkDependencyInfo di{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	di.bufferMemoryBarrierCount = 1;
	di.pBufferMemoryBarriers = &b;

	vkCmdPipelineBarrier2(cmd, &di);
}

void BufferBarriers::ComputeWriteToFragmentRead(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf)
{
	VkBufferMemoryBarrier2 b{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
	b.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
	b.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
	b.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
	b.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
	b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.buffer = buf.m_buffer;
	b.offset = 0;
	b.size = VK_WHOLE_SIZE;

	VkDependencyInfo di{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	di.bufferMemoryBarrierCount = 1;
	di.pBufferMemoryBarriers = &b;

	vkCmdPipelineBarrier2(cmd, &di);
}

void BufferBarriers::ComputeWriteToVertexRead(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf)
{
	VkBufferMemoryBarrier2 b{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
	b.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
	b.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
	b.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
	b.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
	b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.buffer = buf.m_buffer;
	b.offset = 0;
	b.size = VK_WHOLE_SIZE;

	VkDependencyInfo di{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	di.bufferMemoryBarrierCount = 1;
	di.pBufferMemoryBarriers = &b;

	vkCmdPipelineBarrier2(cmd, &di);
}

void BufferBarriers::ComputeWriteToTaskRead(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf)
{
	VkBufferMemoryBarrier2 b{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
	b.srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
	b.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
	b.dstStageMask  = VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT;
	b.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
	b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.buffer = buf.m_buffer;
	b.offset = 0;
	b.size   = VK_WHOLE_SIZE;

	VkDependencyInfo di{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	di.bufferMemoryBarrierCount = 1;
	di.pBufferMemoryBarriers    = &b;
	vkCmdPipelineBarrier2(cmd, &di);
}


void BufferBarriers::ComputeWriteToMeshRead(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf)
{
	VkBufferMemoryBarrier2 b{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
	b.srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
	b.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
	b.dstStageMask  = VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT |
					  VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT;
	b.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
	b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.buffer = buf.m_buffer;
	b.offset = 0;
	b.size   = VK_WHOLE_SIZE;

	VkDependencyInfo di{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	di.bufferMemoryBarrierCount = 1;
	di.pBufferMemoryBarriers    = &b;
	vkCmdPipelineBarrier2(cmd, &di);
}

void BufferBarriers::MeshWriteToMeshRead(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf)
{
	VkBufferMemoryBarrier2 b{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
	b.srcStageMask  = VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT;
	b.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
	b.dstStageMask  = VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT;
	b.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
	b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.buffer = buf.m_buffer;
	b.offset = 0;
	b.size   = VK_WHOLE_SIZE;

	VkDependencyInfo di{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	di.bufferMemoryBarrierCount = 1;
	di.pBufferMemoryBarriers    = &b;
	vkCmdPipelineBarrier2(cmd, &di);
}

void BufferBarriers::ComputeWriteToASBuildRead(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf)
{
	VkBufferMemoryBarrier2 b{
		VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2
	};

	b.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
	b.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
	b.dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
	b.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;

	b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

	b.buffer = buf.m_buffer;
	b.offset = 0;
	b.size = VK_WHOLE_SIZE;

	VkDependencyInfo di{
		VK_STRUCTURE_TYPE_DEPENDENCY_INFO
	};

	di.bufferMemoryBarrierCount = 1;
	di.pBufferMemoryBarriers = &b;

	vkCmdPipelineBarrier2(cmd, &di);
}

void BufferBarriers::ASBuildToASBuild(VkCommandBuffer cmd)
{
	ASMemoryBarrier(cmd,
		VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
		VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
		VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
		VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR |
		VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR);
}

void BufferBarriers::ASBuildToRayQueryRead(VkCommandBuffer cmd)
{
	ASMemoryBarrier(
		cmd,
		VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
		VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
		VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
		VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR);
}

void BufferBarriers::TransferReleaseOnGraphics(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf,
	const DeviceContext& dCtx)
{
	uint32_t srcFamilyIndex = dCtx.queueIndices.transferFamily.value();
	uint32_t dstFamilyIndex = dCtx.queueIndices.graphicsFamily.value();
	ResolveFamilies(srcFamilyIndex, dstFamilyIndex, buf.m_bIsConcurrent);

	VkBufferMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
	barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
	barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
	barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
	barrier.dstAccessMask = VK_ACCESS_2_NONE;
	barrier.srcQueueFamilyIndex = srcFamilyIndex;
	barrier.dstQueueFamilyIndex = dstFamilyIndex;
	barrier.buffer = buf.m_buffer;
	barrier.offset = 0;
	barrier.size = VK_WHOLE_SIZE;

	VkDependencyInfo dependencyInfo{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	dependencyInfo.bufferMemoryBarrierCount = 1;
	dependencyInfo.pBufferMemoryBarriers = &barrier;

	vkCmdPipelineBarrier2(cmd, &dependencyInfo);
}

//void BufferBarriers::TransferReleaseOnCompute(
//	VkCommandBuffer cmd,
//	const AllocatedBuffer& buf,
//	const DeviceContext& dCtx)
//{
//	uint32_t srcFamilyIndex = dCtx.queueIndices.transferFamily.value();
//	uint32_t dstFamilyIndex = dCtx.queueIndices.computeFamily.value();
//	ResolveFamilies(srcFamilyIndex, dstFamilyIndex, buf.m_bIsConcurrent);
//
//	VkBufferMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
//	barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
//	barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
//	barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
//	barrier.dstAccessMask = VK_ACCESS_2_NONE;
//	barrier.srcQueueFamilyIndex = srcFamilyIndex;
//	barrier.dstQueueFamilyIndex = dstFamilyIndex;
//	barrier.buffer = buf.m_buffer;
//	barrier.offset = 0;
//	barrier.size = VK_WHOLE_SIZE;
//
//	VkDependencyInfo dependencyInfo{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
//	dependencyInfo.bufferMemoryBarrierCount = 1;
//	dependencyInfo.pBufferMemoryBarriers = &barrier;
//
//	vkCmdPipelineBarrier2(cmd, &dependencyInfo);
//}
