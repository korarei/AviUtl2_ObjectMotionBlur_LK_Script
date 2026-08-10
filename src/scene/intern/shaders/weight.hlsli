static const float2 kGuidePrecision = float2(3.0, 48.0);

inline float EvaluateGuideWeight(float4 base, float4 src) {
    const float4 diff = base - src;
    return exp(-kGuidePrecision.x * dot(diff.rgb, diff.rgb) - kGuidePrecision.y * diff.a * diff.a);
}
