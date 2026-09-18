#pragma once
#include <vulkan/vulkan.h>

extern PFN_vkCmdDrawMeshTasksEXT              pfn_vkCmdDrawMeshTasksEXT;
extern PFN_vkCmdDrawMeshTasksIndirectEXT      pfn_vkCmdDrawMeshTasksIndirectEXT;
extern PFN_vkCmdDrawMeshTasksIndirectCountEXT pfn_vkCmdDrawMeshTasksIndirectCountEXT;

extern PFN_vkGetDeviceFaultInfoEXT pfn_vkGetDeviceFaultInfoEXT;

extern PFN_vkSetDebugUtilsObjectNameEXT pfn_vkSetDebugUtilsObjectNameEXT;
extern PFN_vkCmdBeginDebugUtilsLabelEXT pfn_vkCmdBeginDebugUtilsLabelEXT;
extern PFN_vkCmdEndDebugUtilsLabelEXT   pfn_vkCmdEndDebugUtilsLabelEXT;
extern PFN_vkCmdInsertDebugUtilsLabelEXT pfn_vkCmdInsertDebugUtilsLabelEXT;

extern PFN_vkCmdSetCheckpointNV        pfn_vkCmdSetCheckpointNV;
extern PFN_vkGetQueueCheckpointData2NV pfn_vkGetQueueCheckpointData2NV;
extern PFN_vkCmdWriteBufferMarker2AMD  pfn_vkCmdWriteBufferMarker2AMD;

extern PFN_vkQueueBeginDebugUtilsLabelEXT pfn_vkQueueBeginDebugUtilsLabelEXT;
extern PFN_vkQueueEndDebugUtilsLabelEXT   pfn_vkQueueEndDebugUtilsLabelEXT;

extern PFN_vkCreateAccelerationStructureKHR              pfn_vkCreateAccelerationStructureKHR;
extern PFN_vkDestroyAccelerationStructureKHR             pfn_vkDestroyAccelerationStructureKHR;
extern PFN_vkGetAccelerationStructureBuildSizesKHR       pfn_vkGetAccelerationStructureBuildSizesKHR;
extern PFN_vkGetAccelerationStructureDeviceAddressKHR    pfn_vkGetAccelerationStructureDeviceAddressKHR;
extern PFN_vkCmdBuildAccelerationStructuresKHR           pfn_vkCmdBuildAccelerationStructuresKHR;
extern PFN_vkCmdCopyAccelerationStructureKHR             pfn_vkCmdCopyAccelerationStructureKHR;
extern PFN_vkCmdWriteAccelerationStructuresPropertiesKHR pfn_vkCmdWriteAccelerationStructuresPropertiesKHR;

void LoadInstanceExtensionFunctions(VkInstance instance);
void LoadDeviceExtensionFunctions(VkDevice device);
bool DidExtensionLoadFail();

#define vkCmdDrawMeshTasksEXT              pfn_vkCmdDrawMeshTasksEXT
#define vkCmdDrawMeshTasksIndirectEXT      pfn_vkCmdDrawMeshTasksIndirectEXT
#define vkCmdDrawMeshTasksIndirectCountEXT pfn_vkCmdDrawMeshTasksIndirectCountEXT

#define vkGetDeviceFaultInfoEXT pfn_vkGetDeviceFaultInfoEXT

#define vkQueueBeginDebugUtilsLabelEXT pfn_vkQueueBeginDebugUtilsLabelEXT
#define vkQueueEndDebugUtilsLabelEXT   pfn_vkQueueEndDebugUtilsLabelEXT

#define vkSetDebugUtilsObjectNameEXT  pfn_vkSetDebugUtilsObjectNameEXT
#define vkCmdBeginDebugUtilsLabelEXT  pfn_vkCmdBeginDebugUtilsLabelEXT
#define vkCmdEndDebugUtilsLabelEXT    pfn_vkCmdEndDebugUtilsLabelEXT
#define vkCmdInsertDebugUtilsLabelEXT pfn_vkCmdInsertDebugUtilsLabelEXT

#define vkCmdSetCheckpointNV        pfn_vkCmdSetCheckpointNV
#define vkGetQueueCheckpointData2NV pfn_vkGetQueueCheckpointData2NV
#define vkCmdWriteBufferMarker2AMD  pfn_vkCmdWriteBufferMarker2AMD

#define vkCreateAccelerationStructureKHR              pfn_vkCreateAccelerationStructureKHR
#define vkDestroyAccelerationStructureKHR             pfn_vkDestroyAccelerationStructureKHR
#define vkGetAccelerationStructureBuildSizesKHR       pfn_vkGetAccelerationStructureBuildSizesKHR
#define vkGetAccelerationStructureDeviceAddressKHR    pfn_vkGetAccelerationStructureDeviceAddressKHR
#define vkCmdBuildAccelerationStructuresKHR           pfn_vkCmdBuildAccelerationStructuresKHR
#define vkCmdCopyAccelerationStructureKHR             pfn_vkCmdCopyAccelerationStructureKHR
#define vkCmdWriteAccelerationStructuresPropertiesKHR pfn_vkCmdWriteAccelerationStructuresPropertiesKHR
