#pragma once

#include "renderer/RendererDefinitions.h"

namespace RD = RendererDefinitions;

inline constexpr size_t SIZEOF_INSTANCE_INPUT    = 52u;   // 13 x uint
inline constexpr size_t SIZEOF_VISIBLE_INSTANCE  = 16u;   // 4 x uint
inline constexpr size_t SIZEOF_STREAM_ENTRY      = 8u;    // 2 x uint (visibleID, binID)
inline constexpr size_t SIZEOF_DRAW_BIN          = 16u;   // 4 x uint
inline constexpr size_t SIZEOF_DISPATCH_ARG      = 16u;   // uvec4
inline constexpr size_t SIZEOF_LOCAL_LIGHT       = 72u;
inline constexpr size_t SIZEOF_MAT4              = 64u;
inline constexpr size_t SIZEOF_RT_INSTANCE       = 64u;
inline constexpr size_t SIZEOF_BIN_KEY           = 12u;   // 3 x uint (meshID, materialID, binID)

// GPU buffer byte sizes
inline constexpr size_t GPU_BYTES_INSTANCE_INPUT
	= RD::MAX_FRAME_INSTANCES_TOTAL * SIZEOF_INSTANCE_INPUT;

inline constexpr size_t GPU_BYTES_VISIBLE_INSTANCES
	= RD::MAX_FRAME_INSTANCES_TOTAL * SIZEOF_VISIBLE_INSTANCE;

inline constexpr size_t GPU_BYTES_INSTANCE_VISIBILITY
	= RD::VIS_SLOT_COUNT * sizeof(uint32_t);

inline constexpr size_t GPU_BYTES_INSTANCE_CURSORS
	= RD::VIS_SLOT_COUNT * sizeof(uint32_t);

// Per-stream routing records — scatter writes, place reads
inline constexpr size_t GPU_BYTES_INSTANCE_STREAMS
	= RD::VIS_SLOT_COUNT * RD::MAX_INSTANCES_PER_STREAM * SIZEOF_STREAM_ENTRY;

// Final placed instance IDs — VS reads via gl_InstanceIndex
inline constexpr size_t GPU_BYTES_DRAW_INSTANCE_IDS
	= RD::VIS_SLOT_COUNT * RD::MAX_INSTANCES_PER_STREAM * sizeof(uint32_t);

// One draw count per stream — feeds DrawIndexedIndirectCount
inline constexpr size_t GPU_BYTES_INDIRECT_DRAW_COUNTS
	= RD::VIS_SLOT_COUNT * sizeof(uint32_t);

// Draw bins: slots x bins x struct
inline constexpr size_t GPU_BYTES_DRAW_BINS
	= RD::VIS_SLOT_COUNT * RD::MAX_DRAW_BINS * SIZEOF_DRAW_BIN;

// Bin counters: reused as intra-bin cursors in place pass (fill 0 each frame)
inline constexpr size_t GPU_BYTES_DRAW_BIN_COUNTERS
	= RD::VIS_SLOT_COUNT * RD::MAX_DRAW_BINS * sizeof(uint32_t);

// Static bin key hash table + dense binID -> {mesh, material} side table
inline constexpr size_t GPU_BYTES_DRAW_BIN_KEYS
	= RD::BIN_TABLE_SIZE * SIZEOF_BIN_KEY
	+ RD::MAX_DRAW_BINS * sizeof(glm::uvec2);

// Shadow cull data — receiverLSMin/Max vec4 pairs + cascadeActive uvec4
inline constexpr size_t GPU_BYTES_SHADOW_CULL_DATA
	= (RD::MAX_SHADOW_CASCADES * 2 * sizeof(float) * 4)
	+ sizeof(uint32_t) * 4;

// Dispatch indirect args — one uvec4 per slot
inline constexpr size_t GPU_BYTES_DISPATCH_INDIRECT_ARGS
	= RD::INDIRECT_DISPATCH_SLOT_COUNT * SIZEOF_DISPATCH_ARG;

inline constexpr size_t GPU_BYTES_STATIC_TRANSFORMS
	= RD::MAX_STATIC_TRANSFORMS * SIZEOF_MAT4;

inline constexpr size_t GPU_BYTES_DYNAMIC_TRANSFORMS
	= RD::MAX_DYNAMIC_TRANSFORMS * SIZEOF_MAT4;

inline constexpr size_t GPU_BYTES_LIGHTS
	= RD::MAX_LIGHTS * SIZEOF_LOCAL_LIGHT;

inline constexpr size_t GPU_BYTES_VISIBLE_LIGHT_IDS
	= RD::MAX_LIGHTS * sizeof(uint32_t);

inline constexpr size_t GPU_BYTES_LUMINANCE
	= (RD::MAX_LUMINANCE_GROUPS + 1u) * sizeof(float) * 4;

inline constexpr size_t  GPU_BYTES_MESHLET_VISIBILITY = RD::MAX_MESHLET_VISIBILITY_BITS / 8u;

inline constexpr size_t GPU_BYTES_TASK_DISPATCH =
	RD::TASK_OFFSET_TOTAL * RD::TASK_GROUP_SIZE;

inline constexpr size_t GPU_BYTES_RT_INSTANCES =
	RD::MAX_RT_INSTANCES * SIZEOF_RT_INSTANCE;

inline constexpr size_t GPU_BYTES_RT_ROWS =
	RD::MAX_RT_INSTANCES * sizeof(uint32_t);

inline constexpr size_t GPU_BYTES_DEBUG_COUNTERS = 8u;

inline constexpr size_t GPU_BYTES_DEBUG_ITEMS = RD::DEBUG_MAX_ITEMS * 12u;

inline constexpr size_t GPU_BYTES_DEBUG_VERTEX = RD::DEBUG_MAX_VERTS * 16u;

inline constexpr size_t MIN_SSBO_ALIGNMENT_BYTES = 256u;
