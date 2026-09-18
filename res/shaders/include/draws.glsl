#ifndef DRAWS_GLSL
#define DRAWS_GLSL

#extension GL_GOOGLE_include_directive  : require
#include "bindings.glsl"

const uint MAX_INSTANCES_PER_STREAM = 262144u; // size streams + final instance regions
const uint MAX_DRAW_BINS            = 65536u;  // unique {mesh,material} pairs, all LOD variant
const uint BIN_TABLE_SIZE           = MAX_DRAW_BINS * 2; // hash table, power of 2, ~2x MAX_DRAW_BINS
const uint INVALID_U32              = 0xFFFFFFFFu;

const uint TASK_GROUP_SIZE = 32u;

const uint DRAW_MAX_OPAQUE        = 32768u;
const uint DRAW_MAX_OPAQUE_MASKED = 12288u;
const uint DRAW_MAX_TRANSPARENT   = 3072u;

// Shadow draws
const uint DRAW_MAX_FLASHLIGHT    = 1024u;
const uint DRAW_MAX_CSM0          = 8192u;
const uint DRAW_MAX_CSM1          = 2048u;
const uint DRAW_MAX_CSM2          = 2048u;
const uint DRAW_MAX_VOLUMETRIC    = 8192u;

const uint DRAW_OFFSET_OPAQUE        = 0u;
const uint DRAW_OFFSET_OPAQUE_MASKED = DRAW_OFFSET_OPAQUE         + DRAW_MAX_OPAQUE;
const uint DRAW_OFFSET_TRANSPARENT   = DRAW_OFFSET_OPAQUE_MASKED  + DRAW_MAX_OPAQUE_MASKED;
const uint DRAW_OFFSET_FLASHLIGHT    = DRAW_OFFSET_TRANSPARENT    + DRAW_MAX_TRANSPARENT;
const uint DRAW_OFFSET_CSM0          = DRAW_OFFSET_FLASHLIGHT     + DRAW_MAX_FLASHLIGHT;
const uint DRAW_OFFSET_CSM1          = DRAW_OFFSET_CSM0           + DRAW_MAX_CSM0;
const uint DRAW_OFFSET_CSM2          = DRAW_OFFSET_CSM1           + DRAW_MAX_CSM1;
const uint DRAW_OFFSET_VOLUMETRIC    = DRAW_OFFSET_CSM2           + DRAW_MAX_CSM2;

const uint DEBUG_MASK_OBB          = 1u << 0;
const uint DEBUG_MASK_SPHERE       = 1u << 1;
const uint DEBUG_MASK_AABB         = 1u << 2;

const uint DEBUG_VERTS_PER_OBB     = 24u;      // 12 edges x 2 verts, LINE_LIST
const uint DEBUG_VERTS_PER_SPHERE  = 72u;      // 3 rings x 12 segments x 2
const uint DEBUG_MAX_ITEMS         = 65536u;
const uint DEBUG_MAX_VERTS         = DEBUG_MAX_ITEMS * DEBUG_VERTS_PER_OBB;

const uint TASK_MAX_OPAQUE        = DRAW_MAX_OPAQUE;
const uint TASK_MAX_OPAQUE_MASKED = DRAW_MAX_OPAQUE_MASKED;
const uint TASK_MAX_TRANSPARENT   = DRAW_MAX_TRANSPARENT;
const uint TASK_MAX_FLASHLIGHT    = DRAW_MAX_FLASHLIGHT;
const uint TASK_MAX_CSM0          = DRAW_MAX_CSM0;
const uint TASK_MAX_CSM1          = DRAW_MAX_CSM1;
const uint TASK_MAX_CSM2          = DRAW_MAX_CSM2;
const uint TASK_MAX_VOLUMETRIC    = DRAW_MAX_VOLUMETRIC;

const uint TASK_OFFSET_OPAQUE        = 0u;
const uint TASK_OFFSET_OPAQUE_MASKED = TASK_OFFSET_OPAQUE        + TASK_MAX_OPAQUE;
const uint TASK_OFFSET_TRANSPARENT   = TASK_OFFSET_OPAQUE_MASKED + TASK_MAX_OPAQUE_MASKED;
const uint TASK_OFFSET_FLASHLIGHT    = TASK_OFFSET_TRANSPARENT   + TASK_MAX_TRANSPARENT;
const uint TASK_OFFSET_CSM0          = TASK_OFFSET_FLASHLIGHT    + TASK_MAX_FLASHLIGHT;
const uint TASK_OFFSET_CSM1          = TASK_OFFSET_CSM0          + TASK_MAX_CSM0;
const uint TASK_OFFSET_CSM2          = TASK_OFFSET_CSM1          + TASK_MAX_CSM1;
const uint TASK_OFFSET_VOLUMETRIC    = TASK_OFFSET_CSM2          + TASK_MAX_CSM2;

