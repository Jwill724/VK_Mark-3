#include "pch.h"

#include "../Renderer.h"
#include "../backend/Device.h"
#include "../backend/BufferBarriers.h"
#include "../scene/LightingSystem.h"
#include "../core/JobSystem.h"
#include "../core/asset/AssetUploadTypes.h"
#include "../backend/memory/Budgets.h"

void Renderer::EndAssetTimer()
{
	auto elapsed = m_profiler.EndTimerSec();
	// Cheap check for now
	if (!m_materials.empty() && m_registeredMeshes.GetMeshCount() > 0)
	{
		fmt::print("Asset loading completed in {:.3f} seconds.\n\n", elapsed);
	}
}

void Renderer::UploadScenes(std::vector<SceneUploadBatch>&& batches)
{
	if (batches.empty()) return;

	// Build one ModelAsset per batch upfront
	std::vector<std::shared_ptr<ModelAsset>> assets;
	assets.reserve(batches.size());

	for (auto& batch : batches)
	{
		auto asset = std::make_shared<ModelAsset>();
		asset->sceneID = batch.sceneID;
		asset->sceneName = batch.sceneName;
		asset->lifetime = batch.lifetime;
		asset->instances = std::move(batch.instances);
		asset->nodeTransforms = std::move(batch.nodeTransforms);
		asset->localToNodeSlot = std::move(batch.localToNodeSlot);
		asset->lights = std::move(batch.lights);
		asset->virtualInstance = batch.virtualInstance;
		assets.push_back(asset);
	}

	// ---- Compute total staging size needed ----
	size_t totalTexBytes = 0;
	size_t totalVtxBytes = 0;
	size_t totalIdxBytes = 0;
	size_t totalMeshBytes = 0;
	size_t totalMatBytes = 0;
	size_t totalMeshletBytes = 0;
	size_t totalMLVertBytes = 0;
	size_t totalMLTriBytes = 0;

	auto& assetCounts = m_profiler.assetCounts;

	for (auto& batch : batches)
	{
		for (const auto& t : batch.textures)
			if (t.IsValid())
				totalTexBytes += AllocatedBuffer::AlignUp(t.pixelData.size(), 4u);

		// TODO: Eventually add a way to subtract from this initial count
		assetCounts.totalVertexCount += static_cast<uint32_t>(batch.vertices.size());
		assetCounts.totalIndexCount += static_cast<uint32_t>(batch.indices.size());
		assetCounts.totalMeshCount += static_cast<uint32_t>(batch.meshes.size());
		assetCounts.totalMaterialCount += static_cast<uint32_t>(batch.materials.size());

		totalVtxBytes += batch.vertices.size() * sizeof(Vertex);
		totalIdxBytes += batch.indices.size() * sizeof(uint32_t);
		totalMeshBytes += batch.meshes.size() * sizeof(Mesh);
		totalMatBytes += batch.materials.size() * sizeof(Material);
		totalMeshletBytes += batch.meshlets.size() * sizeof(Meshlet);
		totalMLVertBytes += batch.meshletVertices.size() * sizeof(uint32_t);
		totalMLTriBytes += batch.meshletTriangles.size() * sizeof(uint8_t);
	}

	const size_t totalNeeded =
		totalTexBytes + totalVtxBytes + totalIdxBytes
		+ totalMeshBytes + totalMatBytes + totalMeshletBytes
		+ totalMLTriBytes + totalMLVertBytes
		+ m_globalAddressTable.GPU_ADDRESS_TABLE_SIZE_GPU_BYTES;

	if (totalNeeded > m_allocator.GlobalStaging.GetCapacity())
		m_allocator.ResetGlobalStaging(totalNeeded, m_device->GetNonCoherentAtomSize());

	// ---- Textures — graphics queue (mip gen needs blit) ----
	{
		auto cmdPool = m_device->GetThreadCommandPool(
			JobSystem::RENDER_THREAD, QueueType::Graphics);

		m_device->RecordDeferredCommand([&](VkCommandBuffer cmd)
			{
				BatchUploadTextures(batches, assets, cmd);
			}, cmdPool, QueueType::Graphics);

		m_device->SubmitDeferredCommands(QueueType::Graphics);
		m_device->GetGraphicsQueue().WaitIdle();
		m_allocator.GlobalStaging.Reset();
	}

	// ---- Meshes — transfer queue ----
	{
		ASSERT(totalVtxBytes > 0);
		ASSERT(totalIdxBytes > 0);
		ASSERT(totalMeshBytes > 0);
		ASSERT(totalMeshletBytes > 0);
		ASSERT(totalMLVertBytes > 0);
		ASSERT(totalMLTriBytes > 0);

		// Allocate singular global buffers sized for ALL scenes combined
		m_globalAddressTable.AddGPUBufferToAddressTable(
			RD::Renderer_Buffer::Vertex, totalVtxBytes, m_allocator);

		m_globalAddressTable.AddGPUBufferToAddressTable(
			RD::Renderer_Buffer::Index, totalIdxBytes, m_allocator);

		m_globalAddressTable.AddGPUBufferToAddressTable(
			RD::Renderer_Buffer::Mesh, totalMeshBytes, m_allocator);

		m_globalAddressTable.AddGPUBufferToAddressTable(
			RD::Renderer_Buffer::Meshlet, totalMeshletBytes, m_allocator);

		m_globalAddressTable.AddGPUBufferToAddressTable(
			RD::Renderer_Buffer::MeshletVertices, totalMLVertBytes, m_allocator);

		m_globalAddressTable.AddGPUBufferToAddressTable(
			RD::Renderer_Buffer::MeshletTriangles, totalMLTriBytes, m_allocator);

		auto cmdPool = m_device->GetThreadCommandPool(
			JobSystem::RENDER_THREAD, QueueType::Transfer);

		m_device->RecordDeferredCommand([&](VkCommandBuffer cmd)
			{
				BatchUploadMeshes(batches, assets, cmd);
			}, cmdPool, QueueType::Transfer);

		m_device->SubmitDeferredCommands(QueueType::Transfer);
		m_device->GetTransferQueue().WaitIdle();
		m_allocator.GlobalStaging.Reset();
	}

	// ---- BLAS builds -----
	{
		auto cmdPool = m_device->GetThreadCommandPool(
			JobSystem::RENDER_THREAD, QueueType::Graphics);

		AllocatedBuffer scratch{};

		m_device->RecordDeferredCommand([&](VkCommandBuffer cmd)
			{
				m_blasAddresses = BuildMeshBLAS(cmd, scratch);
			}, cmdPool, QueueType::Graphics);

		m_device->SubmitDeferredCommands(QueueType::Graphics);
		m_device->GetGraphicsQueue().WaitIdle();
		m_allocator.FreeBuffer(scratch);

		if (!m_blasAddresses.empty())
		{
			m_globalAddressTable.AddGPUBufferToAddressTable(
				RD::Renderer_Buffer::BLASAddresses,
				m_blasAddresses.size() * sizeof(uint64_t),
				m_allocator);

			auto transferPool = m_device->GetThreadCommandPool(
				JobSystem::RENDER_THREAD, QueueType::Transfer);

			m_device->RecordDeferredCommand([&](VkCommandBuffer cmd)
				{
					auto write = m_allocator.GlobalStaging.Stage(
						m_blasAddresses.data(),
						m_blasAddresses.size() * sizeof(uint64_t),
						m_globalAddressTable.GetGPUBuffer(RD::Renderer_Buffer::BLASAddresses).m_buffer);

					m_allocator.GlobalStaging.Flush();
					m_allocator.GlobalStaging.CopyCommand(cmd, write);

					BufferBarriers::TransferReleaseOnGraphics(
						cmd,
						m_globalAddressTable.GetGPUBuffer(RD::Renderer_Buffer::BLASAddresses),
						m_device->GetContext());
				}, transferPool, QueueType::Transfer);

			m_device->SubmitDeferredCommands(QueueType::Transfer);
			m_device->GetTransferQueue().WaitIdle();
			m_allocator.GlobalStaging.Reset();
		}
	}

	// ---- Materials — transfer queue ----
	{
		ASSERT(totalMatBytes > 0);
		m_globalAddressTable.AddGPUBufferToAddressTable(
			RD::Renderer_Buffer::Material, totalMatBytes, m_allocator);

		BatchUploadMaterials(batches, assets);
	}

	// ---- Address table — single upload covering all new buffer pointers ----
	{
		auto cmdPool = m_device->GetThreadCommandPool(
			JobSystem::RENDER_THREAD, QueueType::Transfer);

		m_device->RecordDeferredCommand([&](VkCommandBuffer cmd)
			{
				auto write = m_allocator.GlobalStaging.Stage(
					m_globalAddressTable.GetAddrPtrTable().data(),
					m_globalAddressTable.GPU_ADDRESS_TABLE_SIZE_GPU_BYTES,
					m_globalAddressTable.GetTableBuffer().m_buffer);

				m_allocator.GlobalStaging.Flush();
				m_allocator.GlobalStaging.CopyCommand(cmd, write);
			}, cmdPool, QueueType::Transfer);

		m_device->SubmitDeferredCommands(QueueType::Transfer);
		m_device->GetTransferQueue().WaitIdle();
		m_allocator.GlobalStaging.Reset();
	}

	// Register all assets into World in one pass
	for (auto& asset : assets)
		World::OnSceneLoaded(asset);

	fmt::println("[Renderer] Uploaded {} scene(s).", batches.size());
}

