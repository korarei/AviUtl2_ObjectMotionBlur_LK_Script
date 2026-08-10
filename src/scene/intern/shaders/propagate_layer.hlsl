#include "layer_candidate.hlsli"

struct Output {
    float4 first : SV_Target0;
    float4 second : SV_Target1;
};

static const float kTileSize = 16.0;
static const float kPropagationDistancePrecision = 0.008;

Texture2D layer_textures[2] : register(t0);
cbuffer params : register(b0) {
    float2 resolution;
    float2 shutter;
    int step;
}

float EvaluatePropagationWeight(float4 candidate, int2 dst) {
    const float2 origin = UnpackOrigin(candidate.w) * kTileSize;

    const float4 sweep = float4(candidate.xy * shutter.x, candidate.xy * shutter.y);
    const float2 sweep_min = min(sweep.xy, sweep.zw);
    const float2 sweep_max = max(sweep.xy, sweep.zw);
    const float2 candidate_min = origin + sweep_min - 1.0;
    const float2 candidate_max = origin + kTileSize + sweep_max + 1.0;
    const float2 target_min = float2(dst) * kTileSize;
    const float2 target_max = min(target_min + kTileSize, resolution);
    const float2 dist = max(max(candidate_min - target_max, target_min - candidate_max), 0.0);

    return exp(-dot(dist, dist) * kPropagationDistancePrecision);
}

Output main(float4 pos : SV_Position) {
    uint2 size;
    layer_textures[0].GetDimensions(size.x, size.y);
    const int2 upper = int2(size) - 1;

    const int2 dst = int2(pos.xy);

    Output output = {float4(0.0, 0.0, 0.0, 0.0), float4(0.0, 0.0, 0.0, 0.0)};

    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            const int3 loc = int3(dst + int2(x, y) * step, 0);

            if (any(loc.xy < 0) || any(loc.xy > upper)) {
                continue;
            }

            float4 first = layer_textures[0].Load(loc);
            float4 second = layer_textures[1].Load(loc);

            InsertFlowCandidate(first, EvaluatePropagationWeight(first, dst), output.first, output.second);
            InsertFlowCandidate(second, EvaluatePropagationWeight(second, dst), output.first, output.second);
        }
    }

    return output;
}
