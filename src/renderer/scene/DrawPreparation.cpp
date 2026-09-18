#include "pch.h"

#include "DrawPreparation.h"
#include "Material.h"
#include "Mesh.h"
#include "../backend/Device.h"
#include "../backend/memory/ResourceAllocator.h"
#include "../backend/BufferBarriers.h"
#include "../../core/asset/AssetUploadTypes.h"
#include "../../common/ResourceTypes.h"
#include "../frame/FrameContext.h"
#include "renderer/RendererDefinitions.h"
#include "Scene.h"

namespace RD = RendererDefinitions;

static constexpr bool IsDynamicMethod(const VirtualInstance& gi)
{
	return gi.instancingMethod == RD::InstancingMethod::DrawDynamic ||
		   gi.instancingMethod == RD::InstancingMethod::DrawMultiDynamic;
}

static uint32_t AssignMeshletVisibilityOffsets(
	std::vector<InstanceInput>& rows,
	const std::vector<Mesh>& meshData)
{
	constexpr uint32_t MESHLET_HISTORY_MIN_BITS = 16u;

	uint32_t cursor = 0u;

	for (InstanceInput& row : rows)
	{
		const uint32_t chain[4] = {
			row.lod0,
			row.lod1,
			row.lod2,
			row.lod3
		};
		uint32_t reserve = 0u;
		for (uint32_t i = 0; i < 4u; ++i)
		{
			if (chain[i] >= meshData.size()) continue;
			const Mesh& m = meshData[chain[i]];
			reserve = std::max(reserve, m.meshletVisibilityBase + m.meshletCount);
		}

		if (reserve < MESHLET_HISTORY_MIN_BITS || cursor + reserve > RD::MAX_MESHLET_VISIBILITY_BITS)
		{
			row.meshletVisibilityOffset = RD::INVALID_U32;
			continue;
		}

		row.meshletVisibilityOffset = cursor;
		cursor += reserve;
	}

	return cursor;
}