void Renderer::BatchUploadTextures(
	std::vector<SceneUploadBatch>& batches,
	std::vector<std::shared_ptr<ModelAsset>>& assets,
	VkCommandBuffer                             cmd)
{
	for (size_t b = 0; b < batches.size(); ++b)
	{
		auto& batch = batches[b];
		auto& asset = *assets[b];

		if (batch.textures.empty()) continue;

		asset.ownedTextureSlots = m_bindlessImageTable.UploadAssetTextures(
			batch,
			m_device->GetContext().device,
			m_allocator,
			m_allocator.GlobalStaging,
			cmd);

		asset.textureBindlessIDs.resize(batch.textures.size(), UINT32_MAX);
		for (uint32_t i = 0; i < static_cast<uint32_t>(batch.textures.size()); ++i)
			asset.textureBindlessIDs[i] = batch.textures[i].bindlessID;
	}
}

void Renderer::BatchUploadMeshes(
	std::vector<SceneUploadBatch>& batches,
	std::vector<std::shared_ptr<ModelAsset>>& assets,
	VkCommandBuffer                             cmd)
{
	const auto& vtxBuf = m_globalAddressTable.GetGPUBuffer(RD::Renderer_Buffer::Vertex);
	const auto& idxBuf = m_globalAddressTable.GetGPUBuffer(RD::Renderer_Buffer::Index);
	const auto& meshBuf = m_globalAddressTable.GetGPUBuffer(RD::Renderer_Buffer::Mesh);
	const auto& meshletBuf = m_globalAddressTable.GetGPUBuffer(RD::Renderer_Buffer::Meshlet);
	const auto& meshletVertsBuf = m_globalAddressTable.GetGPUBuffer(RD::Renderer_Buffer::MeshletVertices);
	const auto& meshletTrisBuf = m_globalAddressTable.GetGPUBuffer(RD::Renderer_Buffer::MeshletTriangles);

	// Running cursors — each scene appends after the previous
	uint32_t globalVertexCursor = 0;
	uint32_t globalIndexCursor = 0;
	uint32_t globalMeshCursor = 0;
	uint32_t globalMeshletCursor = 0;
	uint32_t globalMeshletVertsCursor = 0;
	uint32_t globalMeshletTrisCursor = 0;

	// Staging offsets into the single global buffer
	size_t stagingVtxOffset = 0;
	size_t stagingIdxOffset = 0;
	size_t stagingMeshOffset = 0;
	size_t stagingMeshletOffset = 0;
	size_t stagingMeshletVertsOffset = 0;
	size_t stagingMeshletTrisOffset = 0;

	// Collect all GPU mesh structs into one flat array
	std::vector<Mesh> allGpuMeshes;
	std::vector<Meshlet> allMeshlets;

	for (size_t b = 0; b < batches.size(); ++b)
	{
		auto& batch = batches[b];
		auto& asset = *assets[b];

		if (batch.meshes.empty()) continue;

		const uint32_t localMeshBase = globalMeshCursor;
		const uint32_t localVertBase = globalVertexCursor;
		const uint32_t localIndexBase = globalIndexCursor;
		const uint32_t localMeshletBase = globalMeshletCursor;
		const uint32_t localMeshletVertsBase = globalMeshletVertsCursor;
		const uint32_t localMeshletTrisBase = globalMeshletTrisCursor;

		asset.meshGlobalIDs.reserve(batch.meshes.size());

		for (auto& md : batch.meshes)
		{
			// Adjust to global offsets
			md.firstIndex += localIndexBase;
			md.vertexOffset += localVertBase;
			md.shadowFirstIndex += localIndexBase;
			md.meshletOffset += localMeshletBase;
			md.shadowMeshletOffset += localMeshletBase;

			Mesh gpuMesh{};
			gpuMesh.firstIndex = md.firstIndex;
			gpuMesh.indexCount = md.indexCount;
			gpuMesh.vertexOffset = md.vertexOffset;
			gpuMesh.vertexCount = md.vertexCount;
			gpuMesh.shadowFirstIndex = md.shadowFirstIndex;
			gpuMesh.shadowIndexCount = md.shadowIndexCount;
			gpuMesh.localAABB = md.localAABB;
			gpuMesh.localBoundingRadius = md.localBoundingRadius;
			gpuMesh.meshletCount = md.meshletCount;
			gpuMesh.meshletOffset = md.meshletOffset;
			gpuMesh.shadowMeshletCount = md.shadowMeshletCount;
			gpuMesh.shadowMeshletOffset = md.shadowMeshletOffset;
			gpuMesh.meshletVisibilityBase = md.meshletVisibilityBase;

			const uint32_t globalID = m_registeredMeshes.RegisterMesh(gpuMesh);
			md.globalMeshID = globalID;
			asset.meshGlobalIDs.push_back(globalID);
			allGpuMeshes.push_back(gpuMesh);
		}

		for (auto& ml : batch.meshlets)
		{
			ml.vertexOffset += localMeshletVertsBase;
			ml.triangleOffset += localMeshletTrisBase;
			allMeshlets.push_back(ml);
		}

		m_registeredMeshes.ResizeMeshLods();
		for (uint32_t i = 0; i < static_cast<uint32_t>(batch.meshes.size()); ++i)
		{
			const auto& md = batch.meshes[i];
			auto& lods = m_registeredMeshes.GetLodsMutable()[md.globalMeshID];

			auto resolve = [&](uint32_t localIdx) -> uint32_t
				{
					if (localIdx == UINT32_MAX) return md.globalMeshID;
					return batch.meshes[localIdx].globalMeshID;
				};

			lods.lod0 = resolve(md.lod0);
			lods.lod1 = resolve(md.lod1);
			lods.lod2 = resolve(md.lod2);
			lods.lod3 = resolve(md.lod3);
			lods.shadowLod0 = resolve(md.shadowLod0);
			lods.shadowLod1 = resolve(md.shadowLod1);
			lods.shadowLod2 = resolve(md.shadowLod2);
			lods.flags = md.flags;
		}

		// Resolve instance local mesh -> global mesh ID
		for (auto& inst : asset.instances)
			if (inst.localMeshIdx != UINT32_MAX &&
				inst.localMeshIdx < static_cast<uint32_t>(batch.meshes.size()))
				inst.localMeshIdx = batch.meshes[inst.localMeshIdx].globalMeshID;

		globalVertexCursor += static_cast<uint32_t>(batch.vertices.size());
		globalIndexCursor += static_cast<uint32_t>(batch.indices.size());
		globalMeshCursor += static_cast<uint32_t>(batch.meshes.size());
		globalMeshletCursor += static_cast<uint32_t>(batch.meshlets.size());
		globalMeshletVertsCursor += static_cast<uint32_t>(batch.meshletVertices.size());
		globalMeshletTrisCursor += static_cast<uint32_t>(batch.meshletTriangles.size());
	}

	// Stage all scenes into the global buffers in one contiguous write per buffer
	size_t vtxOff = 0, idxOff = 0, mlVertOff = 0, mlTrisOff = 0;

	for (auto& batch : batches)
	{
		if (batch.vertices.empty() && batch.meshletVertices.empty()) continue;

		const size_t vBytes = batch.vertices.size() * sizeof(Vertex);
		const size_t iBytes = batch.indices.size() * sizeof(uint32_t);
		const size_t mlVertBytes = batch.meshletVertices.size() * sizeof(uint32_t);
		const size_t mlTrisBytes = batch.meshletTriangles.size() * sizeof(uint8_t);

		// Stage directly into the correct offset of the global GPU buffer
		auto vtxWrite = m_allocator.GlobalStaging.Stage(
			batch.vertices.data(), vBytes, vtxBuf.m_buffer, vtxOff);
		auto idxWrite = m_allocator.GlobalStaging.Stage(
			batch.indices.data(), iBytes, idxBuf.m_buffer, idxOff);

		auto mlVertWrite = m_allocator.GlobalStaging.Stage(
			batch.meshletVertices.data(), mlVertBytes, meshletVertsBuf.m_buffer, mlVertOff);
		auto mlTrisWrite = m_allocator.GlobalStaging.Stage(
			batch.meshletTriangles.data(), mlTrisBytes, meshletTrisBuf.m_buffer, mlTrisOff);

		m_allocator.GlobalStaging.CopyCommand(cmd, vtxWrite);
		m_allocator.GlobalStaging.CopyCommand(cmd, idxWrite);

		m_allocator.GlobalStaging.CopyCommand(cmd, mlVertWrite);
		m_allocator.GlobalStaging.CopyCommand(cmd, mlTrisWrite);

		vtxOff += vBytes;
		idxOff += iBytes;
		mlVertOff += mlVertBytes;
		mlTrisOff += mlTrisBytes;

		batch.vertices.clear(); batch.vertices.shrink_to_fit();
		batch.indices.clear();  batch.indices.shrink_to_fit();
		batch.meshletVertices.clear(); batch.meshletVertices.shrink_to_fit();
		batch.meshletTriangles.clear();  batch.meshletTriangles.shrink_to_fit();
	}

	// Stage the combined mesh array — already adjusted to global offsets
	if (!allGpuMeshes.empty())
	{
		const size_t totalMeshBytes = allGpuMeshes.size() * sizeof(Mesh);
		auto meshWrite = m_allocator.GlobalStaging.Stage(
			allGpuMeshes.data(), totalMeshBytes, meshBuf.m_buffer);
		m_allocator.GlobalStaging.Flush();
		m_allocator.GlobalStaging.CopyCommand(cmd, meshWrite);
	}

	if (!allMeshlets.empty())
	{
		const size_t totalMeshletBytes = allMeshlets.size() * sizeof(Meshlet);
		auto meshletWrite = m_allocator.GlobalStaging.Stage(
			allMeshlets.data(), totalMeshletBytes, meshletBuf.m_buffer);
		m_allocator.GlobalStaging.Flush();
		m_allocator.GlobalStaging.CopyCommand(cmd, meshletWrite);
	}
}

