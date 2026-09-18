#ifndef COMMON_GLSL
#define COMMON_GLSL

#extension GL_GOOGLE_include_directive : require

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_ARB_gpu_shader_int64 : require

#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require

#extension GL_EXT_shader_8bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require

#extension GL_EXT_nonuniform_qualifier : require

#include "bindings.glsl"
#include "instances.glsl"
#include "draws.glsl"

const float PI      = 3.1415926535897932384626433832795;
const float HALF_PI = 1.5707963267948966192313216916398;
const float TWO_PI  = 6.2831853;

const uint MAX_CASCADES = 3u;

const uint MAX_ENV_SETS = 8u;

const uint AA_OFF     = 0u;
const uint AA_TAA     = 1u;
const uint AA_TAA_CAS = 2u;

const uint SUN_SHADOW_FILTER_PCF              = 0u;
const uint SUN_SHADOW_FILTER_PCSS             = 1u;
const uint SUN_SHADOW_FILTER_RT_SOFT          = 2u;

const uint OFF   = 0u;
const uint VBAO  = 1u;
const uint VBGI  = 2u;

const uint MAX_FLT_UINT = 0x7F7FFFFFu;

const uint MAX_LUMINANCE_GROUPS = 65536u;

const float LIGHT_CULL_NITS = 0.05;

const float COLOR_HISTORY_MAX  = 65504.0;  // RGBA16F
const float RESOLVE_TARGET_MAX = 64512.0;  // r11f_g11f_b10f

const uint RT_MASK_OPAQUE       = 0x01u;
const uint RT_MASK_ALPHA_TESTED = 0x02u;
const uint RT_MASK_TRANSMISSIVE = 0x04u;

const uint RT_INSTANCE_FLAG_TRIANGLE_FACING_CULL_DISABLE = 0x01u;
const uint RT_INSTANCE_FLAG_FORCE_OPAQUE                 = 0x04u;

const uint LIGHT_FLAG_DIRECTIONAL       = 1u << 0;
const uint LIGHT_FLAG_POINT             = 1u << 1;
const uint LIGHT_FLAG_SPOT              = 1u << 2;
const uint LIGHT_FLAG_CASTS_SPOT_SHADOW = 1u << 3;
const uint LIGHT_FLAG_FLASHLIGHT        = 1u << 4;
const uint LIGHT_FLAG_FLASHLIGHT_OFF    = 1u << 5;
const uint LIGHT_FLAG_MASK_ONLY         = 1u << 6;

// Determines switch between dispatching over clusters or lights
const uint LIGHT_THRESHOLD = 500u;

//const uint LIGHT_LIST_STATIC_COUNT     = 2u;
//const uint LIGHT_LIST_SLOT_DIRECTIONAL = 0u;
//const uint LIGHT_LIST_SLOT_FLASHLIGHT  = 1u;

const uint LIGHT_LIST_STATIC_COUNT     = 1u;
const uint LIGHT_LIST_SLOT_FLASHLIGHT  = 0u;

//const uint MAX_LIGHTS = 16384u;
const uint MAX_LIGHTS = 2048u;

const uint MAX_VISIBLE_LIGHTS = MAX_LIGHTS - LIGHT_LIST_STATIC_COUNT;

const float SHADOW_FACE_SIGN = 1.0;

const float MAX_REFRACT_OFFSET = 0.15;

const float DOUBLE_SIDED_TRANS_FLOOR = 0.25;

const uint SHADING_MODEL_STANDARD      = 0u;
const uint SHADING_MODEL_CLEARCOAT     = 1u;
const uint SHADING_MODEL_SHEEN         = 2u;
const uint SHADING_MODEL_TRANSMISSION  = 3u;
const uint SHADING_MODEL_DIFFUSE_TRANS = 4u;

#define DBG(x) (debug.x != 0u)
#define RET(rgb, a) { outFragColor = vec4((rgb), (a)); return; }

const ivec2 AO_QUAD_PHASE[4] = ivec2[4](ivec2(0, 0), ivec2(1, 1), ivec2(1, 0), ivec2(0, 1));

ivec2 aoQuadPhase(uint frameIndex, bool taaOn)
{
	return taaOn ? AO_QUAD_PHASE[frameIndex & 3u] : ivec2(0);
}

float LinearRough(float r) { return max(r * r, 0.001); }

struct GPUStats
{
	uint visibleOpaque;
	uint visibleTransparent;
	uint visibleShadowCasters;

//	uint opaqueDrawCount;
//	uint transparentDrawCount;
//	uint shadowDrawCount;

	uint triangleCount; // Doesnt count shadows

	uint meshletsSubmitted;
	uint meshletsDrawnEarly;
	uint meshletsDrawnLate;
	uint meshletsCulledFrustum;
	uint meshletsCulledCone;
	uint meshletsCulledHiZ;
	uint meshletTriangles;
};

