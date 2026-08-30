static const float kEpsilon = 1.0e-5;
static const float kDepthTolerance = 0.03;
static const float3 kBT709 = float3(0.2126, 0.7152, 0.0722);

Texture2D flow_map : register(t0);
Texture2D coarse_map : register(t1);
Texture2D depth_map : register(t2);
SamplerState linear_sampler : register(s0);
cbuffer params : register(b0) {
    uint2 resolution;
};

inline float SampleDepth(float2 uv) {
    return saturate(dot(depth_map.Sample(linear_sampler, uv).rgb, kBT709));
}

float4 main(float4 pos : SV_Position) : SV_Target {
    const float2 uv = pos.xy * rcp(float2(resolution));

    const float4 base_flow = flow_map.Load(int3(pos.xy, 0));
    const float base_depth = SampleDepth(uv);

    float4 coarse = coarse_map.Sample(linear_sampler, uv);
    const float coarse_depth = coarse.w * rcp(max(coarse.z, kEpsilon));
    coarse *= step(kEpsilon, coarse.z) * step(abs(coarse_depth - base_depth), kDepthTolerance);

    const float4 flow = base_flow + coarse * (1.0 - saturate(base_flow.z));

    return float4(flow.xy * rcp(max(flow.z, kEpsilon)), flow.zz) * step(kEpsilon, flow.z);
}
