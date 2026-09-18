#ifndef SSGI_PUSH_GLSL
#define SSGI_PUSH_GLSL

const float AO_SUPPORT_EPSILON = 1e-5;

bool aoFinite(float v)
{
	return !isnan(v) && !isinf(v);
}

layout(push_constant) uniform PushConstantData
{
	float effectRadius;
	float effectFalloffRange;
	vec2 ndcToViewMul_x_PixelSize;

	float radiusMultiplier;
	float sampleDistributionPower;
	uint noiseIndex;
	uint hilbertLutID;

	float denoiseBlurBeta;
	uint isFinalPass;
	float upsampleDepthSigma;
	float giClampMax;

	float giReprojTolerance;
	float giTemporalAlpha;
	float giFallbackStrength;
	uint aoHistoryValid;

	float aoHistoryWeight;
	float aoDepthTolerance;
	float aoNormalThreshold;
	float aoMinObservation;

	float aoNeighborConfidence;
	float aoClipConfidence;
	float aoMaxHistorySamples;
	float aoInitialVariance;

	float aoClipSigma;
	float aoClipMaxSigma;
	float aoClipMargin;
	float aoReactiveThreshold;

	float aoMissingHoldFrames;
	float aoMissingMaxFrames;
	float aoMissingAgeDecay;
	float aoHistoryFootprintMinSupport;
} pc;

#endif