struct SceneData
{
	mat4 view;
	mat4 proj;
	mat4 projUnjittered;
	mat4 invView;
	mat4 prevInvView;
	mat4 invProj;
	mat4 viewProj;
	mat4 prevViewProjUnjittered;
	mat4 prevView;
	mat4 viewProjUnjittered;
	uvec4 temporal;           // .x = frameNumber, .y = historyValid (0/1), z = Hi-Z valid(0/1)
	vec4 temporalJitter;
	vec4 taaMipParams;        // .x = bias (negative, 0 = off), .y = fade start lod, .z = 1 / fade span
	// x = current jitter x ndc
	// y = current jitter y
	// z = previous jitter x
	// w = previous jitter y
	vec4 sunlightDirection;
	vec4 sunlightColor; // .w = power
	vec4 cameraPos;           // xyz pos
	vec4 cameraClips;         // .x near and .y far, .z invScreenWidth, .w invScreenHeight

	vec4 renderExtentSize; // .x and .y for width and height, .z for pixel count
	vec4 displayExtentSize;
	vec4 renderPixelSizes; // .x/.y = (1 / full extent) .z/.w = (1 / half extent)
	vec4 displayPixelSizes;

	vec2 tanHalfFov;           // 1 / proj[0][0], 1 / proj[1][1]
	float depthLinearizeMult;  // -proj[3][2]
	float depthLinearizeAdd;   //  proj[2][2]
	vec2 ndcToViewMult;        // tanHalfFov.x *  2, tanHalfFov.y * -2
	vec2 ndcToViewAdd;         // tanHalfFov.x * -1, tanHalfFov.y *  1
	mat4 flashlightVP;

	vec4 atmosphereRayleigh;
	vec4 atmosphereMie;
	vec4 atmosphereAbsorption;
	vec4 atmosphereGeometry;
	uvec4 atmosphereIntegration;

	vec4 atmospherePlacement;
	vec4 atmosphereScattering;
	vec4 atmosphereSun;
	vec4 atmosphereGround;

	RenderTargetIDs renderTargetIDs;
};


struct GPUAddressTable
{
	uint64_t addrs[ABT_Count];
};

struct AABB {
	vec3 vmin; // origin: 0.5f * (vmin + vmax)
	vec3 vmax; // extent: 0.5f * (vmax - vmin)
};

struct Material
{
	vec4 colorFactor;

	vec2 metalRoughFactors;
	float normalScale;
	float alphaCutoff;

	vec3 emissiveColor;
	float emissiveStrength;

	float ior;
	float specularFactor;

	float clearcoatFactor;
	float clearcoatRough;

	vec3 sheenColor;
	float sheenRough;

	float transmissionFactor;
	float diffuseTransFactor;

	float thicknessFactor;
	vec3 attenuationColor;
	float attenuationDistance;

	uint shadingModel;

	uint albedoID;
	uint metalRoughnessID;
	uint normalID;
	uint emissiveID;
};

struct Mesh
{
	AABB localAABB;
	float localBoundingRadius;
	uint firstIndex;
	uint indexCount;
	uint vertexOffset;
	uint vertexCount;
	uint meshletOffset;
	uint meshletCount;
	uint meshletVisibilityBase;
	uint shadowFirstIndex;
	uint shadowIndexCount;
	uint shadowMeshletOffset;
	uint shadowMeshletCount;
};

const uint MESHLET_MAX_VERTS = 64u;
const uint MESHLET_MAX_TRIS  = 124u;

struct Meshlet
{
	vec3     center;
	float    radius;

	i8vec3   coneAxis;
	int8_t   coneCutoff;
	uint     vertexOffset;
	uint     triangleOffset;
	uint8_t  vertexCount;   // <= 64
	uint8_t  triangleCount; // <= 124
};

struct Vertex
{
	vec3 position;

	// Normal: octahedral, snorm16
	int16_t   normalX;
	int16_t   normalY;

	// Tangent: octahedral, snorm16. Handedness (W) = sign of tangentY.
	int16_t   tangentX;
	int16_t   tangentY;

	// UV: FP16 bits (half-float)
	uint16_t  uvX;
	uint16_t  uvY;

	// Vertex color, RGBA8 packed
	uint      colorRGBA8;
};

vec2 octSignNotZero(vec2 v)
{
	return vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
}

vec2 octEncode(vec3 n)
{
	float l1 = abs(n.x) + abs(n.y) + abs(n.z);
	n /= max(l1, 1e-8);
	if (n.z < 0.0) n.xy = (1.0 - abs(n.yx)) * octSignNotZero(n.xy);
	return n.xy * 0.5 + 0.5;
}

vec3 octDecode(vec2 e) {
	vec3 v = vec3(e.x, e.y, 1.0 - abs(e.x) - abs(e.y));

	if (v.z < 0.0) {
		v.xy = (vec2(1.0) - abs(v.yx)) * octSignNotZero(v.xy);
	}

	return normalize(v);
}

float snorm8ToFloat(int v) { return clamp(float(v) / 127.0, -1.0, 1.0); }

float snorm16ToFloat(int v) { return clamp(float(v) / 32767.0, -1.0, 1.0); }

vec2 unpackUV(uint16_t uvX, uint16_t uvY) {
	uint packed = (uint(uvY) << 16u) | uint(uvX);
	return unpackHalf2x16(packed);
}

vec4 unpackRGBA8(uint packedRGBA8) {
	return unpackUnorm4x8(packedRGBA8);
}