void Renderer::BatchUploadMaterials(
	std::vector<SceneUploadBatch>& batches,
	std::vector<std::shared_ptr<ModelAsset>>& assets)
{
	std::vector<Material> allGpuMaterials;

	for (size_t b = 0; b < batches.size(); ++b)
	{
		auto& batch = batches[b];
		auto& asset = *assets[b];

		if (batch.materials.empty()) continue;

		asset.materialGlobalIDs.reserve(batch.materials.size());

		auto resolve = [&](uint32_t localIdx, RD::Renderer_Texture errorTex) -> uint32_t
			{
				if (localIdx == UINT32_MAX ||
					localIdx >= static_cast<uint32_t>(asset.textureBindlessIDs.size()))
					return m_bindlessImageTable.GetStaticTexture(errorTex).m_bindlessID;
				return asset.textureBindlessIDs[localIdx];
			};

		for (auto& desc : batch.materials)
		{
			Material mat{};
			mat.albedoID = resolve(desc.albedoTexIdx, RD::Renderer_Texture::White);
			mat.metalRoughnessID = resolve(desc.metalRoughTexIdx, RD::Renderer_Texture::White);
			mat.normalID = resolve(desc.normalTexIdx, RD::Renderer_Texture::Normal);
			mat.emissiveID = resolve(desc.emissiveTexIdx, RD::Renderer_Texture::White);
			mat.colorFactor = desc.colorFactor;
			mat.metalRoughFactors = desc.metalRoughFactors;
			mat.emissiveColor = desc.emissiveColor;

			const bool hasEmissive = glm::dot(desc.emissiveColor, desc.emissiveColor) > 0.0f;
			mat.emissiveStrength = hasEmissive
				? desc.emissiveStrength * LightUnits::EM_LED_INDICATOR
				: 0.0f;

			mat.alphaCutoff = desc.alphaCutoff;
			mat.normalScale = desc.normalScale;
			mat.ior = desc.ior;
			mat.specularFactor = desc.specularFactor;
			mat.clearcoatFactor = desc.clearcoatFactor;
			mat.clearcoatRough = desc.clearcoatRough;
			mat.diffuseTransFactor = desc.diffuseTransFactor;
			mat.transmissionFactor = desc.transmissionFactor;
			mat.sheenColor = desc.sheenColor;
			mat.sheenRough = desc.sheenRough;
			mat.shadingModel = desc.shadingModel;
			mat.thicknessFactor = desc.thicknessFactor;
			mat.attenuationColor = desc.attenuationColor;
			mat.attenuationDistance = desc.attenuationDistance;

			const uint32_t globalID = static_cast<uint32_t>(m_materials.size());
			desc.globalMaterialID = globalID;
			asset.materialGlobalIDs.push_back(globalID);
			m_materials.push_back(mat);
			allGpuMaterials.push_back(mat);

			if (globalID >= static_cast<uint32_t>(m_materialFlagsIDs.size()))
				m_materialFlagsIDs.resize(static_cast<size_t>(globalID + 1), 0u);
			m_materialFlagsIDs[globalID] = desc.flags;
		}

		// Resolve instance local material -> global material ID
		for (auto& inst : asset.instances)
			if (inst.localMaterialIdx != UINT32_MAX &&
				inst.localMaterialIdx < static_cast<uint32_t>(batch.materials.size()))
				inst.localMaterialIdx = batch.materials[inst.localMaterialIdx].globalMaterialID;
	}

	if (allGpuMaterials.empty()) return;

	const size_t totalMatBytes = allGpuMaterials.size() * sizeof(Material);
	const auto& matBuf = m_globalAddressTable.GetGPUBuffer(RD::Renderer_Buffer::Material);

	auto cmdPool = m_device->GetThreadCommandPool(
		JobSystem::RENDER_THREAD, QueueType::Transfer);

	m_device->RecordDeferredCommand([&](VkCommandBuffer cmd)
		{
			auto matWrite = m_allocator.GlobalStaging.Stage(
				allGpuMaterials.data(), totalMatBytes, matBuf.m_buffer);
			m_allocator.GlobalStaging.Flush();
			m_allocator.GlobalStaging.CopyCommand(cmd, matWrite);
		}, cmdPool, QueueType::Transfer);

	m_device->SubmitDeferredCommands(QueueType::Transfer);
	m_device->GetTransferQueue().WaitIdle();
	m_allocator.GlobalStaging.Reset();
}