static void BakeInstanceData(
	InstanceState& vs,
	const VirtualInstance& gi,
	const ModelAsset& asset,
	const std::vector<Mesh>& meshData,
	const std::vector<MeshLODs>& meshLods,
	const std::vector<glm::mat4>& transforms,
	const std::vector<uint32_t>& materialFlags,
	uint32_t& outFirst,
	uint32_t& outCount)
{
	const uint32_t stride = gi.perInstanceStride;
	const uint32_t copies = gi.usedCopies;

	ASSERT(!transforms.empty());
	ASSERT(stride > 0);
	ASSERT(copies >= 1);
	ASSERT(gi.transformCount > 0);
	ASSERT(gi.capacityCopies >= copies);
	ASSERT(stride == static_cast<uint32_t>(asset.instances.size()));

	const uint32_t slabTransformCount = gi.transformCount * gi.capacityCopies;
	const uint32_t slabBegin = gi.firstTransform;
	const uint32_t slabEnd = slabBegin + slabTransformCount;
	ASSERT(slabBegin < transforms.size());
	ASSERT(slabEnd <= transforms.size());

	outFirst = static_cast<uint32_t>(vs.gpuInputs.size());
	outCount = copies * stride;

	const size_t newSize = static_cast<size_t>(outFirst + outCount);
	vs.gpuInputs.resize(newSize);

	uint32_t writeIndex = outFirst;
	const uint32_t usedEnd = slabBegin + gi.transformCount * copies;

	for (uint32_t copyIndex = 0; copyIndex < copies; ++copyIndex)
	{
		for (uint32_t localIndex = 0; localIndex < stride; ++localIndex, ++writeIndex)
		{
			const InstanceDesc& instDesc = asset.instances[localIndex];

			const uint32_t nodeSlot = asset.localToNodeSlot[localIndex];
			ASSERT(nodeSlot < gi.transformCount);

			const uint32_t rawIdx = gi.firstTransform + copyIndex * gi.transformCount + nodeSlot;
			ASSERT(rawIdx >= slabBegin && rawIdx < usedEnd);

			const uint32_t transformID = IsDynamicMethod(gi)
				? (rawIdx | RD::TRANSFORM_DYNAMIC_BIT)
				: rawIdx;

			const uint32_t meshID = instDesc.localMeshIdx;
			ASSERT(meshID < meshData.size());

			MeshLODs lods = meshLods[meshID];

			const uint32_t matID = instDesc.localMaterialIdx;
			const uint32_t matFlags = (matID < materialFlags.size()) ? materialFlags[matID] : 0u;

			uint32_t flags = 0;

			// --- Layer 1: pass routing (from primitive) ---
			if (instDesc.passType == static_cast<uint32_t>(MaterialPass::Opaque))
				flags |= InstanceFlags::PASS_OPAQUE;
			else
				flags |= InstanceFlags::PASS_TRANSPARENT;

			// --- Layer 1: material-driven ---
			if (matFlags & MATERIAL_FLAG_ALPHA_MASKED)
				flags |= InstanceFlags::ALPHA_TESTED;

			if (matFlags & MATERIAL_FLAG_DOUBLE_SIDED)
				flags |= InstanceFlags::DOUBLE_SIDED;

			if (matFlags & MATERIAL_FLAG_IS_TREE)
				flags |= InstanceFlags::IS_TREE;

			if (matFlags & MATERIAL_FLAG_HAS_NORMAL_MAP)
				flags |= InstanceFlags::HAS_NORMALS;

			if (matFlags & MATERIAL_FLAG_TRANSMISSIVE)
				flags |= InstanceFlags::TRANSMISSIVE;

			// sun shadow casting
			// opaque only, stripped from BLEND MATERIALS and INSTANCING
			if ((flags & InstanceFlags::PASS_OPAQUE) &&
				(matFlags & MATERIAL_FLAG_CASTS_SHADOWS) &&
				!(gi.instancingMethod == RD::InstancingMethod::DrawMultiStatic) &&
				!(gi.instancingMethod == RD::InstancingMethod::DrawMultiDynamic))
			{
				flags |= InstanceFlags::CAST_CSM;
			}

			if ((flags & InstanceFlags::PASS_OPAQUE) &&
				(matFlags & MATERIAL_FLAG_CASTS_SHADOWS))
			{
				flags |= InstanceFlags::CAST_FLASHLIGHT;
			}

			// shadow receiving — all opaque geometry
			if (flags & InstanceFlags::PASS_OPAQUE)
				flags |= InstanceFlags::RECEIVE_SHADOW;

			// --- Layer 1: mesh-driven ---
			if (lods.flags & MESH_FLAG_IS_LOD)
				flags |= InstanceFlags::LOD_ENABLED;

			if (lods.flags & MESH_FLAG_GOOD_OCCLUDEE)
				flags |= InstanceFlags::OCCLUDABLE;

			// --- Layer 2: placement-driven ---
			if (gi.instancingMethod == RD::InstancingMethod::DrawStatic ||
				gi.instancingMethod == RD::InstancingMethod::DrawMultiStatic)
				flags |= InstanceFlags::STATIC_OBJECT;
			else
				flags |= InstanceFlags::DYNAMIC_OBJECT;

			// TLAS — opaque only, stripped from mass instancing
			const bool rtMassInstanced =
				gi.instancingMethod == RD::InstancingMethod::DrawMultiStatic ||
				gi.instancingMethod == RD::InstancingMethod::DrawMultiDynamic;

			const bool rtEligible =
				((flags & InstanceFlags::PASS_OPAQUE) || (flags & InstanceFlags::TRANSMISSIVE)) &&
				!rtMassInstanced;

			if (rtEligible)
				flags |= InstanceFlags::RT_VISIBLE;

			// all instances start active; lazy shrink clears this
			flags |= InstanceFlags::INSTANCE_ACTIVE;

			// --- Layer 3: runtime override from VirtualInstance ---
			flags = (flags & gi.flagsMask) | gi.flagsForce;

			InstanceInput row{};
			row.meshID = meshID;
			row.materialID = instDesc.localMaterialIdx;
			row.transformID = transformID;
			row.lod0 = lods.lod0;
			row.lod1 = lods.lod1;
			row.lod2 = lods.lod2;
			row.lod3 = lods.lod3;
			row.shadowLod0 = lods.shadowLod0;
			row.shadowLod1 = lods.shadowLod1;
			row.shadowLod2 = lods.shadowLod2;
			row.flags = flags;
			row.rtMeshID = RTMeshID(meshID, lods.lod1, lods.flags);

			vs.gpuInputs[writeIndex] = row;
		}
	}

	vs.slabs[static_cast<ModelID>(gi.sceneID)] = {
		.first = outFirst,
		.stride = stride,
		.usedCopies = copies
	};
}

