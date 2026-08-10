#include "weight.hlsli"
#include "layer_candidate.hlsli"

struct Output {
    float4 first : SV_Target0;
    float4 second : SV_Target1;
};

static const float kEpsilon = 1.0e-5;
static const int kTileSize = 16;

Texture2D classified_flow_texture : register(t0);
Texture2D source_texture : register(t1);

float PackMetadata(int2 origin, float depth) {
    return float(origin.x + origin.y * int(kCandidateStride)) + depth * kDepthPackScale;
}

float EvaluateSupport(int2 base_loc, float2 base_velocity, float base_depth, int2 upper) {
    const float4 base_color = source_texture.Load(int3(base_loc, 0));
    float support = 0.0;
    float norm = 0.0;

    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            const int3 loc = int3(clamp(base_loc + int2(x, y), 0, upper), 0);
            const float4 flow = classified_flow_texture.Load(loc);
            const float4 color = source_texture.Load(loc);

            const float depth_weight = EvaluateDepthWeight(base_depth, flow.w);
            const float weight = EvaluateGuideWeight(base_color, color) * depth_weight;
            support += weight * saturate(flow.z) * EvaluateFlowSimilarity(base_velocity, flow.xy);
            norm += weight;
        }
    }

    return saturate(support * rcp(max(norm, kEpsilon)));
}

Output main(float4 pos : SV_Position) {
    uint2 size;
    source_texture.GetDimensions(size.x, size.y);
    const int2 upper = int2(size) - 1;

    const int2 origin = int2(pos.xy);
    const int2 base_loc = origin * kTileSize;

    Output output = {float4(0.0, 0.0, 0.0, 0.0), float4(0.0, 0.0, 0.0, 0.0)};

    for (int y = 0; y < kTileSize; ++y) {
        for (int x = 0; x < kTileSize; ++x) {
            const int2 loc = base_loc + int2(x, y);

            if (any(loc > upper)) {
                continue;
            }

            float4 candidate = classified_flow_texture.Load(int3(loc, 0));
            if (candidate.z <= 0.0) {
                continue;
            }

            const float support = EvaluateSupport(loc, candidate.xy, candidate.w, upper);
            candidate.w = PackMetadata(origin, candidate.w);
            InsertFlowCandidate(candidate, support, output.first, output.second);
        }
    }

    return output;
}