AABB TransformAABB(AABB localAABB, mat4 transform)
{
	AABB worldAABB;

	vec3 center = 0.5 * (localAABB.vmin + localAABB.vmax);
	vec3 extent = 0.5 * (localAABB.vmax - localAABB.vmin);

	vec3 worldCenter = (transform * vec4(center, 1.0)).xyz;

	mat3 rotationScale = mat3(transform);
	mat3 absMatrix = mat3(
		abs(rotationScale[0]),
		abs(rotationScale[1]),
		abs(rotationScale[2]));

	vec3 worldExtent = absMatrix * extent;

	worldAABB.vmin = worldCenter - worldExtent;
	worldAABB.vmax = worldCenter + worldExtent;

	return worldAABB;
}

float luminance(vec3 color) {
	return max(dot(color, vec3(0.2126, 0.7152, 0.0722)), 1e-5);
}

// suppresses fireflies by weighting bright pixels less
float KarisWeight(vec3 color)
{
	return 1.0 / (1.0 + luminance(color));
}

float softThreshold(float value, float threshold, float knee)
{
	float kneeMin = threshold - knee;
	float kneeMax = threshold + knee;

	if (value <= kneeMin) return 0.0;

	if (value >= kneeMax) return value - threshold;

	// Smooth in-between
	float t = (value - kneeMin) / max(kneeMax - kneeMin, 1e-6);
	t = t * t * (3.0 - 2.0 * t);

	float soft = (value - threshold) * t;
	return max(soft, 0.0);
}


float saturate(float value) { return clamp(value, 0.0, 1.0); }
vec2 saturate(vec2 value)  { return clamp(value, vec2(0.0), vec2(1.0)); }
vec4 saturate(vec4 value)  { return clamp(value, vec4(0.0), vec4(1.0)); }

float mad(float a, float b, float c) { return a * b + c; }
vec2 mad(vec2 a, vec2 b, vec2 c)   { return a * b + c; }
vec3 mad(vec3 a, vec3 b, vec3 c)   { return a * b + c; }
vec4 mad(vec4 a, vec4 b, vec4 c)   { return a * b + c; }
vec2 mad(float a, vec2 b, vec2 c)  { return a * b + c; }

void SMAAMovc(bvec2 cond, inout vec2 variable, vec2 value) {
	if (cond.x) variable.x = value.x;
	if (cond.y) variable.y = value.y;
}

void SMAAMovc(bvec4 cond, inout vec4 variable, vec4 value) {
	SMAAMovc(cond.xy, variable.xy, value.xy);
	SMAAMovc(cond.zw, variable.zw, value.zw);
}

vec3 linearToSRGB(vec3 x)
{
	x = max(x, vec3(0.0));

	bvec3 lo = lessThanEqual(x, vec3(0.0031308));

	vec3 low  = x * 12.92;
	vec3 high = 1.055 * pow(x, vec3(1.0 / 2.4)) - 0.055;

	return mix(high, low, lo);
}

vec3 sRGBToLinear(vec3 x)
{
	x = max(x, vec3(0.0));

	bvec3 lo = lessThanEqual(x, vec3(0.04045));

	vec3 low  = x / 12.92;
	vec3 high = pow((x + 0.055) / 1.055, vec3(2.4));

	return mix(high, low, lo);
}

float interleavedGradientNoise(vec2 pixel) {
	const vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
	return fract(magic.z * fract(dot(pixel, magic.xy)));
}

// https://github.com/PanosK92/SpartanEngine/blob/4a0fe6d6d6ae54be08d5e9541a75adfb9d32d35f/data/shaders/common.hlsl#L434
float temporalInterleavedGradientNoise(vec2 screen_pos, int frame_count, float taaOn) {
	const float RPC_16 = 0.0625;

	// temporal factor
	float animate      = saturate(taaOn + 1.0);
	float frame_step   = float(frame_count % 16) * RPC_16 * animate;
	screen_pos.x      += frame_step * 4.7526;
	screen_pos.y      += frame_step * 3.1914;

	vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
	return fract(magic.z * fract(dot(screen_pos, magic.xy)));
}

// fast 1d hash
float hash(float p) {
	// scale input, convert to uint for bit manipulation
	uint u = floatBitsToUint(p * 3141592653.0);

	// mix with multiply and xor, normalize to [0,1)
	return float(u * u * 3141592653u) / 4294967295.0;
}

// fast 2d hash
float hash(vec2 p) {
	// scale each component, convert to uint2
	uvec2 u = floatBitsToUint(p * vec2(141421356.0, 2718281828.0));

	// combine with xor, mix, normalize to [0,1)
	return float((u.x ^ u.y) * 3141592653u) / 4294967295.0;
}

float createPhase(vec2 pixelCoord)
{
	return interleavedGradientNoise(pixelCoord) * TWO_PI;
}

float createPhaseTemporal(vec2 pixelCoord, uint frameIndex)
{
	vec2 jitteredPixel = pixelCoord + float(frameIndex) * vec2(1.618033988, 1.324717957);
	return interleavedGradientNoise(jitteredPixel) * TWO_PI;
}

