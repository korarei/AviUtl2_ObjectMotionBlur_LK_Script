static const float kEpsilon = 1.0e-5;
static const float kMinimumConfidence = 0.02;
static const float kOpacityScoreScale = 8.0;
static const float3 kLumaCoefficients = float3(0.2126, 0.7152, 0.0722);

Texture2D regularized_flow_texture : register(t0);
Texture2D source_texture : register(t1);
Texture2D depth_texture : register(t2);
SamplerState linear_sampler : register(s0);
cbuffer params : register(b0) {
    uint2 resolution;
    uint grid_size;
    float flow_scale;
}

inline float EvaluateScore(float4 flow, float alpha) {
    const float mask = step(kMinimumConfidence, flow.z) * step(kEpsilon, alpha);
    return mask * flow.z * flow.z * flow.w * (alpha * kOpacityScoreScale);
}

float4 main(float4 pos : SV_Position) : SV_Target {
    const int3 loc = int3(pos.xy, 0);
    const float4 flow = regularized_flow_texture.Load(loc);
    const float4 color = source_texture.Load(loc);
    const float4 depth = depth_texture.Sample(linear_sampler, pos.xy * rcp(float2(resolution)));

    return float4(flow.xy, EvaluateScore(flow, color.a), saturate(dot(depth.rgb, kLumaCoefficients)));
}
