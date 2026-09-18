#include <pch.h>

#include "../descriptors/DescriptorWriter.h"
#include "../memory/AllocatedImage.h"
#include "../memory/AllocatedBuffer.h"
#include "../../RendererDefinitions.h"

namespace RD = RendererDefinitions;

// ------------------------
// Push descriptor writing
// ------------------------
void PushDescriptorWriter::Reserve(size_t descriptorCount)
{
	m_pushImageInfos.reserve(descriptorCount);
	m_pushWrites.reserve(descriptorCount);
	m_pushInfoIndices.reserve(descriptorCount);
}

void PushDescriptorWriter::BeginBatch()
{
	if (!m_bPushed) return;

	m_pushImageInfos.clear();
	m_pushBufferInfos.clear();
	m_pushWrites.clear();
	m_pushInfoIndices.clear();
	m_bPushed = false;
}

void PushDescriptorWriter::WritePushCombinedImage(
	uint32_t binding, VkImageView view, VkImageLayout layout, VkSampler sampler)
{
	ASSERT(view != VK_NULL_HANDLE && sampler != VK_NULL_HANDLE);
	BeginBatch();

	m_pushInfoIndices.push_back(static_cast<uint32_t>(m_pushImageInfos.size()));
	m_pushImageInfos.emplace_back(VkDescriptorImageInfo{ sampler, view, layout });

	m_pushWrites.emplace_back(VkWriteDescriptorSet{
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstBinding = binding,
		.descriptorCount = 1u,
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER });
}

void PushDescriptorWriter::WritePushSampledImage(
	uint32_t binding, VkImageView view, VkImageLayout layout)
{
	ASSERT(view != VK_NULL_HANDLE);
	BeginBatch();

	m_pushInfoIndices.push_back(static_cast<uint32_t>(m_pushImageInfos.size()));
	m_pushImageInfos.emplace_back(VkDescriptorImageInfo{ VK_NULL_HANDLE, view, layout });

	m_pushWrites.emplace_back(VkWriteDescriptorSet{
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstBinding = binding,
		.descriptorCount = 1u,
		.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE });
}

void PushDescriptorWriter::WritePushStorageImage(uint32_t binding, VkImageView view)
{
	ASSERT(view != VK_NULL_HANDLE);
	BeginBatch();

	m_pushInfoIndices.push_back(static_cast<uint32_t>(m_pushImageInfos.size()));
	m_pushImageInfos.emplace_back(
		VkDescriptorImageInfo{ VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_GENERAL });

	m_pushWrites.emplace_back(VkWriteDescriptorSet{
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstBinding = binding,
		.descriptorCount = 1u,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE });
}

void PushDescriptorWriter::WritePushUniformBuffer(
	uint32_t binding, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range)
{
	ASSERT(buffer != VK_NULL_HANDLE && range > 0);
	BeginBatch();

	m_pushInfoIndices.push_back(static_cast<uint32_t>(m_pushBufferInfos.size()));
	m_pushBufferInfos.emplace_back(VkDescriptorBufferInfo{ buffer, offset, range });

	m_pushWrites.emplace_back(VkWriteDescriptorSet{
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstBinding = binding,
		.descriptorCount = 1u,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER });
}

void PushDescriptorWriter::WritePushImage(
	uint32_t binding,
	const AllocatedImage& image,
	VkImageLayout layout,
	VkSampler sampler,
	uint32_t storageViewIndex)
{
	ASSERT(image.m_imageView != VK_NULL_HANDLE);

	VkImageView view = image.m_imageView;
	if (storageViewIndex != UINT32_MAX && storageViewIndex < image.m_vStorageViews.size())
		view = image.m_vStorageViews[storageViewIndex];

	if (sampler != VK_NULL_HANDLE)
		WritePushCombinedImage(binding, view, layout, sampler);
	else
		WritePushStorageImage(binding, view);
}