mat2 phaseToRotation(float ang)
{
	return mat2(cos(ang), -sin(ang), sin(ang), cos(ang));
}

mat2 createHash(vec2 pixelCoord)
{
	return phaseToRotation(createPhase(pixelCoord));
}

mat2 createHashTemporal(vec2 pixelCoord, uint frameIndex)
{
	return phaseToRotation(createPhaseTemporal(pixelCoord, frameIndex));
}

uint packNormalMetalModel(vec3 N, float metal, uint shadingModel)
{
	vec2 oct = octEncode(N);
	uint ox  = uint(round(clamp(oct.x, 0.0, 1.0) * 2047.0));
	uint oy  = uint(round(clamp(oct.y, 0.0, 1.0) * 2047.0));
	uint m   = uint(round(clamp(metal,  0.0, 1.0) * 63.0));
	return ox | (oy << 11) | (m << 22) | ((shadingModel & 0xFu) << 28);
}

void unpackNormalMetalModel(uint p, out vec3 N, out float metal, out uint shadingModel)
{
	vec2 oct;
	oct.x        = float( p        & 0x7FFu) / 2047.0;
	oct.y        = float((p >> 11) & 0x7FFu) / 2047.0;
	metal        = float((p >> 22) & 0x3Fu)  / 63.0;
	shadingModel =       (p >> 28) & 0xFu;
	N            = octDecode(oct * 2.0 - 1.0);
}

void unpackNormalMetal(uint p, out vec3 N, out float metal)
{
	vec2 oct;
	oct.x = float( p        & 0x7FFu) / 2047.0;
	oct.y = float((p >> 11) & 0x7FFu) / 2047.0;
	metal = float((p >> 22) & 0x3Fu)  / 63.0;
	N     = octDecode(oct * 2.0 - 1.0);
}

vec3 unpackGBufferNormal(uint p)
{
	vec2 oct;
	oct.x = float( p        & 0x7FFu) / 2047.0;
	oct.y = float((p >> 11) & 0x7FFu) / 2047.0;
	return octDecode(oct * 2.0 - 1.0);
}

float unpackGBufferMetal(uint p)
{
	return float((p >> 22) & 0x3Fu)  / 63.0;
}

vec4 unpackEdges(float _packedVal)
{
	uint packedVal = uint(_packedVal * 255.5);
	vec4 edgesLRTB;
	edgesLRTB.x = float((packedVal >> 6) & 0x03) / 3.0;          // there's really no need for mask (as it's an 8 bit input) but I'll leave it in so it doesn't cause any trouble in the future
	edgesLRTB.y = float((packedVal >> 4) & 0x03) / 3.0;
	edgesLRTB.z = float((packedVal >> 2) & 0x03) / 3.0;
	edgesLRTB.w = float((packedVal >> 0) & 0x03) / 3.0;

	return saturate(edgesLRTB);
}

// Lighting struct
struct LocalLight {
	vec3 position;
	float radius;

	vec3 color;
	float intensity;

	vec3 direction;
	float innerCos;

	float outerCos;
	uint flags;

	float sourceRadius;
	float sourceLength;

	float changeRate;
	float lumens;
};

struct ShadowCSM {
	mat4 cascadeVP[MAX_CASCADES];
	mat4 cascadeLightViews[MAX_CASCADES];
	mat4 cascadeInvTransVP[MAX_CASCADES];
	vec4 cascadeSplits;
	// x=shadowAtlasID, y=cascadeCount, z=atlasTexelSize(1/atlasHeight)
	vec4 params;
	// xy = uvScale, zw = uvOffset (per cascade)
	vec4 atlasUV[MAX_CASCADES];
	vec4 maxPcfFilterRadiusTexels;
	vec4 maxPcssFilterRadiusTexels;
	vec4 cascadeWorldTexels;
	// x = tan(sunAngularRadius), y = minFilterRadiusTexels,
	// z = searchRadiusScale, w = maxNormalOffsetTexels
	vec4 pcss;
	// x = contactOffsetTexels, y = offsetGapFraction
	vec4 pcssBias;
};

struct ShadowCullData {
	vec4 receiverLSMin[MAX_CASCADES];   // xyz used, w = pad
	vec4 receiverLSMax[MAX_CASCADES];

	// per-cascade active flag — 0 means no visible receivers, skip entirely
	uvec4 cascadeActive;                // .x=c0 .y=c1 .z=c2 .w=c3
};

struct VolumetricShadowInfo
{
	mat4 cascadeVP;
	mat4 cascadeLightView;
	vec4 params;
	// x = shadow map ID
	// y = cascadeWorldTexel
	// z = shadow texel size
	// w = light-space epsilon

	vec4 receiverLSMin;
	vec4 receiverLSMax;
};

struct ClusteredData {
	uint tileSizeX;
	uint tileSizeY;
	uint zSlices;
	uint maxLightsPerCluster;
	uint tileCountX;
	uint tileCountY;
	uint clusterCount;
	uint maxVisibleLights;

	vec4 pad0[6];
};

// inline uniform block
struct DebugToggles
{
	uint enableLensFlare;
	uint enableChromaticAberration;
	uint enableSSS;
	uint enableFlashlight;

