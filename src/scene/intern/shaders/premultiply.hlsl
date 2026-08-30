static const float2 kTrustLower = float2(0.2, 0.5);
static const float2 kTrustUpper = float2(0.6, 0.8);
static const float3 kBT709 = float3(0.2126, 0.7152, 0.0722);

Texture2D flow_map : register(t0);
Texture2D depth_map : register(t1);
SamplerState linear_sampler : register(s0);
cbuffer params : register(b0) {
    uint2 resolution;
};

inline float EvaluateTrustWeight(float4 flow) {
    const float2 trust = smoothstep(kTrustLower, kTrustUpper, flow.zw);
    return min(trust.x * flow.z, trust.y * flow.w);
}

inline float SampleDepth(float2 uv) {
    return saturate(dot(depth_map.Sample(linear_sampler, uv).rgb, kBT709));
}

float4 main(float4 pos : SV_Position) : SV_Target {
    const float2 uv = pos.xy * rcp(float2(resolution));

    const float4 flow = flow_map.Load(int3(pos.xy, 0));
    const float alpha = EvaluateTrustWeight(flow);
    const float depth = SampleDepth(uv);

    return float4(flow.xy * alpha, alpha, depth * alpha);
}
