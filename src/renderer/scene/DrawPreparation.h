#pragma once

#include <vector>
#include <unordered_map>
#include <memory>

#include "renderer/RendererDefinitions.h"
namespace RD = RendererDefinitions;

class FrameContext;
struct LocalLight;
class Device;
class Allocator;
struct Mesh;
struct MeshLODs;
struct InstanceState;
struct InstanceInput;
struct BinTableBuild;
struct DrawBinKeys;
enum class ModelID;
struct ModelAsset;
class BindlessBDATable;
class Scene;

namespace DrawPreparation
{
	bool SyncInstanceInputs(
		InstanceState& vs,
		const Scene& scene,
		const std::unordered_map<ModelID, std::shared_ptr<ModelAsset>>& loaded,
		const std::vector<Mesh>& meshData,
		const std::vector<MeshLODs>& meshLods,
		const std::vector<uint32_t>& materialFlags,
		const std::vector<uint64_t>& blasAddresses);

	BinTableBuild BuildDrawBinTable(const std::vector<InstanceInput>& instances);

	void UploadGPUBuffersForFrame(
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
		bool                              bMotionNeeded);
}
