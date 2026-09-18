#include "VulkanExtensionFunctions.h"

#undef vkCmdDrawMeshTasksEXT
#undef vkCmdDrawMeshTasksIndirectEXT
#undef vkCmdDrawMeshTasksIndirectCountEXT
#undef vkGetDeviceFaultInfoEXT
#undef vkSetDebugUtilsObjectNameEXT
#undef vkCmdBeginDebugUtilsLabelEXT
#undef vkCmdEndDebugUtilsLabelEXT
#undef vkCmdInsertDebugUtilsLabelEXT
#undef vkQueueBeginDebugUtilsLabelEXT
#undef vkQueueEndDebugUtilsLabelEXT
#undef vkCreateAccelerationStructureKHR
#undef vkDestroyAccelerationStructureKHR
#undef vkGetAccelerationStructureBuildSizesKHR
#undef vkGetAccelerationStructureDeviceAddressKHR
#undef vkCmdBuildAccelerationStructuresKHR
#undef vkCmdCopyAccelerationStructureKHR
#undef vkCmdWriteAccelerationStructuresPropertiesKHR

#undef vkCmdSetCheckpointNV
#undef vkGetQueueCheckpointData2NV

#include <cstdio>

PFN_vkCmdDrawMeshTasksEXT              pfn_vkCmdDrawMeshTasksEXT = nullptr;
PFN_vkCmdDrawMeshTasksIndirectEXT      pfn_vkCmdDrawMeshTasksIndirectEXT = nullptr;
PFN_vkCmdDrawMeshTasksIndirectCountEXT pfn_vkCmdDrawMeshTasksIndirectCountEXT = nullptr;

PFN_vkGetDeviceFaultInfoEXT pfn_vkGetDeviceFaultInfoEXT = nullptr;

PFN_vkSetDebugUtilsObjectNameEXT  pfn_vkSetDebugUtilsObjectNameEXT = nullptr;
PFN_vkCmdBeginDebugUtilsLabelEXT  pfn_vkCmdBeginDebugUtilsLabelEXT = nullptr;
PFN_vkCmdEndDebugUtilsLabelEXT    pfn_vkCmdEndDebugUtilsLabelEXT = nullptr;
PFN_vkCmdInsertDebugUtilsLabelEXT pfn_vkCmdInsertDebugUtilsLabelEXT = nullptr;

PFN_vkQueueBeginDebugUtilsLabelEXT pfn_vkQueueBeginDebugUtilsLabelEXT = nullptr;
PFN_vkQueueEndDebugUtilsLabelEXT   pfn_vkQueueEndDebugUtilsLabelEXT = nullptr;

PFN_vkCmdSetCheckpointNV        pfn_vkCmdSetCheckpointNV = nullptr;
PFN_vkGetQueueCheckpointData2NV pfn_vkGetQueueCheckpointData2NV = nullptr;
PFN_vkCmdWriteBufferMarker2AMD  pfn_vkCmdWriteBufferMarker2AMD = nullptr;

PFN_vkCreateAccelerationStructureKHR              pfn_vkCreateAccelerationStructureKHR = nullptr;
PFN_vkDestroyAccelerationStructureKHR             pfn_vkDestroyAccelerationStructureKHR = nullptr;
PFN_vkGetAccelerationStructureBuildSizesKHR       pfn_vkGetAccelerationStructureBuildSizesKHR = nullptr;
PFN_vkGetAccelerationStructureDeviceAddressKHR    pfn_vkGetAccelerationStructureDeviceAddressKHR = nullptr;
PFN_vkCmdBuildAccelerationStructuresKHR           pfn_vkCmdBuildAccelerationStructuresKHR = nullptr;
PFN_vkCmdCopyAccelerationStructureKHR             pfn_vkCmdCopyAccelerationStructureKHR = nullptr;
PFN_vkCmdWriteAccelerationStructuresPropertiesKHR pfn_vkCmdWriteAccelerationStructuresPropertiesKHR = nullptr;

static bool s_extensionLoadFailed = false;

#define LOAD(fn)                                                                  \
	do {                                                                          \
		pfn_##fn = reinterpret_cast<PFN_##fn>(vkGetDeviceProcAddr(device, #fn));  \
		if (!pfn_##fn)                                                            \
		{                                                                         \
			s_extensionLoadFailed = true;                                         \
			std::fprintf(stderr, "[ExtLoader] device fn NULL: %s\n", #fn);        \
		}                                                                         \
	} while (0)

#define LOAD_INSTANCE(fn)                                                             \
	do {                                                                              \
		pfn_##fn = reinterpret_cast<PFN_##fn>(vkGetInstanceProcAddr(instance, #fn));  \
		if (!pfn_##fn)                                                                \
		{                                                                             \
			s_extensionLoadFailed = true;                                             \
			std::fprintf(stderr, "[ExtLoader] instance fn NULL: %s\n", #fn);          \
		}                                                                             \
	} while (0)

#define LOAD_DEVICE_OPT(fn) \
	pfn_##fn = reinterpret_cast<PFN_##fn>(vkGetDeviceProcAddr(device, #fn))

void LoadInstanceExtensionFunctions(VkInstance instance)
{
	LOAD_INSTANCE(vkSetDebugUtilsObjectNameEXT);
	LOAD_INSTANCE(vkCmdBeginDebugUtilsLabelEXT);
	LOAD_INSTANCE(vkCmdEndDebugUtilsLabelEXT);
	LOAD_INSTANCE(vkCmdInsertDebugUtilsLabelEXT);
	LOAD_INSTANCE(vkQueueBeginDebugUtilsLabelEXT);
	LOAD_INSTANCE(vkQueueEndDebugUtilsLabelEXT);
}

void LoadDeviceExtensionFunctions(VkDevice device)
{
	LOAD(vkCmdDrawMeshTasksEXT);
	LOAD(vkCmdDrawMeshTasksIndirectEXT);
	LOAD(vkCmdDrawMeshTasksIndirectCountEXT);

	LOAD(vkGetDeviceFaultInfoEXT);

	LOAD_DEVICE_OPT(vkCmdSetCheckpointNV);
	LOAD_DEVICE_OPT(vkGetQueueCheckpointData2NV);
	LOAD_DEVICE_OPT(vkCmdWriteBufferMarker2AMD);

	LOAD(vkCreateAccelerationStructureKHR);
	LOAD(vkDestroyAccelerationStructureKHR);
	LOAD(vkGetAccelerationStructureBuildSizesKHR);
	LOAD(vkGetAccelerationStructureDeviceAddressKHR);
	LOAD(vkCmdBuildAccelerationStructuresKHR);
	LOAD(vkCmdCopyAccelerationStructureKHR);
	LOAD(vkCmdWriteAccelerationStructuresPropertiesKHR);
}

bool DidExtensionLoadFail() { return s_extensionLoadFailed; }

#undef LOAD
#undef LOAD_INSTANCE
#undef LOAD_DEVICE_OPT