// Toggles INSTANCE_ACTIVE when a dynamic placement's copy count changes
// Returns true if any row changed (caller must re-upload instance inputs).
static bool SyncCopyCount(InstanceState& vs, CoreSlab& slab, const VirtualInstance& gi)
{
	if (slab.usedCopies == gi.usedCopies)
		return false;

	ASSERT(gi.usedCopies <= gi.capacityCopies);

	const uint32_t stride = slab.stride;

	for (uint32_t c = 0; c < gi.capacityCopies; ++c)
	{
		const bool shouldBeActive = (c < gi.usedCopies);
		uint32_t idx = slab.first + c * stride;

		for (uint32_t local = 0; local < stride; ++local, ++idx)
		{
			uint32_t& flags = vs.gpuInputs[idx].flags;

			if (shouldBeActive)
				flags |= InstanceFlags::INSTANCE_ACTIVE;
			else
				flags &= ~InstanceFlags::INSTANCE_ACTIVE;
		}
	}

	slab.usedCopies = gi.usedCopies;
	return true;
}

//static bool UpdateWorldAABBsForDynamic(
//	InstanceState& vs,
//	const VirtualInstance& gi,
//	const std::vector<Mesh>& meshData,
//	const std::vector<glm::mat4>& transforms)
//{
//	if (!IsDynamicMethod(gi))
//		return false;
//
//	const auto it = vs.slabs.find(static_cast<ModelID>(gi.sceneID));
//	if (it == vs.slabs.end()) return false;
//
//	const CoreSlab& slab = it->second;
//	if (slab.usedCopies == 0) return false;
//
//	// Only the active (used) copies need fresh AABBs; inactive tail rows are
//	// gated out of the active list and by INSTANCE_ACTIVE anyway.
//	const uint32_t rowCount = slab.usedCopies * slab.stride;
//	uint32_t idx = slab.first;
//
//	for (uint32_t i = 0; i < rowCount; ++i, ++idx)
//	{
//		const uint32_t meshID = vs.gpuInputs[idx].meshID;
//		const uint32_t tid    = vs.gpuInputs[idx].transformID;
//
//		ASSERT(meshID < meshData.size());
//		ASSERT(tid < transforms.size());
//
//		vs.worldAABBs[idx] = AABBtoWorldSpace(
//			meshData[meshID].localAABB, transforms[tid]);
//	}
//
//	return rowCount > 0;
//}

static uint32_t RebuildActive(InstanceState& vs, const std::vector<uint64_t>& blasAddresses)
{
	vs.active.clear();
	vs.rtRows.clear();

	uint32_t rtCandidates = 0;
	uint32_t rtMissingBlas = 0;

	for (auto& [sid, slab] : vs.slabs)
	{
		const uint32_t stride = slab.stride;
		for (uint32_t c = 0; c < slab.usedCopies; ++c)
		{
			for (uint32_t local = 0; local < stride; ++local)
			{
				const uint32_t idx = slab.first + c * stride + local;
				vs.active.push_back(idx);

				const InstanceInput& row = vs.gpuInputs[idx];

				constexpr uint32_t need = InstanceFlags::RT_VISIBLE | InstanceFlags::INSTANCE_ACTIVE;
				if ((row.flags & need) != need) continue;

				++rtCandidates;

				if (row.rtMeshID >= blasAddresses.size() ||
					blasAddresses[row.rtMeshID] == 0ull)
				{
					++rtMissingBlas;
					continue;
				}

				if (vs.rtRows.size() < RD::MAX_RT_INSTANCES)
					vs.rtRows.push_back(idx);
			}
		}
	}

	if (rtMissingBlas > 0)
		fmt::println("[RT] {} of {} eligible instances have no BLAS for their RT mesh",
			rtMissingBlas, rtCandidates);

	if (rtCandidates - rtMissingBlas > RD::MAX_RT_INSTANCES)
		fmt::println("[RT] {} eligible instances exceeds MAX_RT_INSTANCES ({}); clamping.",
			rtCandidates - rtMissingBlas, RD::MAX_RT_INSTANCES);

	return static_cast<uint32_t>(vs.rtRows.size());
}