	uint aaMode;
	uint giMode;
	uint enableShadows;
	uint enableVolumetrics;

	uint activeEnvMap;
	uint disableOcclusionCull;
	uint renderingMode;
	uint debugView;

	float depthScale;
	uint enableWireframe;
	uint sunShadowFilter;
	uint enableRTReflections;

	uint enableProfilerView;
	uint enableSettings;
	uint enableBloom;
	float bloomIntensity;

	uint showOpaqueOBBs;
	uint showTransparentOBBs;
	uint activeInstanceCount;
	uint activeLightCount;

	uint activeRTInstances;
	uint csmAtlasCached;
	uint worldProbesDebugView;
	uint pad0;
};

bool uintBool(uint x) { return x != 0u; }


// =================================
// === GLOBAL ssbo address table ===
// =================================
layout(set = GLOBAL_SET, binding = ADDRESS_TABLE_BINDING, scalar) readonly buffer GlobalAddressTableBuffer {
	GPUAddressTable globalAddressTable;
};
uint64_t getABTGlobalAddress(uint id) { return globalAddressTable.addrs[id]; }

// ===========================
// === GLOBAL uniform sets ===
// ===========================

struct AtmosphereResource
{
	uvec4 textureIDs; // x transmittance; y skyView, z lighting atlas  w unused.
	uvec4 transmittanceExtent;
};

layout(set = GLOBAL_SET, binding = GLOBAL_BINDING_ATMOSPHERE, scalar) uniform AtmosphereResourceUBO
{
	AtmosphereResource atmosphere;
};
AtmosphereResource getAtmosphereResourceUBO() { return atmosphere; }

layout(set = GLOBAL_SET, binding = GLOBAL_BINDING_DEBUG_INLINE, scalar) uniform DebugData {
	DebugToggles debug;
};

DebugToggles getDebugToggles() { return debug; }

bool IsTAAEnabled() { return debug.aaMode != AA_OFF; }


// ============================================
// === GLOBAL ADDRESSS TABLE BUFFER GETTERS ===
// ============================================

// instance inputs
layout(buffer_reference, scalar) readonly buffer InstanceInputsBuffer {
	InstanceInput instanceInputs[];
};
// pass gl_InstanceIndex
InstanceInputsBuffer getInstanceInputBuffer() {
	return InstanceInputsBuffer(getABTGlobalAddress(ABT_InstanceInputs));
}

layout(buffer_reference, scalar) readonly buffer DrawBinKeysBuffer {
	BinKey keys[BIN_TABLE_SIZE];
	uvec2  dense[MAX_DRAW_BINS];
};
DrawBinKeysBuffer getDrawBinKeyBuffer() {
	return DrawBinKeysBuffer(getABTGlobalAddress(ABT_DrawBinKeys));
}

uint binLookup(uint meshID, uint materialID)
{
	uint h = (meshID * 2654435761u) ^ (materialID * 2246822519u);
	h &= (BIN_TABLE_SIZE - 1u);

	// open addressing, table built CPU-side so termination is guaranteed
	for (uint probe = 0u; probe < 64u; ++probe)
	{
		BinKey key = getDrawBinKeyBuffer().keys[h];
		if (key.meshID == meshID && key.materialID == materialID) return key.binID;
		if (key.meshID == INVALID_U32) return INVALID_U32; // unregistered pair
		h = (h + 1u) & (BIN_TABLE_SIZE - 1u);
	}
	return INVALID_U32;
}

// materials, vertices, indices, all ready at render time and uploaded at end of asset loading
layout(buffer_reference, scalar) readonly buffer MaterialBuffer {
	Material materials[];
};
MaterialBuffer getMaterialBuffer() {
	uint64_t addr = getABTGlobalAddress(ABT_Material);
	return MaterialBuffer(addr);
}

layout(buffer_reference, scalar) readonly buffer VertexBuffer {
	Vertex vertices[];
};
VertexBuffer getVertexBuffer() {
	uint64_t addr = getABTGlobalAddress(ABT_Vertex);
	return VertexBuffer(addr);
}

layout(buffer_reference, scalar) readonly buffer IndexBuffer {
	uint indices[];
};
IndexBuffer getIndexBuffer() {
	uint64_t addr = getABTGlobalAddress(ABT_Index);
	return IndexBuffer(addr);
}

layout(buffer_reference, scalar) readonly buffer MeshBuffer {
	Mesh meshes[];
};
MeshBuffer getMeshBuffer() {
	uint64_t addr = getABTGlobalAddress(ABT_Mesh);
	return MeshBuffer(addr);
}

const float EXPOSURE_MIN = 1e-9;

layout(buffer_reference, scalar) buffer LuminanceBuffer {
	vec4 luminanceSums[MAX_LUMINANCE_GROUPS];
};
LuminanceBuffer getLuminanceBuffer() {
	uint64_t addr = getABTGlobalAddress(ABT_Luminance);
	return LuminanceBuffer(addr);
}

