static const float kEpsilon = 1.0e-5;
static const int kPropagationRadius = 1;
static const float kDepthLayerDifference = 0.05;
static const float kPropagationMargin = 0.5;
static const float kPropagationDistancePrecision = 0.125;
static const float kMinimumConfidence = 0.02;
static const float kMinimumPropagationScore = kMinimumConfidence;
static const float kOpacityScoreScale = 8.0;
static const float3 kLumaCoefficients = float3(0.2126, 0.7152, 0.0722);

inline float SampleDepth(float2 pos) {
    return saturate(dot(depth_texture.Sample(linear_sampler, pos * rcp(float2(resolution))).rgb,
                        kLumaCoefficients));
}

inline float EvaluateScore(float4 flow, float alpha) {
    const float mask = step(kMinimumConfidence, flow.z) * step(kEpsilon, alpha);
    return saturate(mask * flow.z * flow.z * flow.w * (alpha * kOpacityScoreScale));
}

inline float EvaluateFlowSimilarity(float2 first, float2 second) {
    const float2 len = float2(length(first), length(second));
    const float norm = max(len.x, len.y);

    const float dir = saturate(dot(first, second) * rcp(max(len.x * len.y, kEpsilon)));
    const float mag = saturate(min(len.x, len.y) * rcp(max(norm, kEpsilon)));

    return lerp(dir * mag, 1.0, step(norm, kEpsilon));
}

inline float EvaluateShutterWeight(float4 candidate, int2 src, int2 dst) {
    const float2 pos = float2(src) + 0.5;
    const float2 target = float2(dst) + 0.5;
    const float2 start = pos + candidate.xy * shutter.x;
    const float2 end = pos + candidate.xy * shutter.y;
    const float2 segment = end - start;
    const float segment_length_squared = dot(segment, segment);
    const float segment_position = saturate(
        dot(target - start, segment) * rcp(max(segment_length_squared, kEpsilon)));
    const float2 closest = start + segment * segment_position;
    const float segment_distance = length(target - closest);
    const float support = 1.0 - smoothstep(
        kPropagationMargin,
        kPropagationMargin + 1.0,
        segment_distance);
    const float distance = max(segment_distance - kPropagationMargin, 0.0);

    return support * exp(-distance * distance * kPropagationDistancePrecision);
}
