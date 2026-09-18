#ifndef INSTANCES_GLSL
#define INSTANCES_GLSL

#extension GL_GOOGLE_include_directive : require
#include "bindings.glsl"

// Stored in InstanceInput
const uint PASS_OPAQUE            = 1u << 0;
const uint PASS_TRANSPARENT       = 1u << 1;
const uint STATIC_OBJECT          = 1u << 2;
const uint DYNAMIC_OBJECT         = 1u << 3;
const uint CAST_CSM               = 1u << 4;
const uint CAST_FLASHLIGHT        = 1u << 5;
const uint RECEIVE_SHADOW         = 1u << 6;
const uint OCCLUDABLE             = 1u << 7;
const uint LOD_ENABLED            = 1u << 8;
const uint ALPHA_TESTED           = 1u << 9;
const uint DOUBLE_SIDED           = 1u << 10;
const uint GPU_SKINNED            = 1u << 11;
const uint ALWAYS_VISIBLE         = 1u << 12;
const uint IS_TREE                = 1u << 13;
const uint HAS_NORMALS            = 1u << 14;
const uint INSTANCE_ACTIVE        = 1u << 15;
const uint RT_VISIBLE             = 1u << 16;
const uint TRANSMISSIVE           = 1u << 17;

// Stored in VisibleInstance
const uint VIS_PRIMARY_OPAQUE        = 1u << 0;
const uint VIS_PRIMARY_OPAQUE_MASKED = 1u << 1;
const uint VIS_PRIMARY_TRANSPARENT   = 1u << 2;
const uint VIS_FLASHLIGHT            = 1u << 3;
const uint VIS_CSM0                  = 1u << 4;
const uint VIS_CSM1                  = 1u << 5;
const uint VIS_CSM2                  = 1u << 6;
const uint VIS_VOLUMETRIC            = 1u << 7;

const uint VISIBILITY_TYPE_COUNT = 8u;

const uint LOD_IDX_LOD0    = 0u;
const uint LOD_IDX_LOD1    = 1u;
const uint LOD_IDX_LOD2    = 2u;
const uint LOD_IDX_LOD3    = 3u;
const uint LOD_IDX_BASE    = 4u;   // LOD_ENABLED off -> instance.meshID
const uint LOD_IDX_SHADOW0 = 5u;
const uint LOD_IDX_SHADOW1 = 6u;
const uint LOD_IDX_SHADOW2 = 7u;

const uint TRANSFORM_DYNAMIC_BIT = 1u << 31;
const uint TRANSFORM_INDEX_MASK  = ~TRANSFORM_DYNAMIC_BIT;

bool isDynamicTransform(uint transformID) { return (transformID & TRANSFORM_DYNAMIC_BIT) != 0u; }
uint transformIndex(uint transformID)     { return transformID & TRANSFORM_INDEX_MASK; }

struct InstanceInput
{
	uint meshID;
	uint materialID;
	uint transformID;
	uint meshletVisibilityOffset;

	uint lod0;
	uint lod1;
	uint lod2;
	uint lod3;

	uint shadowLod0;
	uint shadowLod1;
	uint shadowLod2;

	uint rtMeshID;

	uint flags;
};

struct VisibleInstance
{
	uint instanceID;
	uint primaryLodIndex;
	uint primaryMesh;
	uint visMask;
};

struct InstanceCursors
{
	uint cursors[VIS_SLOT_COUNT];
};

uint meshFromLODIndex(InstanceInput instance, uint lodIdx)
{
	switch (lodIdx)
	{
		case LOD_IDX_LOD0:    return instance.lod0;
		case LOD_IDX_LOD1:    return instance.lod1;
		case LOD_IDX_LOD2:    return instance.lod2;
		case LOD_IDX_LOD3:    return instance.lod3;

		case LOD_IDX_BASE:    return instance.meshID;

		case LOD_IDX_SHADOW0: return instance.shadowLod0;
		case LOD_IDX_SHADOW1: return instance.shadowLod1;
		case LOD_IDX_SHADOW2: return instance.shadowLod2;

		default:              return instance.meshID;
	}
}