float getPreExposure()      { return max(getLuminanceBuffer().luminanceSums[0].x, EXPOSURE_MIN); }
float getPrevPreExposure()  { return max(getLuminanceBuffer().luminanceSums[0].y, EXPOSURE_MIN); }
float getPreExposureDelta() { return getPreExposure() / getPrevPreExposure(); }

vec3 sanitizeHDR(vec3 c, float maxValue)
{
	c = mix(c, vec3(0.0), isnan(c));
	return clamp(c, vec3(0.0), vec3(maxValue));
}

layout(buffer_reference, scalar) readonly buffer MeshletBuffer { Meshlet meshlets[]; };
MeshletBuffer getMeshletBuffer() {
	uint64_t addr = getABTGlobalAddress(ABT_Meshlet);
	return MeshletBuffer(addr);
}
layout(buffer_reference, scalar) readonly buffer MeshletVertsBuffer { uint verts[]; };
MeshletVertsBuffer getMeshletVertsBuffer() {
	uint64_t addr = getABTGlobalAddress(ABT_MeshletVertices);
	return MeshletVertsBuffer(addr);
}
layout(buffer_reference, scalar) readonly buffer MeshletTrisBuffer  { uint8_t tris[]; };
MeshletTrisBuffer getMeshletTrisBuffer() {
	uint64_t addr = getABTGlobalAddress(ABT_MeshletTriangles);
	return MeshletTrisBuffer(addr);
}

layout(buffer_reference, scalar) readonly buffer StaticTransformsBuffer {
	mat4 transforms[];
};
StaticTransformsBuffer getStaticTransformsBuffer() {
	uint64_t addr = getABTGlobalAddress(ABT_StaticTransforms);
	return StaticTransformsBuffer(addr);
}

struct SphericalHarmonic
{
	vec3 sh[9];
};

layout(buffer_reference, scalar) readonly buffer BlasAddressesBuffer {
	uvec2 blasAddrs[];
};
BlasAddressesBuffer getBlasAddressesBuffer() {
	uint64_t addr = getABTGlobalAddress(ABT_BLASAddresses);
	return BlasAddressesBuffer(addr);
}

// ================================
// === FRAME ssbo address table ===
// =================================

layout(set = FRAME_SET, binding = ADDRESS_TABLE_BINDING, scalar) readonly buffer FrameAddressTableBuffer {
	GPUAddressTable frameAddressTable;
};
uint64_t getABTFrameAddress(uint id) { return frameAddressTable.addrs[id]; }

// ===========================
// === FRAME uniform sets ====
// ===========================
layout(set = FRAME_SET, binding = FRAME_BINDING_SCENE) uniform SceneUBO {
	SceneData scene;
};
SceneData getSceneData() { return scene; }

layout(set = FRAME_SET, binding = FRAME_BINDING_CSM) uniform ShadowUBO {
	ShadowCSM csm;
};
ShadowCSM getShadowCSM() { return csm; }

layout(set = FRAME_SET, binding = FRAME_BINDING_CLUSTERED) uniform ClusteredUBO {
	ClusteredData clusteredData;
};
ClusteredData getClusteredData() { return clusteredData; }

layout(set = FRAME_SET, binding = FRAME_BINDING_VOLUMETRIC) uniform VolumetricShadowUBO {
	VolumetricShadowInfo volShadow;
};
VolumetricShadowInfo getVolumetricShadowInfo() { return volShadow; }

// ===========================================
// === FRAME ADDRESSS TABLE BUFFER GETTERS ===
// ===========================================

// visible instances
layout(buffer_reference, scalar) buffer VisibleInstanceBuffer {
	VisibleInstance visibles[];
};
VisibleInstanceBuffer getVisibleInstanceBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_VisibleInstances);
	return VisibleInstanceBuffer(addr);
}

// pass gl_InstanceIndex
layout(buffer_reference, scalar) buffer DrawInstanceIDsBuffer {
	uint ids[];
};
DrawInstanceIDsBuffer getDrawInstanceIDsBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_DrawInstanceIDs);
	return DrawInstanceIDsBuffer(addr);
}

// draw bin
layout(buffer_reference, scalar) buffer DrawBinBuffer {
	DrawBin drawBins[];
};
DrawBinBuffer getDrawBinBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_DrawBins);
	return DrawBinBuffer(addr);
}

// draw bin counter
layout(buffer_reference, scalar) buffer DrawBinCounterBuffer {
	uint drawBinCounters[];
};
DrawBinCounterBuffer getDrawBinCounterBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_DrawBinCounters);
	return DrawBinCounterBuffer(addr);
}

struct VisibilityCounters
{
	uint counts[VIS_SLOT_COUNT];
};

// instance visibility counters
layout(buffer_reference, scalar) buffer InstanceVisibilityBuffer {
	VisibilityCounters counters;
};
InstanceVisibilityBuffer getInstanceVisibilityBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_InstanceVisibility);
	return InstanceVisibilityBuffer(addr);
}

layout(buffer_reference, scalar) buffer InstanceCursorsBuffer {
	InstanceCursors cursors;
};
InstanceCursorsBuffer getInstanceCursorsBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_InstanceCursors);
	return InstanceCursorsBuffer(addr);
}

