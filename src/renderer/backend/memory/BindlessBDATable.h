#pragma once

#include "renderer/RendererDefinitions.h"
#include "AllocatedBuffer.h"
#include <array>

namespace RD = RendererDefinitions;

class Allocator;

// Bindless indirect table, stores ssbo bda pointers.
// Upload address table buffer after new addresses are attached or removed to the table.
// Buffer cleanup needs to be done by the Allocator class, where AllocatedBuffer types are created
class BindlessBDATable final
{
public:
	static constexpr size_t ADDRESS_TABLE_BUFFER_COUNT       = static_cast<size_t>(RD::Renderer_Buffer::Count);
	static constexpr size_t GPU_ADDRESS_TABLE_SIZE_GPU_BYTES = ADDRESS_TABLE_BUFFER_COUNT * sizeof(uint64_t);

	// Set this one time at start, lasts for duration of renderer lifetime.
	void Init(Allocator& allocator);
	void Shutdown(Allocator& allocator);

	const AllocatedBuffer& GetTableBuffer() const { return m_addressTableBuffer; }

	const std::array<uint64_t, ADDRESS_TABLE_BUFFER_COUNT>& GetAddrPtrTable() const { return m_addresses; }

	const AllocatedBuffer& GetGPUBuffer(RD::Renderer_Buffer slot) const;

	bool ContainsGPUBuffer(RD::Renderer_Buffer slot) const noexcept;

	void AddGPUBufferToAddressTable(RD::Renderer_Buffer slot, size_t size, Allocator& allocator);

	void ClearGPUAddressBuffer(RD::Renderer_Buffer slot, Allocator& allocator);

	bool IsTableDirty() const { return m_bIsTableDirty; }

	// Called after buffer barrier
	void ClearDirty() { m_bIsTableDirty = false; }

	uint32_t GetCpuVersion() const { return m_cpuVersion; }
	void UpdateCpuVersion() { m_cpuVersion++; }
	uint32_t GetGpuVersion() const { return m_gpuVersion; }
	void SetGpuVersion(uint32_t version);

	void IsVersionMismatched() const;

	void SwapBufferSlots(RD::Renderer_Buffer a, RD::Renderer_Buffer b);

	void ClearAssetBuffers(Allocator& allocator);

	AllocatedBuffer DetachGPUAddressBuffer(RD::Renderer_Buffer slot);

private:
	void SetAddress(RD::Renderer_Buffer slot, uint64_t address);
	void RemoveAddress(RD::Renderer_Buffer slot);

	std::array<uint64_t, ADDRESS_TABLE_BUFFER_COUNT> m_addresses{};  // Primary array that stores buffer pointers, entry in the gpu
	std::array<AllocatedBuffer, ADDRESS_TABLE_BUFFER_COUNT> m_gpuBuffers{}; // Full handles

	uint32_t m_cpuVersion    = 1u; // increment when modified
	uint32_t m_gpuVersion    = 0u; // last uploaded version
	bool     m_bIsTableDirty = false;

	AllocatedBuffer m_addressTableBuffer;
};