static uint32_t RegisterBin(BinTableBuild& table, uint32_t meshID, uint32_t materialID)
{
	uint32_t h = BinHash(meshID, materialID);

	for (uint32_t probe = 0; probe < RD::BIN_TABLE_SIZE; ++probe)
	{
		BinKey& slot = table.binKeys.hashTable[h];

		if (slot.meshID == meshID && slot.materialID == materialID) return slot.binID;

		if (slot.meshID == RD::INVALID_U32)
		{
			ASSERT(table.binCount < RD::MAX_DRAW_BINS, "Exceeded MAX_DRAW_BINS");

			slot.meshID     = meshID;
			slot.materialID = materialID;
			slot.binID      = table.binCount;

			table.binKeys.denseKeys[table.binCount] = { meshID, materialID };
			return table.binCount++;
		}

		h = (h + 1u) & (RD::BIN_TABLE_SIZE - 1u);
	}

	ASSERT(false, "Bin hash table full");
	return RD::INVALID_U32;
}

BinTableBuild DrawPreparation::BuildDrawBinTable(const std::vector<InstanceInput>& instances)
{
	BinTableBuild table;
	table.binKeys.hashTable.resize(RD::BIN_TABLE_SIZE, { RD::INVALID_U32, 0u, 0u });
	table.binKeys.denseKeys.resize(RD::MAX_DRAW_BINS, glm::uvec2(RD::INVALID_U32));

	for (const InstanceInput& inst : instances)
	{
		const uint32_t mat = inst.materialID;

		// primary LOD chain — exactly what selectLOD can return
		if (inst.flags & LOD_ENABLED)
		{
			RegisterBin(table, inst.lod0, mat);
			RegisterBin(table, inst.lod1, mat);
			RegisterBin(table, inst.lod2, mat);
			RegisterBin(table, inst.lod3, mat);
		}
		else
		{
			RegisterBin(table, inst.meshID, mat);
		}

		// shadow LOD chain — exactly what selectShadowLOD / flashlight can return
		if (inst.flags & (CAST_CSM | CAST_FLASHLIGHT))
		{
			RegisterBin(table, inst.shadowLod0, mat);
			RegisterBin(table, inst.shadowLod1, mat);
			RegisterBin(table, inst.shadowLod2, mat);
		}
	}

	return table;
}

bool DrawPreparation::SyncInstanceInputs(
	InstanceState& vs,
	const Scene& scene,
	const std::unordered_map<ModelID, std::shared_ptr<ModelAsset>>& loaded,
	const std::vector<Mesh>& meshData,
	const std::vector<MeshLODs>& meshLods,
	const std::vector<uint32_t>& materialFlags,
	const std::vector<uint64_t>& blasAddresses)
{
	bool gpuInputsDirty = false;

	const auto& virtualInstances = scene.GetVirtualInstances();

	for (const auto& gi : virtualInstances)
	{
		const ModelID sid = static_cast<ModelID>(gi.sceneID);
		auto assetIt = loaded.find(sid);
		if (assetIt == loaded.end()) continue;

		const ModelAsset& asset = *assetIt->second;
		if (!asset.IsLoaded()) continue;

		ASSERT(gi.perInstanceStride == static_cast<uint32_t>(asset.instances.size()));

		auto slabIt = vs.slabs.find(sid);

		const bool bDynamic =
			gi.instancingMethod == RD::InstancingMethod::DrawDynamic ||
			gi.instancingMethod == RD::InstancingMethod::DrawMultiDynamic;

		const auto& transforms = scene.GetTransformPool(bDynamic);

		if (slabIt == vs.slabs.end())
		{
			uint32_t f = 0, c = 0;
			BakeInstanceData(vs, gi, asset, meshData, meshLods, transforms, materialFlags, f, c);
			gpuInputsDirty = true;
			continue;
		}

		CoreSlab& slab = slabIt->second;

		if (SyncCopyCount(vs, slab, gi)) gpuInputsDirty = true;

		//UpdateWorldAABBsForDynamic(vs, gi, meshData, transforms);
	}

	if (gpuInputsDirty)
	{
		vs.rtInstanceCount = RebuildActive(vs, blasAddresses);

		AssignMeshletVisibilityOffsets(vs.gpuInputs, meshData);
	}

	return gpuInputsDirty;
}


