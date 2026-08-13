#include "flow_params.hlsli"

Texture2D regularized_flow_texture : register(t0);
Texture2D source_texture : register(t1);
Texture2D depth_texture : register(t2);
SamplerState linear_sampler : register(s0);

#include "propagate_common.hlsli"

inline float4 LoadSeedCandidate(float2 pos) {
    const int3 loc = int3(pos, 0);
    const float4 flow = regularized_flow_texture.Load(loc);
    const float4 color = source_texture.Load(loc);
    const float depth = SampleDepth(pos);
    const float score = EvaluateScore(flow, color.a);

    return float4(flow.xy, score, depth);
}

float4 main(float4 pos : SV_Position) : SV_Target {
    return LoadSeedCandidate(pos.xy);
}