void PushDescriptorWriter::UpdatePushLayout(
	VkCommandBuffer cmd,
	VkPipelineBindPoint bindPoint,
	VkPipelineLayout pipelineLayout,
	uint32_t setIndex)
{
	if (m_pushWrites.empty()) return;

	ASSERT(pipelineLayout != VK_NULL_HANDLE);

	for (size_t i = 0; i < m_pushWrites.size(); ++i)
	{
		VkWriteDescriptorSet& w = m_pushWrites[i];

		if (w.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
			w.pBufferInfo = &m_pushBufferInfos[m_pushInfoIndices[i]];
		else
			w.pImageInfo = &m_pushImageInfos[m_pushInfoIndices[i]];
	}

	vkCmdPushDescriptorSet(
		cmd,
		bindPoint,
		pipelineLayout,
		setIndex,
		static_cast<uint32_t>(m_pushWrites.size()),
		m_pushWrites.data());

	m_bPushed = true;
}

// ---------------------------------
// Bindless descriptor set writing
// ---------------------------------
void DescriptorWriter::WriteBuffer(
	uint32_t binding,
	const AllocatedBuffer& buffer,
	VkDescriptorSet set,
	VkDescriptorType type)
{
	ASSERT(buffer.m_buffer != VK_NULL_HANDLE);
	ASSERT(buffer.m_bytesSize != 0);
	ASSERT(set != VK_NULL_HANDLE);

	if (type == VK_DESCRIPTOR_TYPE_MAX_ENUM)
	{
		switch (binding)
		{
		case RD::ADDRESS_TABLE_BINDING:
			type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			break;

		case RD::FRAME_BINDING_SCENE:
		case RD::FRAME_BINDING_CSM:
		case RD::FRAME_BINDING_VOLUMETRIC:
		case RD::FRAME_BINDING_CLUSTERED:
			type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			break;

		default:
			ASSERT(false && "Specify the buffer descriptor type.");
			return;
		}
	}

	const size_t bufferIndex = m_bufferInfos.size();

	m_bufferInfos.emplace_back(
		buffer.m_buffer,
		0,
		buffer.m_bytesSize);

	m_bufferWrites.emplace_back(VkWriteDescriptorSet{
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = set,
		.dstBinding = binding,
		.descriptorCount = 1u,
		.descriptorType = type,
		.pBufferInfo = nullptr,
		});

	m_writeBufferIndices.push_back(bufferIndex);
}

void DescriptorWriter::WriteBindlessImages(
	std::span<const VkDescriptorImageInfo> images,
	uint32_t binding,
	VkDescriptorSet set,
	Vulkan_DescriptorType type)
{
	if (images.empty()) return;

	auto& group = m_imageWriteGroups.emplace_back();

	group.binding = binding;
	group.type = static_cast<VkDescriptorType>(type);
	group.dstSet = set;

	group.imageInfos.assign(
		images.begin(),
		images.end());
}

// Special instant inline set update
void DescriptorWriter::WriteInlineUniform(
	VkDevice device,
	VkDescriptorSet unifiedSet, // Hard defined set
	const void* data,
	uint32_t size)
{
	VkWriteDescriptorSetInlineUniformBlock inlineBlock
	{
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_INLINE_UNIFORM_BLOCK,
		.pNext = nullptr,
		.dataSize = size,
		.pData = data
	};

	VkWriteDescriptorSet write
	{
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.pNext = &inlineBlock,
		.dstSet = unifiedSet,
		.dstBinding = RendererDefinitions::GLOBAL_BINDING_DEBUG_INLINE,
		.descriptorCount = size,
		.descriptorType = VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK
	};

	vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

void DescriptorWriter::WriteAccelerationStructure(
	uint32_t binding,
	VkAccelerationStructureKHR tlas,
	VkDescriptorSet set)
{
	VkAccelerationStructureKHR& handle = m_accelHandles.emplace_back(tlas);

	VkWriteDescriptorSetAccelerationStructureKHR& asInfo = m_accelInfos.emplace_back(
		VkWriteDescriptorSetAccelerationStructureKHR{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
			.accelerationStructureCount = 1,
			.pAccelerationStructures = &handle
		});

	m_accelWrites.emplace_back(VkWriteDescriptorSet{
		.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.pNext           = &asInfo,
		.dstSet          = set,
		.dstBinding      = binding,
		.descriptorCount = 1,
		.descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR
	});
}

void DescriptorWriter::UpdateSet(VkDevice device, VkDescriptorSet set)
{
	std::vector<VkWriteDescriptorSet> imageWrites;
	if (!m_imageWriteGroups.empty())
	{
		imageWrites.reserve(m_imageWriteGroups.size());
		for (const auto& group : m_imageWriteGroups)
		{
			imageWrites.emplace_back(VkWriteDescriptorSet{
				.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet          = group.dstSet,
				.dstBinding      = group.binding,
				.descriptorCount = static_cast<uint32_t>(group.imageInfos.size()),
				.descriptorType  = group.type,
				.pImageInfo      = group.imageInfos.data()
			});
		}

		vkUpdateDescriptorSets(
			device,
			static_cast<uint32_t>(imageWrites.size()),
			imageWrites.data(),
			0,
			nullptr);
		m_bShouldClearWrites = true;
	}

	if (!m_bufferWrites.empty())
	{
		for (size_t i = 0; i < m_bufferWrites.size(); ++i)
		{
			m_bufferWrites[i].dstSet = set;
			m_bufferWrites[i].pBufferInfo = &m_bufferInfos[m_writeBufferIndices[i]];
		}

		vkUpdateDescriptorSets(
			device,
			static_cast<uint32_t>(m_bufferWrites.size()),
			m_bufferWrites.data(),
			0,
			nullptr);
		m_bShouldClearWrites = true;
	}

	if (!m_accelWrites.empty())
	{
		for (auto& w : m_accelWrites)
			w.dstSet = set;

		vkUpdateDescriptorSets(
			device,
			static_cast<uint32_t>(m_accelWrites.size()),
			m_accelWrites.data(),
			0,
			nullptr);
		m_bShouldClearWrites = true;
	}

	if (m_bShouldClearWrites) Clear();
}
