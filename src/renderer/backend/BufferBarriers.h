#pragma once

#include "renderer/backend/VulkanForward.h"

struct AllocatedBuffer;
struct DeviceContext;

namespace BufferBarriers
{
	void ComputeWriteToRead(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf);
	void ComputeWriteToRW(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf);
	void ComputeWriteToTransferRead(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf);
	void ComputeReadToWrite(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf);

	void ComputeStorageRW(VkCommandBuffer cmd);

	void ComputeWriteToIndirectRead(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf);

	void CmdFillToComputeRW(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf);

	void CmdFillToMeshRW(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf);

	void TLASInstanceReuseToCmdFill(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf);

	void ComputeWriteToASBuildRead(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf);

	void ASBuildToASBuild(VkCommandBuffer cmd);
	void ASBuildToRayQueryRead(VkCommandBuffer cmd);

	void ComputeWriteToFragmentRead(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf);

	void ComputeWriteToVertexRead(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf);

	void ComputeWriteToTaskRead(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf);

	void ComputeWriteToMeshRead(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf);

	// Task shader writes the meshlet visibility bitfield; next consumer is another
	// task shader(phase 2, or next frame's phase 1 after the A/B swap).
	void MeshWriteToMeshRead(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf);

	void TransferWriteToComputeRead(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf,
		const DeviceContext& dCtx);

	void TransferWriteToComputeWrite(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf,
		const DeviceContext& dCtx);

	void CmdFillToComputeAS(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf);

	void TransferWriteToGraphicsRead(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf,
		const DeviceContext& dCtx);

	// Transfer queue buffer releases
	void TransferReleaseOnCompute(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf,
		const DeviceContext& dCtx);
	void TransferReleaseOnGraphics(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf,
		const DeviceContext& dCtx);
}
