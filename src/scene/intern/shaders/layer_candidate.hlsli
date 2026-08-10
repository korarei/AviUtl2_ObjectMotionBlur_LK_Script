static const float kSameFlowLayerSimilarityThreshold = 0.7;
static const float kSameDepthLayerDifference = 0.05;
static const float kDepthPackScale = 0.49;
static const float kDepthSupportRange = 0.10;
static const uint kCandidateStride = 16384u;

inline float2 UnpackOrigin(float packed) {
    const uint v = uint(round(packed));
    return float2(v % kCandidateStride, v / kCandidateStride);
}

inline float EvaluateFlowSimilarity(float2 first, float2 second) {
    const float2 len = float2(length(first), length(second));
    const float len_max = max(len.x, len.y);

    const float dir = saturate(dot(first, second) * rcp(max(len.x * len.y, 1.0e-5)));
    const float norm = saturate(min(len.x, len.y) * rcp(max(len_max, 1.0e-5)));

    return lerp(dir * norm, 1.0, step(len_max, 1.0e-5));
}

inline float UnpackCandidateDepth(float packed) {
    return saturate(frac(packed) * rcp(kDepthPackScale));
}

inline float EvaluateDepthWeight(float base, float src) {
    return 1.0 - smoothstep(0.0, kDepthSupportRange, abs(base - src));
}

inline bool IsSameFlowLayer(float2 first_flow, float first_depth, float2 second_flow, float second_depth) {
    const float similarity = EvaluateFlowSimilarity(first_flow, second_flow);
    const float diff = abs(first_depth - second_depth);

    return similarity >= kSameFlowLayerSimilarityThreshold && diff <= kSameDepthLayerDifference;
}

// total 情報が消失してる
inline float4 MergeFlowCandidate(float4 base, float4 src) {
    const float2 flow = lerp(base.xy, src.xy, src.z * rcp(max(base.z + src.z, 1.0e-5)));
    const float score = max(base.z, src.z);
    const float packed = lerp(base.w, src.w, step(base.z, src.z));

    return float4(flow, score, packed);
}

// 発散ヤバそう
inline void InsertFlowCandidate(float4 candidate, float weight, inout float4 first, inout float4 second) {
    candidate.z *= weight;

    if (candidate.z <= 0.0) {
        return;
    }

    const float candidate_depth = UnpackCandidateDepth(candidate.w);

    if (first.z > 0.0 && IsSameFlowLayer(candidate.xy, candidate_depth, first.xy, UnpackCandidateDepth(first.w))) {
        first = MergeFlowCandidate(first, candidate);
        return;
    }

    if (second.z > 0.0 && IsSameFlowLayer(candidate.xy, candidate_depth, second.xy, UnpackCandidateDepth(second.w))) {
        second = MergeFlowCandidate(second, candidate);
        return;
    }

    if (candidate.z > first.z) {
        second = first;
        first = candidate;
    } else if (candidate.z > second.z) {
        second = candidate;
    }
}
