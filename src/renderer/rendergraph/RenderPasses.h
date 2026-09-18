#pragma once

#include <span>
#include <vector>
#include "../backend/VulkanForward.h"

// Pipelines are placed in order of execution
// Draw extent is equal to window extent

class RenderGraph;
struct PipelineHandle;
class BindlessImageTable;
class PushDescriptorWriter;

void RegisterDirectionalCSMPass(RenderGraph& graph);

void RegisterShadowBoundsPass(RenderGraph& graph);

void RegisterMaterialResolvePass(RenderGraph& graph);

void RegisterOpaqueLightingPass(RenderGraph& graph);

void RegisterRTShadowsPass(RenderGraph& graph);

void RegisterNRDDenoisePass(RenderGraph& graph);

void RegisterWorldProbesUpdatePass(RenderGraph& graph);
void RegisterWorldProbesResolvePass(RenderGraph& graph);
void RegisterWorldProbesReconstructPass(RenderGraph& graph);
void RegisterWorldProbesCachePass(RenderGraph& graph);
void RegisterWorldProbesDebugPass(RenderGraph& graph);

void RegisterInstanceCullPass(RenderGraph& graph);

void RegisterDrawBuildPass(RenderGraph& graph);

void RegisterTLASBuildPass(RenderGraph& graph);

void RegisterRTReflectionsPass(RenderGraph& graph);

void RegisterWireframePass(RenderGraph& graph);

void RegisterChromaticAberrationPass(RenderGraph& graph);

void RegisterFlashlightShadowMapPass(RenderGraph& graph);

void RegisterVelocityResolvePass(RenderGraph& graph);

void RegisterTemporalCopyPass(RenderGraph& graph);

void RegisterThePrepass(RenderGraph& graph);

void RegisterHiZGenerationPass(RenderGraph& graph);

void RegisterThePrepassLate(RenderGraph& graph);

void RegisterHiZGenerationLatePass(RenderGraph& graph);

void RegisterClusteredLightsPass(RenderGraph& graph);

void RegisterVolumetricFogPass(RenderGraph& graph);

void RegisterVolumetricShadowMapPass(RenderGraph& graph);

void RegisterTAAPass(RenderGraph& graph);

void RegisterCASPass(RenderGraph& graph);

void RegisterDebugDrawBuildPass(RenderGraph& graph);

void RegisterLineDebugPass(RenderGraph& graph);

void RegisterLensFlarePass(RenderGraph& graph);

void RegisterBloomPass(RenderGraph& graph);

void RegisterFinalCompositePass(RenderGraph& graph);

void RegisterGBufferDebugPass(RenderGraph& graph);

void RegisterLuminanceExposurePass(RenderGraph& graph);

void RegisterSSGIPass(RenderGraph& graph);

void RegisterContactShadowsPass(RenderGraph& graph);

void RegisterAtmosphereSkyRenderPass(RenderGraph& graph);

void RegisterAtmosphereSkyViewPass(RenderGraph& graph);

void RegisterAtmosphereLUTUpdatePass(RenderGraph& graph);

void RegisterAtmosphereLightingPass(RenderGraph& graph);

void RegisterHDRSceneCompositePass(RenderGraph& graph);

void RegisterImguiDrawPass(RenderGraph& graph);

void RegisterTransparentForwardPass(RenderGraph& graph);

void RegisterSkyboxPass(RenderGraph& graph);

void RegisterSwapchainPresentPass(RenderGraph& graph);

void BakeEnvironmentMaps(
	VkCommandBuffer cmd,
	BindlessImageTable& imageTable,
	std::span<const PipelineHandle> pipelines);
