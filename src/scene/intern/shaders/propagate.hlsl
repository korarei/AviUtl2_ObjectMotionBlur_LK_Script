struct PSOutput {
    float4 first : SV_Target0;
    float4 second : SV_Target1;
};

static const float kEpsilon = 1.0e-5;
static const float kDepthTolerance = 0.03;
static const float kMinimumSpeedSq = 0.04;
static const float kStrokeRadius = 1.5;
static const float kStrokeFeather = 0.5;
static const float kMotionDifferenceSq = 2.25;
static const float kInvalidDistanceSq = 3.402823e+38;
static const float3 kBT709 = float3(0.2126, 0.7152, 0.0722);

Texture2D flow_maps[2] : register(t0);
Texture2D depth_map    : register(t2);
SamplerState linear_sampler : register(s0);
cbuffer params : register(b0) {
    uint2 resolution;
    int stride;
};

inline float SampleDepth(float2 uv) {
    return saturate(dot(depth_map.Sample(linear_sampler, uv).rgb, kBT709));
}

inline float2 UnpackOrigin(float packed) {
    const uint origin = asuint(packed);
    return float2(min(uint2(origin & 0xffffu, origin >> 16u), resolution - 1u)) + 0.5;
}

inline float IsInside(int2 loc) {
    const float2 p = float2(loc);
    const float2 inside = step(0.0, p) * step(p, float2(resolution - 1u));
    return inside.x * inside.y;
}

inline bool IsSameOrigin(float lhs, float rhs) {
    return asuint(lhs) == asuint(rhs);
}

inline bool IsDifferentMotion(float2 lhs, float2 rhs) {
    const float2 delta = lhs - rhs;
    return dot(delta, delta) >= kMotionDifferenceSq;
}

inline float EvaluateStrokeDistanceSq(float2 origin, float2 velocity, float2 target) {
    const float2 seg = target - origin;
    const float t = saturate(dot(seg, velocity) * rcp(max(dot(velocity, velocity), kEpsilon)));
    const float2 perp = mad(-velocity, t, seg);

    return dot(perp, perp);
}

inline float EvaluateStrokeCoverage(float distance_sq) {
    const float stroke_distance = sqrt(distance_sq);
    return 1.0 - smoothstep(kStrokeRadius - kStrokeFeather, kStrokeRadius + kStrokeFeather, stroke_distance);
}

inline void UpdateSlots(inout float4 first, inout float4 second, inout float2 dists, float4 cand, float valid,
                        float2 target) {
    if (valid < 0.5) {
        return;
    }

    const float cand_dist = EvaluateStrokeDistanceSq(UnpackOrigin(cand.w), cand.xy, target);

    if (IsSameOrigin(cand.w, first.w)) {
        return;
    }

    if (cand_dist < dists.x) {
        const float4 first_prev = first;
        const float dist_prev = dists.x;

        first = cand;
        dists.x = cand_dist;

        if (dist_prev < kInvalidDistanceSq && IsDifferentMotion(first.xy, first_prev.xy)) {
            second = first_prev;
            dists.y = dist_prev;
        } else if (dists.y < kInvalidDistanceSq && !IsDifferentMotion(first.xy, second.xy)) {
            second = first;
            dists.y = kInvalidDistanceSq;
        }
    } else if (IsDifferentMotion(first.xy, cand.xy) && !IsSameOrigin(cand.w, second.w) && cand_dist < dists.y) {
        second = cand;
        dists.y = cand_dist;
    }
}

PSOutput main(float4 pos : SV_Position) {
    const int2 base_loc = int2(pos.xy);
    const float base_depth = SampleDepth(pos.xy * rcp(float2(resolution)));

    float4 first = flow_maps[0].Load(int3(base_loc, 0));
    float4 second = first;

    const float first_depth_valid = step(base_depth - kDepthTolerance, first.z);
    const float first_speed_valid = step(kMinimumSpeedSq, dot(first.xy, first.xy));

    float2 dists = float2(kInvalidDistanceSq, kInvalidDistanceSq);

    if (first_depth_valid * first_speed_valid >= 0.5) {
        dists.x = EvaluateStrokeDistanceSq(UnpackOrigin(first.w), first.xy, pos.xy);
    }

    const float4 base_second = flow_maps[1].Load(int3(base_loc, 0));
    const float second_depth_valid = step(base_depth - kDepthTolerance, base_second.z);
    const float second_speed_valid = step(kMinimumSpeedSq, dot(base_second.xy, base_second.xy));
    UpdateSlots(first, second, dists, base_second, second_depth_valid * second_speed_valid, pos.xy);

    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            const int2 sample_loc = base_loc + int2(x, y) * stride;
            const int2 clamped_loc = clamp(sample_loc, int2(0, 0), int2(resolution) - 1);
            const float location_valid = IsInside(sample_loc) * step(0.5, float(abs(x) + abs(y)));

            [unroll]
            for (int i = 0; i < 2; ++i) {
                const float4 cand = flow_maps[i].Load(int3(clamped_loc, 0));

                const float depth_valid = step(base_depth - kDepthTolerance, cand.z);
                const float speed_valid = step(kMinimumSpeedSq, dot(cand.xy, cand.xy));

                UpdateSlots(first, second, dists, cand, location_valid * depth_valid * speed_valid, pos.xy);
            }
        }
    }

    if (dists.y == kInvalidDistanceSq) {
        second = first;
    }

    if (stride == 1) {
        const float2 coverage = float2(EvaluateStrokeCoverage(dists.x), EvaluateStrokeCoverage(dists.y));

        first.xy *= step(kEpsilon, coverage.x);
        first.z = coverage.x;
        second.xy *= step(kEpsilon, coverage.y);
        second.z = coverage.y;
    }

    PSOutput output;
    output.first = first;
    output.second = second;

    return output;
}