uint selectLODIndex(
	InstanceInput instance,
	vec3 center,
	float radius,
	vec3 camPos,
	float projScaleY)
{
	if ((instance.flags & LOD_ENABLED) == 0u)
	{
		return LOD_IDX_BASE;
	}

	float dist = max(length(center - camPos) - radius, 0.0);

	float screenRadius = (dist > 0.0)
		? (radius * projScaleY) / dist
		: 1.0;

	if      (screenRadius < 0.02) return LOD_IDX_LOD3;
	else if (screenRadius < 0.05) return LOD_IDX_LOD2;
	else if (screenRadius < 0.10) return LOD_IDX_LOD1;
	else                          return LOD_IDX_LOD0;
}

uint selectShadowLODIndex(InstanceInput instance, uint cascadeIndex)
{
	uint lodIdx;

	// Surface CSM:
	// C0 -> shadow0
	// C1 -> shadow0
	// C2 -> shadow1
	switch (cascadeIndex)
	{
		case 0u:
		case 1u:
			lodIdx = LOD_IDX_SHADOW0;
			break;

		case 2u:
			lodIdx = LOD_IDX_SHADOW1;
			break;

		default:
			lodIdx = LOD_IDX_SHADOW2;
			break;
	}

	// Preserve more silhouette detail for alpha-tested trees.
	bool isAlphaTested = (instance.flags & ALPHA_TESTED) != 0u;
	bool isTree        = (instance.flags & IS_TREE) != 0u;

	if (isAlphaTested && isTree && lodIdx > LOD_IDX_SHADOW0)
	{
		lodIdx -= 1u;
	}

	return lodIdx;
}

uint selectVolumetricShadowLODIndex(InstanceInput instance)
{
	// Foliage so god rays can hit through leaves
	if ((instance.flags & IS_TREE) != 0u)
	{
		return LOD_IDX_SHADOW1;
	}

	return LOD_IDX_SHADOW2;
}

uint selectFlashlightShadowLODIndex(InstanceInput instance)
{
	return LOD_IDX_SHADOW0;
}

uint selectLOD(
	InstanceInput instance,
	vec3 center,
	float radius,
	vec3 camPos,
	float projScaleY)
{
	uint lodIdx = selectLODIndex(
		instance,
		center,
		radius,
		camPos,
		projScaleY);

	return meshFromLODIndex(instance, lodIdx);
}

uint selectShadowLOD(
	InstanceInput instance,
	uint cascadeIndex)
{
	uint lodIdx = selectShadowLODIndex(
		instance,
		cascadeIndex);

	return meshFromLODIndex(instance, lodIdx);
}

uint selectVolumetricShadowLOD(InstanceInput instance)
{
	uint lodIdx = selectVolumetricShadowLODIndex(instance);

	return meshFromLODIndex(instance, lodIdx);
}

uint selectFlashlightShadowLOD(
	InstanceInput instance)
{
	uint lodIdx = selectFlashlightShadowLODIndex(instance);

	return meshFromLODIndex(instance, lodIdx);
}

uint resolveLODIndexForStream(
	VisibleInstance vi,
	InstanceInput instance,
	uint slot)
{
	switch (slot)
	{
		case VIS_SLOT_OPAQUE:
		case VIS_SLOT_OPAQUE_MASKED:
		case VIS_SLOT_TRANSPARENT:
			return vi.primaryLodIndex;

		case VIS_SLOT_FLASHLIGHT:
			return selectFlashlightShadowLODIndex(instance);

		case VIS_SLOT_CSM0:
			return selectShadowLODIndex(instance, 0u);

		case VIS_SLOT_CSM1:
			return selectShadowLODIndex(instance, 1u);

		case VIS_SLOT_CSM2:
			return selectShadowLODIndex(instance, 2u);

		case VIS_SLOT_VOLUMETRIC:
			return selectVolumetricShadowLODIndex(instance);

		default:
			return LOD_IDX_BASE;
	}
}

uint resolveMeshForStream(
	VisibleInstance vi,
	InstanceInput instance,
	uint slot)
{
	switch (slot)
	{
		case VIS_SLOT_OPAQUE:
		case VIS_SLOT_OPAQUE_MASKED:
		case VIS_SLOT_TRANSPARENT:
			return vi.primaryMesh;
	}

	uint lodIdx = resolveLODIndexForStream(
		vi,
		instance,
		slot);

	return meshFromLODIndex(instance, lodIdx);
}

#endif
