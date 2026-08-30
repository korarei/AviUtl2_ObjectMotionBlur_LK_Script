static const float kEpsilon = 1.0e-5;
static const float kDepthTolerance = 0.03;
static const float3 kBT709 = float3(0.2126, 0.7152, 0.0722);

Texture2D flow_map : register(t0);
Texture2D depth_map : register(t1);
SamplerState linear_sampler : register(s0);
cbuffer params : register(b0) {
    uint4 resolution;
};

inline float SampleDepth(float2 uv) {
    return saturate(dot(depth_map.Sample(linear_sampler, uv).rgb, kBT709));
}

float4 main(float4 pos : SV_Position) : SV_Target {
    const float2 uv = pos.xy * rcp(float2(resolution.xy));

    const int2 base_loc = int2(pos.xy) * 2;
    const float base_depth = SampleDepth(uv);

    float4 flow = 0.0;
    float weight = 0.0;
    float confidence = 0.0;

    [unroll]
    for (int y = 0; y < 2; ++y) {
        [unroll]
        for (int x = 0; x < 2; ++x) {
            const int2 sample_loc = base_loc + int2(x, y);
            const float in_bounds = float(all(sample_loc < int2(resolution.zw)));

            const float4 sample_flow = flow_map.Load(int3(sample_loc, 0));
            const float sample_depth = sample_flow.w * rcp(max(sample_flow.z, kEpsilon));

            const float depth_weight = step(abs(sample_depth - base_depth), kDepthTolerance);
            const float valid = in_bounds * step(kEpsilon, sample_flow.z) * depth_weight;

            flow += sample_flow * valid;
            weight += sample_flow.z * valid;
            confidence = max(confidence, sample_flow.z * valid);
        }
    }

    return flow * confidence * rcp(max(weight, kEpsilon));
}