std::vector<uint64_t> Renderer::BuildMeshBLAS(VkCommandBuffer cmd, AllocatedBuffer& outScratch)
{
	const auto& meshes = m_registeredMeshes.GetMeshes();
	const auto& lods = m_registeredMeshes.GetLods();

	const VkDeviceAddress vtxAddr = m_globalAddressTable
		.GetGPUBuffer(RD::Renderer_Buffer::Vertex).m_address;
	const VkDeviceAddress idxAddr = m_globalAddressTable
		.GetGPUBuffer(RD::Renderer_Buffer::Index).m_address;

	std::vector<bool> needsBLAS(meshes.size(), false);
	for (size_t i = 0; i < meshes.size(); ++i)
	{
		if (lods[i].flags & MESH_FLAG_IS_LOD_VARIANT) continue;

		const uint32_t rtMesh = RTMeshID(
			static_cast<uint32_t>(i), lods[i].lod1, lods[i].flags);

		ASSERT(rtMesh < meshes.size());
		needsBLAS[rtMesh] = true;
	}

	std::vector<uint32_t> buildList;
	for (size_t i = 0; i < meshes.size(); ++i)
		if (needsBLAS[i] && meshes[i].indexCount >= 3)
			buildList.push_back(static_cast<uint32_t>(i));

	ASSERT(!buildList.empty());

	std::vector<VkAccelerationStructureGeometryKHR> geoms(buildList.size());
	std::vector<VkAccelerationStructureBuildGeometryInfoKHR> builds(buildList.size());
	std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges(buildList.size());
	std::vector<VkDeviceSize> sizes(buildList.size());

	VkDeviceSize totalASBytes = 0;
	VkDeviceSize maxScratch = 0;

	for (size_t i = 0; i < buildList.size(); ++i)
	{
		const Mesh& m = meshes[buildList[i]];

		auto& g = geoms[i];
		g = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
		g.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
		g.flags = 0;

		auto& tri = g.geometry.triangles;
		tri.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
		tri.pNext = nullptr;
		tri.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
		tri.vertexData.deviceAddress = vtxAddr + static_cast<VkDeviceSize>(m.vertexOffset) * sizeof(Vertex);
		tri.vertexStride = sizeof(Vertex);
		tri.maxVertex = m.vertexCount - 1;
		tri.indexType = VK_INDEX_TYPE_UINT32;
		tri.indexData.deviceAddress = idxAddr + static_cast<VkDeviceSize>(m.firstIndex) * sizeof(uint32_t);
		tri.transformData.deviceAddress = 0;

		auto& b = builds[i];
		b = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
		b.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		b.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
		b.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		b.geometryCount = 1;
		b.pGeometries = &geoms[i];

		const uint32_t primCount = m.indexCount / 3u;
		ranges[i] = { primCount, 0, 0, 0 };

		VkAccelerationStructureBuildSizesInfoKHR sizeInfo{
			VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
		vkGetAccelerationStructureBuildSizesKHR(
			m_device->GetContext().device,
			VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
			&b, &primCount, &sizeInfo);

		sizes[i] = AllocatedBuffer::AlignUp(sizeInfo.accelerationStructureSize, MIN_SSBO_ALIGNMENT_BYTES);
		totalASBytes += sizes[i];
		maxScratch = std::max(maxScratch, sizeInfo.buildScratchSize);
	}

	const VkDeviceSize scratchAlign = m_device->GetMinASScratchAlignment();

	m_blasStorage = m_allocator.AllocateBuffer({
		totalASBytes, Vulkan_BufferUsage::AS_STORAGE, HeapType::GPU_Local, false, "BLASStorage" });

	AllocatedBuffer scratch = m_allocator.AllocateBuffer({
		maxScratch + scratchAlign, Vulkan_BufferUsage::AS_SCRATCH, HeapType::GPU_Local, false, "BLASScratch" });

	const VkDeviceAddress scratchAddr =
		(scratch.m_address + scratchAlign - 1) & ~(scratchAlign - 1);

	std::vector<uint64_t> blasAddresses(meshes.size(), 0ull);
	m_blasHandles.resize(buildList.size());

	VkDeviceSize offset = 0;

	for (size_t i = 0; i < buildList.size(); ++i)
	{
		VkAccelerationStructureCreateInfoKHR ci{
			VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
		ci.buffer = m_blasStorage.m_buffer;
		ci.offset = offset;
		ci.size = sizes[i];
		ci.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

		VK_CHECK(vkCreateAccelerationStructureKHR(
			m_device->GetContext().device, &ci, nullptr, &m_blasHandles[i]));

		offset += sizes[i];

		builds[i].dstAccelerationStructure = m_blasHandles[i];
		builds[i].scratchData.deviceAddress = scratchAddr;

		ASSERT((scratchAddr % scratchAlign) == 0);
		ASSERT(scratchAddr + maxScratch <= scratch.m_address + scratch.m_bytesSize);

		const VkAccelerationStructureBuildRangeInfoKHR* pRange = &ranges[i];
		vkCmdBuildAccelerationStructuresKHR(cmd, 1, &builds[i], &pRange);
		BufferBarriers::ASBuildToASBuild(cmd);

		VkAccelerationStructureDeviceAddressInfoKHR addrInfo{
			VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
		addrInfo.accelerationStructure = m_blasHandles[i];

		blasAddresses[buildList[i]] = vkGetAccelerationStructureDeviceAddressKHR(
			m_device->GetContext().device, &addrInfo);
	}

	//fmt::println("[RT] BLAS built for {} of {} meshes", buildList.size(), meshes.size());

	outScratch = scratch;
	return blasAddresses;
}