// task dispatch region base per stream — same shape as streamDrawBase
uint streamTaskBase(uint slot)
{
	switch (slot)
	{
		case VIS_SLOT_OPAQUE:        return TASK_OFFSET_OPAQUE;
		case VIS_SLOT_OPAQUE_MASKED: return TASK_OFFSET_OPAQUE_MASKED;
		case VIS_SLOT_TRANSPARENT:   return TASK_OFFSET_TRANSPARENT;
		case VIS_SLOT_FLASHLIGHT:    return TASK_OFFSET_FLASHLIGHT;
		case VIS_SLOT_CSM0:          return TASK_OFFSET_CSM0;
		case VIS_SLOT_CSM1:          return TASK_OFFSET_CSM1;
		case VIS_SLOT_CSM2:          return TASK_OFFSET_CSM2;
		case VIS_SLOT_VOLUMETRIC:    return TASK_OFFSET_VOLUMETRIC;
		default:                     return TASK_OFFSET_OPAQUE;
	}
}

uint maxTaskDispatches(uint slot)
{
	switch (slot)
	{
		case VIS_SLOT_OPAQUE:        return TASK_MAX_OPAQUE;
		case VIS_SLOT_OPAQUE_MASKED: return TASK_MAX_OPAQUE_MASKED;
		case VIS_SLOT_TRANSPARENT:   return TASK_MAX_TRANSPARENT;
		case VIS_SLOT_FLASHLIGHT:    return TASK_MAX_FLASHLIGHT;
		case VIS_SLOT_CSM0:          return TASK_MAX_CSM0;
		case VIS_SLOT_CSM1:          return TASK_MAX_CSM1;
		case VIS_SLOT_CSM2:          return TASK_MAX_CSM2;
		case VIS_SLOT_VOLUMETRIC:    return TASK_MAX_VOLUMETRIC;
		default:                     return 0u;
	}
}

struct TaskDispatch
{
	uint groupCountX;    // ceil(meshletCount / TASK_GROUP_SIZE)  — must be first 12 bytes
	uint groupCountY;    // instanceCount
	uint groupCountZ;    // 1

	uint instanceBase;
	uint meshletOffset;
	uint meshletCount;
	uint vertexOffset;
	uint visibilityBase;
};

// final visible-instance buffer regions (what firstInstance indexes into)
// stream slot s owns [s * MAX_INSTANCES_PER_STREAM, ...)
uint streamInstanceBase(uint slot) { return slot * MAX_INSTANCES_PER_STREAM; }

// draw region base per stream
uint streamDrawBase(uint slot)
{
	switch (slot)
	{
		case VIS_SLOT_OPAQUE:        return DRAW_OFFSET_OPAQUE;
		case VIS_SLOT_OPAQUE_MASKED: return DRAW_OFFSET_OPAQUE_MASKED;
		case VIS_SLOT_TRANSPARENT:   return DRAW_OFFSET_TRANSPARENT;
		case VIS_SLOT_FLASHLIGHT:    return DRAW_OFFSET_FLASHLIGHT;
		case VIS_SLOT_CSM0:          return DRAW_OFFSET_CSM0;
		case VIS_SLOT_CSM1:          return DRAW_OFFSET_CSM1;
		case VIS_SLOT_CSM2:          return DRAW_OFFSET_CSM2;
		case VIS_SLOT_VOLUMETRIC:    return DRAW_OFFSET_VOLUMETRIC;
		default:                     return DRAW_OFFSET_OPAQUE;
	}
}

uint maxDrawsForSlot(uint slot)
{
	switch (slot)
	{
		case VIS_SLOT_OPAQUE:        return DRAW_MAX_OPAQUE;
		case VIS_SLOT_OPAQUE_MASKED: return DRAW_MAX_OPAQUE_MASKED;
		case VIS_SLOT_TRANSPARENT:   return DRAW_MAX_TRANSPARENT;
		case VIS_SLOT_FLASHLIGHT:    return DRAW_MAX_FLASHLIGHT;
		case VIS_SLOT_CSM0:          return DRAW_MAX_CSM0;
		case VIS_SLOT_CSM1:          return DRAW_MAX_CSM1;
		case VIS_SLOT_CSM2:          return DRAW_MAX_CSM2;
		case VIS_SLOT_VOLUMETRIC:    return DRAW_MAX_VOLUMETRIC;
		default:                     return 0u;
	}
}

struct BinKey        // static, uploaded at load — ABT_DrawBinKeys (global)
{
	uint meshID;     // BIN_INVALID = empty hash slot
	uint materialID;
	uint binID;
};

struct StreamEntry   // transient routing record — ABT_InstanceStreams
{
	uint visibleID;  // index into VisibleInstances
	uint binID;
};

struct IndirectDrawCmd
{
	uint vertexCount;
	uint instanceCount;
	uint firstVertex;
	uint firstInstance;
};

struct DrawBin
{
	uint meshID;
	uint materialID;

	uint instanceOffset;
	uint instanceCount;
};

struct DebugItem
{
	uint instanceID;
	uint drawType;
	uint colorPacked;
};

struct DebugVertex
{
	vec3 position;
	uint colorPacked;
};

struct DebugCounters
{
	uint itemCount;
	uint vertexCount;
};

uint packDebugColor(vec4 c)
{
	return (uint(c.a * 255.0) << 24) | (uint(c.b * 255.0) << 16)
		 | (uint(c.g * 255.0) << 8)  |  uint(c.r * 255.0);
}
vec4 unpackDebugColor(uint p)
{
	return vec4(float(p & 0xFFu), float((p >> 8) & 0xFFu),
				float((p >> 16) & 0xFFu), float((p >> 24) & 0xFFu)) / 255.0;
}

#endif
