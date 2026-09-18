#pragma once

#include "VmaForward.h"
#include "../VulkanForward.h"
#include <string>
#include "EngineTypes.h"

enum class ImageAspect
{
	Color,
	Depth
};

struct AllocatedImage
{
	VkImage                  m_image      = VK_NULL_HANDLE;
	VkImageView              m_imageView  = VK_NULL_HANDLE;
	VmaAllocation            m_allocation = VK_NULL_HANDLE;

	std::vector<VkImageView> m_vStorageViews{};
	bool                     m_bPerMipStorageViews = false;

	ImageAspect              m_aspect        = ImageAspect::Color;
	bool                     m_isFirstLayout = false; // Undefined at creation
	uint32_t                 m_pixelBytes    = 0;

	Extents3D                m_extent        = { 0, 0, 0 };

	uint32_t                 m_mipLevels     = 0;
	uint32_t                 m_arrayLayers   = 1;
	uint32_t                 m_bindlessID    = UINT32_MAX;
	bool                     m_bIsMipmapped  = false;
	bool                     m_bIsCubemap    = false;
	std::string              m_name;

	uint32_t Width()  const { return m_extent.Width(); }
	uint32_t Height() const { return m_extent.Height(); }
	uint32_t Depth()  const { return m_extent.Depth(); }

	bool IsValid() const noexcept { return m_image != VK_NULL_HANDLE; }
	void Reset()   { *this = AllocatedImage{}; }
};

struct EnvironmentSet
{
	struct alignas(16) SpecularPrefilterPush
	{
		float    roughness;
		uint32_t width;
		uint32_t height;
		uint32_t sampleCount;
	};

	AllocatedImage specular{};
	AllocatedImage skybox{};
	AllocatedImage equirect{}; // Transient, used during prefilter bake
	uint32_t       setIndex = UINT32_MAX; // Maps to sh irradiance too
	std::vector<SpecularPrefilterPush> specularPCs{};

	float skyScale = 1.0f;

	bool IsValid() const noexcept { return setIndex != UINT32_MAX; }
	void Reset()   { *this = EnvironmentSet{}; }
};

struct AssetTextureEntry
{
	uint32_t    tableIndex  = UINT32_MAX; // index into m_assetTextures
	uint32_t    bindlessID  = UINT32_MAX; // cached for fast material resolve
	uint32_t    refCount    = 0;          // how many scenes reference this
};