// instance visible count
layout(buffer_reference, scalar) buffer VisibleCountBuffer {
	uint count;
};
VisibleCountBuffer getVisibleCountBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_VisibleCount);
	return VisibleCountBuffer(addr);
}

layout(buffer_reference, scalar) buffer InstanceStreamsBuffer {
	StreamEntry entries[];
};
InstanceStreamsBuffer getInstanceStreamsBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_InstanceStreams);
	return InstanceStreamsBuffer(addr);
}

layout(buffer_reference, scalar) buffer IndirectDrawCountsBuffer {
	uint counts[VIS_SLOT_COUNT];
};
IndirectDrawCountsBuffer getIndirectDrawCountsBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_IndirectDrawCounts);
	return IndirectDrawCountsBuffer(addr);
}
//
//bool hasPrimaryVisibles()
//{
//	VisibilityCounters c = getInstanceVisibilityBuffer().counters;
//	return (c.counts[VIS_SLOT_OPAQUE] + c.counts[VIS_SLOT_TRANSPARENT]) > 0u;
//}
//
//bool hasOpaqueVisibles()
//{
//	return getInstanceVisibilityBuffer().counters.counts[VIS_SLOT_OPAQUE] > 0u;
//}
//
//bool hasCSMCasters()
//{
//	VisibilityCounters c = getInstanceVisibilityBuffer().counters;
//	return (c.counts[VIS_SLOT_CSM0] +
//			c.counts[VIS_SLOT_CSM1] +
//			c.counts[VIS_SLOT_CSM2]) > 0u;
//}
//
//bool hasCascadeCasters(uint cascadeSlot)
//{
//	return getInstanceVisibilityBuffer().counters.counts[cascadeSlot] > 0u;
//}
//
//bool hasFlashlightCasters()
//{
//	return getInstanceVisibilityBuffer().counters.counts[VIS_SLOT_FLASHLIGHT] > 0u;
//}
//
//bool hasAnyVisibles()
//{
//	return getVisibleCountBuffer().count > 0u;
//}
//

layout(buffer_reference, scalar) buffer ShadowCullDataBuffer {
	ShadowCullData data;
};
ShadowCullDataBuffer getShadowCullDataBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_ShadowCullData);
	return ShadowCullDataBuffer(addr);
}

layout(buffer_reference, scalar) readonly buffer DynamicTransformsBuffer {
	mat4 transforms[];
};
DynamicTransformsBuffer getDynamicTransformsBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_DynamicTransforms);
	return DynamicTransformsBuffer(addr);
}

layout(buffer_reference, scalar) readonly buffer MotionMatricesBuffer {
	mat4 matrices[];
};
MotionMatricesBuffer getMotionMatricesBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_MotionMatrices);
	return MotionMatricesBuffer(addr);
}

mat4 getInstanceTransform(InstanceInput inst)
{
	uint idx = transformIndex(inst.transformID);
	return isDynamicTransform(inst.transformID)
		? getDynamicTransformsBuffer().transforms[idx]
		: getStaticTransformsBuffer().transforms[idx];
}

// gpu stats
layout(buffer_reference, scalar) writeonly buffer GPUStatsBuffer {
	GPUStats gpuStats;
};
GPUStatsBuffer getGPUStatsBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_DrawStats);
	return GPUStatsBuffer(addr);
}

// Indirect dispatch arguments
layout(buffer_reference, scalar) buffer DispatchIndirectArgsBuffer {
	uvec4 args[INDIRECT_DISPATCH_SLOT_COUNT]; // args[i].xyz used
};
DispatchIndirectArgsBuffer getIndirectDispatchBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_DispatchIndirectArgs);
	return DispatchIndirectArgsBuffer(addr);
}

// Task Dispatch
layout(buffer_reference, scalar) buffer TaskDispatchBuffer {
	TaskDispatch taskDispatch[];
};
TaskDispatchBuffer getTaskDispatchBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_TaskDispatch);
	return TaskDispatchBuffer(addr);
}

layout(buffer_reference, scalar) buffer MeshletVisibilityABuffer {
	uint bits[];
};
MeshletVisibilityABuffer getMeshletVisibilityABuffer() {
	uint64_t addr = getABTFrameAddress(ABT_MeshletVisibilityA);
	return MeshletVisibilityABuffer(addr);
}

layout(buffer_reference, scalar) buffer MeshletVisibilityBBuffer {
	uint bits[];
};
MeshletVisibilityBBuffer getMeshletVisibilityBBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_MeshletVisibilityB);
	return MeshletVisibilityBBuffer(addr);
}

layout(buffer_reference, scalar) buffer DebugCountersBuffer {
	DebugCounters counters;
};
DebugCountersBuffer getDebugCountersBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_DebugCounts);
	return DebugCountersBuffer(addr);
}

layout(buffer_reference, scalar) buffer DebugItemsBuffer {
	DebugItem items[];
};
DebugItemsBuffer getDebugItemsBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_DebugItems);
	return DebugItemsBuffer(addr);
}

layout(buffer_reference, scalar) buffer DebugVertexBuffer {
	DebugVertex vertices[];
};
DebugVertexBuffer getDebugVertexBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_DebugVertex);
	return DebugVertexBuffer(addr);
}

