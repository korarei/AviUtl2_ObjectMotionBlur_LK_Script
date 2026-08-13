#include "flow_params.hlsli"

Texture2D depth_texture : register(t2);
Texture2D propagated_flow_texture : register(t3);
SamplerState linear_sampler : register(s0);

#include "propagate_common.hlsli"

inline float4 MergeCandidates(float4 first, float4 second) {
    if (second.z > first.z) {
        return second;
    }

    return first;
}

inline float4 SelectCandidate(float4 best, float4 candidate, float weight, float depth) {
    candidate.z *= weight;

    if (candidate.z < kMinimumPropagationScore ||
        candidate.w + kDepthLayerDifference < depth) {
        return best;
    }

    if (best.z <= 0.0) {
        return candidate;
    }

    if (candidate.w > best.w + kDepthLayerDifference) {
        return candidate;
    }

    if (best.w > candidate.w + kDepthLayerDifference) {
        return best;
    }

    if (EvaluateFlowSimilarity(best.xy, candidate.xy) >= 0.7) {
        return MergeCandidates(best, candidate);
    }

    if (candidate.z > best.z) {
        return candidate;
    }

    return best;
}

float4 main(float4 pos : SV_Position) : SV_Target {
    const int3 base_loc = int3(pos.xy, 0);
    const float base_depth = SampleDepth(pos.xy);

    float4 best = propagated_flow_texture.Load(base_loc);
    if (best.z < kMinimumPropagationScore ||
        best.w + kDepthLayerDifference < base_depth) {
        best.z = 0.0;
    }

    [unroll]
    for (int y = -kPropagationRadius; y <= kPropagationRadius; ++y) {
        [unroll]
        for (int x = -kPropagationRadius; x <= kPropagationRadius; ++x) {
            const int3 loc = base_loc + int3(x, y, 0) * propagation_step;
            if (any(loc.xy < 0) || any(loc.xy >= int2(resolution))) {
                continue;
            }

            const float4 candidate = propagated_flow_texture.Load(loc);
            if (candidate.z < kMinimumPropagationScore) {
                continue;
            }

            const float weight = EvaluateShutterWeight(candidate, loc.xy, base_loc.xy);
            best = SelectCandidate(best, candidate, weight, base_depth);
        }
    }

    return float4(best.xy, saturate(best.z), saturate(best.w));
}
