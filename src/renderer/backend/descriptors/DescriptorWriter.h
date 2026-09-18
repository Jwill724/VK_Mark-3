#pragma once

#include "../VulkanTypes.h"
#include <span>

struct AllocatedImage;
struct AllocatedBuffer;

class DescriptorWriter
{
public:
	void WriteBuffer(
		uint32_t binding,
		const AllocatedBuffer& buffer,
		VkDescriptorSet set,
		VkDescriptorType type = VK_DESCRIPTOR_TYPE_MAX_ENUM);

	void WriteBindlessImages(
		std::span<const VkDescriptorImageInfo> images,
		uint32_t binding,
		VkDescriptorSet set,
		Vulkan_DescriptorType type = Vulkan_DescriptorType::COMBINED_SAMPLER); // No storage for now

	// Requires immediate update
	void WriteInlineUniform(
		VkDevice device,
		VkDescriptorSet unifiedSet,
		const void* data,
		uint32_t size);

	void WriteAccelerationStructure(
		uint32_t binding,
		VkAccelerationStructureKHR tlas,
		VkDescriptorSet set);

	void UpdateSet(VkDevice device, VkDescriptorSet set);

	virtual ~DescriptorWriter() { Clear(); }
	void Clear() { ClearImpl(); }

protected:
	// Can write bindless and push images
	struct ImageWriteGroup
	{
		uint32_t binding = UINT32_MAX;
		VkDescriptorType type = VK_DESCRIPTOR_TYPE_MAX_ENUM;
		VkDescriptorSet dstSet = VK_NULL_HANDLE;

		std::vector<VkDescriptorImageInfo> imageInfos;
	};

	// Per-binding grouped m_image descriptor writes
	std::vector<ImageWriteGroup> m_imageWriteGroups;

	std::deque<VkAccelerationStructureKHR> m_accelHandles;
	std::deque<VkWriteDescriptorSetAccelerationStructureKHR> m_accelInfos;

private:
	std::vector<VkDescriptorBufferInfo> m_bufferInfos;
	std::vector<VkWriteDescriptorSet> m_bufferWrites;
	std::vector<size_t> m_writeBufferIndices;

	std::vector<VkWriteDescriptorSet> m_accelWrites;

	// After frameSet is updated, all data written is cleared
	bool m_bShouldClearWrites = false;

	virtual void ClearImpl()
	{
		m_imageWriteGroups.clear();
		m_bufferWrites.clear();
		m_writeBufferIndices.clear();
		m_bufferInfos.clear();
		m_bShouldClearWrites = false;
		m_accelHandles.clear();
		m_accelInfos.clear();
		m_accelWrites.clear();
	}
};

class PushDescriptorWriter final : public DescriptorWriter
{
public:
	// sampler == read only, !sampler == write only
	void WritePushImage(
		uint32_t binding,
		const AllocatedImage& image,
		VkImageLayout imgLayout,
		VkSampler sampler = VK_NULL_HANDLE,
		uint32_t storageViewIndex = UINT32_MAX);

	// HLSL-style separate texture; the sampler is immutable in the set layout
	void WritePushSampledImage(uint32_t binding, VkImageView view, VkImageLayout imgLayout);
	void WritePushStorageImage(uint32_t binding, VkImageView view);
	void WritePushCombinedImage(uint32_t binding, VkImageView view, VkImageLayout imgLayout, VkSampler sampler);

	void WritePushUniformBuffer(
		uint32_t binding,
		VkBuffer buffer,
		VkDeviceSize offset,
		VkDeviceSize range);

	void UpdatePushLayout(
		VkCommandBuffer cmd,
		VkPipelineBindPoint bindPoint,
		VkPipelineLayout pipelineLayout,
		uint32_t setIndex = RD::PUSH_SET);

	void Reserve(size_t descriptorCount);

private:
	std::vector<VkDescriptorImageInfo>  m_pushImageInfos;
	std::vector<VkDescriptorBufferInfo> m_pushBufferInfos;
	std::vector<VkWriteDescriptorSet>   m_pushWrites;
	std::vector<uint32_t>               m_pushInfoIndices;

	bool m_bPushed = false;

	void BeginBatch();

	void ClearImpl() override
	{
		m_pushImageInfos.clear();
		m_pushBufferInfos.clear();
		m_pushWrites.clear();
		m_pushInfoIndices.clear();
		m_bPushed = false;
	}
};