layout(buffer_reference, scalar) buffer DebugDrawBuffer {
	IndirectDrawCmd draw;
};
DebugDrawBuffer getDebugDrawBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_DebugDraw);
	return DebugDrawBuffer(addr);
}


// =================================
// === clustered shading buffers ===
// =================================

layout(buffer_reference, scalar) readonly buffer LightBuffer {
	LocalLight lights[];
};
LightBuffer getLightBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_Lights);
	return LightBuffer(addr);
}

layout(buffer_reference, scalar) buffer VisibleLightCount {
	uint count;
};
VisibleLightCount getVisibleLightCountBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_VisibleLightCount);
	return VisibleLightCount(addr);
}

layout(buffer_reference, scalar) buffer VisibleLightIDs {
	uint ids[];
};
VisibleLightIDs getVisibleLightIDsBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_VisibleLightIDs);
	return VisibleLightIDs(addr);
}

layout(buffer_reference, scalar) buffer ClusterCounts {
	uint counts[];
};
ClusterCounts getClusterCountsBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_ClusterCounts);
	return ClusterCounts(addr);
}

layout(buffer_reference, scalar) buffer ClusterOffsets {
	uint offsets[];
};
ClusterOffsets getClusterOffsetsBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_ClusterOffsets);
	return ClusterOffsets(addr);
}

layout(buffer_reference, scalar) buffer ClusterCursors {
	uint cursors[];
};
ClusterCursors getClusterCursorsBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_ClusterCursors);
	return ClusterCursors(addr);
}

layout(buffer_reference, scalar) buffer ClusterLightIDs {
	uint lightIDs[];
};
ClusterLightIDs getClusterLightIDsBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_ClusterLightIDs);
	return ClusterLightIDs(addr);
}

// One per tile: (minSlice, maxSlice)
layout(buffer_reference, scalar) buffer ClusterTileSliceRanges {
	uvec2 ranges[];
};
ClusterTileSliceRanges getClusterTileSliceRangesBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_ClusterTileSliceRanges);
	return ClusterTileSliceRanges(addr);
}

// Scratch for scan
layout(buffer_reference, scalar) buffer ClusterScanScratch {
	uint scratch[];
};
ClusterScanScratch getClusterScratchBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_ClusterScanScratch);
	return ClusterScanScratch(addr);
}

layout(buffer_reference, scalar) buffer ClusterTileTransparentNear {
	uint nearDepth[];
};
ClusterTileTransparentNear getClusterTileTransparentNearBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_ClusterTileTransparentNear);
	return ClusterTileTransparentNear(addr);
}

struct ShadowInvalidVolume
{
	vec3 boundsMin;
	vec3 boundsMax;
};

layout(buffer_reference, scalar) buffer ShadowInvalidVolumesBuffer {
	uint count;
	uint pad0[3];
	ShadowInvalidVolume volumes[];
};
ShadowInvalidVolumesBuffer getShadowInvalidVolumesBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_ShadowInvalidVolumes);
	return ShadowInvalidVolumesBuffer(addr);
}

layout(buffer_reference, scalar) buffer VolClusterCounts {
	uint counts[];
};

VolClusterCounts getVolClusterCountsBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_VolClusterCounts);
	return VolClusterCounts(addr);
}

layout(buffer_reference, scalar) buffer VolClusterOffsets {
	uint offsets[];
};

VolClusterOffsets getVolClusterOffsetsBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_VolClusterOffsets);
	return VolClusterOffsets(addr);
}

layout(buffer_reference, scalar) buffer VolClusterLightIDs {
	uint lightIDs[];
};

VolClusterLightIDs getVolClusterLightIDsBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_VolClusterLightIDs);
	return VolClusterLightIDs(addr);
}

layout(buffer_reference, scalar) buffer VolClusterCursors {
	uint cursors[];
};

VolClusterCursors getVolClusterCursorsBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_VolClusterCursors);
	return VolClusterCursors(addr);
}

void unpackVertex(
	int vertexId,
	out vec2 uv,
	out vec4 color,
	out vec3 normal,
	out vec3 tangent,
	out float tangentHandedness,
	out vec3 position)
{
	Vertex vtx = getVertexBuffer().vertices[vertexId];
	position = vtx.position;

	uv    = unpackUV(vtx.uvX, vtx.uvY);
	color = unpackRGBA8(vtx.colorRGBA8);

	normal = octDecode(vec2(snorm16ToFloat(int(vtx.normalX)),
							snorm16ToFloat(int(vtx.normalY))));

	int ty = int(vtx.tangentY);
	tangentHandedness = (ty >= 0) ? 1.0 : -1.0;

	float octX = snorm16ToFloat(int(vtx.tangentX));
	float octY = snorm16ToFloat(abs(ty)) * 2.0 - 1.0;

	tangent = octDecode(vec2(octX, octY));
}

vec2 GetPrevUV(vec4 prevClip)
{
	vec2 prevUv = (prevClip.xy / prevClip.w) * 0.5 + 0.5;
	prevUv.y    = 1.0 - prevUv.y;
	return prevUv;
}

#include "textures.glsl"

#endif