void DrawPreparation::UploadGPUBuffersForFrame(
	FrameContext&                     frameCtx,
	BindlessBDATable&                 globalBDATable,
	const DrawBinKeys&                drawBinKeys,
	Device&                           device,
	Allocator&                        allocator,
	const std::vector<InstanceInput>& instanceInputs,
	const std::vector<uint32_t>&      rtRows,
	Scene&                            scene,
	const std::vector<LocalLight>&    lights,
	const glm::vec4                   defaultLuminance,
	bool                              bMotionNeeded)
{
	auto& frameStaging  = allocator.FrameStaging;
	auto& globalStaging = allocator.GlobalStaging;
	auto& frameAddrTable = frameCtx.GetBindlessBDATable();

	const auto& dynamicTransforms = scene.GetDynamicTransforms();
	const auto& motionMatrices    = scene.GetMotionMatrices();
	const auto& staticTransforms  = scene.GetStaticTransforms();

	const bool uploadInstances = frameCtx.IsInstanceInputsUploadNeeded();
	const bool uploadLights = frameCtx.IsLightsUploadNeeded();
	const bool uploadStatic = scene.IsStaticTransformsDirty();
	const bool uploadLuminance = frameCtx.IsLuminanceResetNeeded();

	const bool uploadDynamic = !dynamicTransforms.empty();
	const bool uploadMotion = uploadDynamic && bMotionNeeded && !motionMatrices.empty();

	if (!uploadInstances && !uploadLights && !uploadStatic && !uploadLuminance &&
		!uploadDynamic && !frameAddrTable.IsTableDirty()) return;

	struct UploadPlan
	{
		StagedWrite instanceInputs{};
		StagedWrite rtRows{};
		StagedWrite drawBinKeys{};
		StagedWrite drawBinKeysDense{};
		StagedWrite globalAddrTable{};
		StagedWrite staticTransforms{};
		//uint32_t    globalAddrVersion = UINT32_MAX;
		//bool        hasGlobalAddrTable = false;

		StagedWrite luminance{};
		bool        hasLuminance = false;

		bool instanceUploadNeeded = false;
		bool hasStaticTransforms  = false;

		StagedWrite dynamicTransforms{};
		StagedWrite motionMatrices{};
		StagedWrite lights{};
		StagedWrite frameAddrTable{};
		uint32_t    frameAddrVersion = UINT32_MAX;

		bool hasDynamicTransforms = false;
		bool hasMotionMatrices    = false;
		bool hasLights            = false;
		bool hasFrameAddrTable    = false;
	} plan;

	// Global buffers for instance inputs and draw bin keys
	if (frameCtx.IsInstanceInputsUploadNeeded() &&
		!instanceInputs.empty() &&
		!drawBinKeys.hashTable.empty() &&
		!drawBinKeys.denseKeys.empty())
	{
		const size_t instBytes     = instanceInputs.size()        * sizeof(InstanceInput);
		const size_t hashBytes     = drawBinKeys.hashTable.size() * sizeof(BinKey);
		const size_t denseBytes    = drawBinKeys.denseKeys.size() * sizeof(glm::uvec2);
		const size_t rtRowBytes    = rtRows.size()                * sizeof(uint32_t);

		VkBuffer binKeyBuf = globalBDATable.GetGPUBuffer(RD::Renderer_Buffer::DrawBinKeys).m_buffer;

		plan.instanceInputs = frameStaging.Stage(
			instanceInputs.data(),
			instBytes,
			globalBDATable.GetGPUBuffer(RD::Renderer_Buffer::InstanceInputs).m_buffer);

		plan.rtRows = frameStaging.Stage(
			rtRows.data(),
			rtRowBytes,
			globalBDATable.GetGPUBuffer(RD::Renderer_Buffer::RTRows).m_buffer);

		// hash table at offset 0
		plan.drawBinKeys = frameStaging.Stage(
			drawBinKeys.hashTable.data(),
			hashBytes,
			binKeyBuf,
			0);

		plan.drawBinKeysDense = frameStaging.Stage(
			drawBinKeys.denseKeys.data(),
			denseBytes,
			binKeyBuf,
			hashBytes);  // offset = end of hash table

		plan.globalAddrTable = frameStaging.Stage(
			globalBDATable.GetAddrPtrTable().data(),
			globalBDATable.GPU_ADDRESS_TABLE_SIZE_GPU_BYTES,
			globalBDATable.GetTableBuffer().m_buffer);

		plan.instanceUploadNeeded = true;
	}

	// Transforms
	if (uploadDynamic)
	{
		plan.dynamicTransforms = frameStaging.Stage(
			dynamicTransforms.data(),
			dynamicTransforms.size() * sizeof(glm::mat4),
			frameAddrTable.GetGPUBuffer(RD::Renderer_Buffer::DynamicTransforms).m_buffer);

		plan.hasDynamicTransforms = true;
	}

	if (uploadMotion)
	{
		plan.motionMatrices = frameStaging.Stage(
			motionMatrices.data(),
			motionMatrices.size() * sizeof(glm::mat4),
			frameAddrTable.GetGPUBuffer(RD::Renderer_Buffer::MotionMatrices).m_buffer);

		plan.hasMotionMatrices = true;
	}

	if (uploadStatic)
	{
		const DirtyRange range = scene.GetStaticDirtyRange();
		const size_t bytes     = static_cast<size_t>(range.count) * sizeof(glm::mat4);
		const size_t dstOffset = static_cast<size_t>(range.offset) * sizeof(glm::mat4);

		if (!globalStaging.CanFit(bytes))
		{
			device.GetTransferQueue().WaitIdle();
			globalStaging.Reset();
		}

		ASSERT(globalStaging.CanFit(bytes) &&
			"GlobalStaging too small for the static transform range");

		plan.staticTransforms = globalStaging.Stage(
			staticTransforms.data() + range.offset,
			bytes,
			globalBDATable.GetGPUBuffer(RD::Renderer_Buffer::StaticTransforms).m_buffer,
			dstOffset);

		plan.hasStaticTransforms = true;
		scene.ClearStaticTransformsDirty();
	}

	// Lights
	if (uploadLights && !lights.empty())
	{
		const size_t lightBytes = lights.size() * sizeof(LocalLight);

		plan.lights = frameStaging.Stage(
			lights.data(),
			lightBytes,
			frameAddrTable.GetGPUBuffer(RD::Renderer_Buffer::Lights).m_buffer);

		plan.hasLights = true;
	}

	// Buffer address table
	if (frameAddrTable.IsTableDirty())
	{
		plan.frameAddrTable = frameStaging.Stage(
			frameAddrTable.GetAddrPtrTable().data(),
			frameAddrTable.GPU_ADDRESS_TABLE_SIZE_GPU_BYTES,
			frameAddrTable.GetTableBuffer().m_buffer);

		plan.hasFrameAddrTable = true;
		plan.frameAddrVersion = frameAddrTable.GetCpuVersion();
	}

	// Luminance header reset
	if (uploadLuminance)
	{
		plan.luminance = frameStaging.Stage(
			&defaultLuminance,
			sizeof(glm::vec4),
			globalBDATable.GetGPUBuffer(RD::Renderer_Buffer::Luminance).m_buffer,
			0);

		plan.hasLuminance = true;
	}

	frameStaging.Flush();
	if (plan.hasStaticTransforms) globalStaging.Flush();

	// Record transfer command
	device.RecordDeferredCommand([&](VkCommandBuffer cmd)
	{
		if (plan.instanceUploadNeeded)
		{
			frameStaging.CopyCommand(cmd, plan.instanceInputs);
			frameStaging.CopyCommand(cmd, plan.rtRows);
			frameStaging.CopyCommand(cmd, plan.drawBinKeys);
			frameStaging.CopyCommand(cmd, plan.drawBinKeysDense);
			frameStaging.CopyCommand(cmd, plan.globalAddrTable);

			BufferBarriers::TransferReleaseOnGraphics(
				cmd,
				globalBDATable.GetGPUBuffer(RD::Renderer_Buffer::InstanceInputs),
				device.GetContext());
			BufferBarriers::TransferReleaseOnGraphics(
				cmd,
				globalBDATable.GetGPUBuffer(RD::Renderer_Buffer::RTRows),
				device.GetContext());
			BufferBarriers::TransferReleaseOnGraphics(
				cmd,
				globalBDATable.GetGPUBuffer(RD::Renderer_Buffer::DrawBinKeys),
				device.GetContext());
			BufferBarriers::TransferReleaseOnGraphics(
				cmd,
				globalBDATable.GetTableBuffer(),
				device.GetContext());
		}

		if (plan.hasDynamicTransforms)
		{
			frameStaging.CopyCommand(cmd, plan.dynamicTransforms);
			BufferBarriers::TransferReleaseOnGraphics(
				cmd, frameAddrTable.GetGPUBuffer(RD::Renderer_Buffer::DynamicTransforms),
				device.GetContext());
		}

		if (plan.hasMotionMatrices)
		{
			frameStaging.CopyCommand(cmd, plan.motionMatrices);
			BufferBarriers::TransferReleaseOnGraphics(
				cmd, frameAddrTable.GetGPUBuffer(RD::Renderer_Buffer::MotionMatrices),
				device.GetContext());
		}

		if (plan.hasStaticTransforms)
		{
			globalStaging.CopyCommand(cmd, plan.staticTransforms);
			BufferBarriers::TransferReleaseOnGraphics(
				cmd, globalBDATable.GetGPUBuffer(RD::Renderer_Buffer::StaticTransforms),
				device.GetContext());
		}

		if (plan.hasLights)
		{
			frameStaging.CopyCommand(cmd, plan.lights);
			BufferBarriers::TransferReleaseOnGraphics(
				cmd,
				frameAddrTable.GetGPUBuffer(RD::Renderer_Buffer::Lights),
				device.GetContext());
		}

		if (plan.hasFrameAddrTable)
		{
			frameStaging.CopyCommand(cmd, plan.frameAddrTable);
			BufferBarriers::TransferReleaseOnGraphics(
				cmd,
				frameAddrTable.GetTableBuffer(),
				device.GetContext());

			frameCtx.SetPendingAddrTableVersion(plan.frameAddrVersion);
		}

		if (plan.hasLuminance)
		{
			frameStaging.CopyCommand(cmd, plan.luminance);
			BufferBarriers::TransferReleaseOnGraphics(
				cmd,
				globalBDATable.GetGPUBuffer(RD::Renderer_Buffer::Luminance),
				device.GetContext());
		}

	}, frameCtx.GetTransferPool(), QueueType::Transfer);

	frameCtx.CollectTransferCmds(std::move(device.DeferredCmds.CollectTransfer()), QueueType::Transfer);

	// Timeline semaphore sync — render waits on this value
	const uint64_t signalValue = device.GetTransferQueue().Submit(frameCtx.GetTransferCommands());

	frameCtx.StashTransferCmds();
	frameCtx.GetTransferWaitValue() = signalValue;

	frameAddrTable.SetGpuVersion(frameCtx.GetPendingAddrTableVersion());

	// Reset FrameStaging head — safe since transfer is submitted
	frameStaging.Reset();
}
