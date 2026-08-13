#include "flow_params.hlsli"

static const float kEpsilon = 1.0e-5;
static const uint kGroupSize = 8u;
static const int kMaximumRadius = 4;
static const uint kSharedSize = kGroupSize + 2u * uint(kMaximumRadius);
static const int kStride = int(kSharedSize);
static const float2 kMinimumQuality = float2(0.25, 0.65);
static const float4 kSigma = float4(1.0, 0.25, 1.5, 0.75);
static const float kMinimumConfidence = 0.02;

RWTexture2D<float4> output_flow_texture : register(u0);
Texture2D input_flow_texture : register(t0);

groupshared float4 flows[kSharedSize * kSharedSize];

inline int ToIndex(int2 loc) {
    return loc.y * kStride + loc.x;
}

inline float EvaluateWeight(float2 offset, float precision) {
    return exp(-dot(offset, offset) * precision);
}

inline float4 RegularizeDilated(float4 base_flow, int2 base_loc, uint2 size, int step) {
    const int2 upper = int2(size) - 1;
    const float step_float = float(step);
    const float spatial_precision = rcp(max(step_float * step_float, 1.0));
    float2 ref_flow = base_flow.xy;

    {
        float best = 0.0;

        for (int y = -1; y <= 1; ++y) {
            for (int x = -1; x <= 1; ++x) {
                if (x == 0 && y == 0) {
                    continue;
                }

                const int2 offset = int2(x, y) * step;
                const int2 loc = clamp(base_loc + offset, 0, upper);
                const float4 flow = input_flow_texture.Load(int3(loc, 0));
                const float spatial_weight = EvaluateWeight(float2(offset), spatial_precision);
                const float score = flow.z * flow.w * spatial_weight;

                if (score > best) {
                    ref_flow = flow.xy;
                    best = score;
                }
            }
        }
    }

    const float grid_sigma = float(grid_size) * lerp(kSigma.z, kSigma.w, base_flow.w * base_flow.w);
    const float motion_sigma = length(ref_flow) * kSigma.y;
    const float sigma = max(kSigma.x, max(grid_sigma, motion_sigma));
    const float precision = rcp(2.0 * sigma * sigma);
    float2 velocity = 0.0;
    float consistency = 0.0;
    float2 mass = 0.0;

    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            const int2 offset = int2(x, y) * step;
            const int2 loc = clamp(base_loc + offset, 0, upper);
            const float4 flow = input_flow_texture.Load(int3(loc, 0));
            const float weight = EvaluateWeight(float2(offset), spatial_precision);
            const float influence = flow.z * weight * EvaluateWeight(flow.xy - ref_flow, precision);

            velocity += flow.xy * influence;
            consistency += flow.w * influence;
            mass.x += influence;
            mass.y += weight;
        }
    }

    const float confidence = saturate(mass.x * rcp(max(mass.y, kEpsilon)));

    if (mass.x > kEpsilon && confidence >= kMinimumConfidence) {
        const float scale = rcp(mass.x);
        return float4(velocity * scale, confidence, saturate(consistency * scale));
    }

    return base_flow;
}

[numthreads(8, 8, 1)]
void main(FlowComputeInput input) {
    uint2 size;
    input_flow_texture.GetDimensions(size.x, size.y);

    if (propagation_step == 0) {
        const int2 origin = int2(input.gid.xy * kGroupSize) - kMaximumRadius;

        for (uint i = input.gidx; i < kSharedSize * kSharedSize; i += kGroupSize * kGroupSize) {
            const int3 loc = int3(clamp(origin + int2(i % kSharedSize, i / kSharedSize), 0, int2(size) - 1), 0);
            flows[i] = input_flow_texture.Load(loc);
        }
    }

    GroupMemoryBarrierWithGroupSync();

    if (propagation_step > 0) {
        if (any(input.dtid.xy >= size)) {
            return;
        }

        const int2 base_loc = int2(input.dtid.xy);
        const float4 base_flow = input_flow_texture.Load(int3(base_loc, 0));

        if (all(base_flow.zw >= kMinimumQuality)) {
            output_flow_texture[input.dtid.xy] = base_flow;
            return;
        }

        output_flow_texture[input.dtid.xy] = RegularizeDilated(
            base_flow,
            base_loc,
            size,
            propagation_step);
        return;
    }

    if (any(input.dtid.xy >= size)) {
        return;
    }

    const int base_index = ToIndex(int2(input.gtid.xy) + kMaximumRadius);
    const float4 base_flow = flows[base_index];

    if (all(base_flow.zw >= kMinimumQuality)) {
        output_flow_texture[input.dtid.xy] = base_flow;
        return;
    }

    const int radius = clamp(grid_size, 2, kMaximumRadius);
    const float spatial_precision = rcp(max(float(radius * radius), 1.0));
    float2 ref_flow = base_flow.xy;

    {
        float best = 0.0;

        for (int y = -radius; y <= radius; ++y) {
            for (int x = -radius; x <= radius; ++x) {
                if (x == 0 && y == 0) {
                    continue;
                }

                const int idx = base_index + ToIndex(int2(x, y));
                const float4 flow = flows[idx];
                const float spatial_weight = EvaluateWeight(float2(x, y), spatial_precision);
                const float score = flow.z * flow.w * spatial_weight;

                if (score > best) {
                    ref_flow = flow.xy;
                    best = score;
                }
            }
        }
    }

    const float grid_sigma = float(grid_size) * lerp(kSigma.z, kSigma.w, base_flow.w * base_flow.w);
    const float motion_sigma = length(ref_flow) * kSigma.y;
    const float sigma = max(kSigma.x, max(grid_sigma, motion_sigma));
    const float precision = rcp(2.0 * sigma * sigma);
    float2 velocity = 0.0;
    float consistency = 0.0;
    float2 mass = 0.0;

    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            const int idx = base_index + ToIndex(int2(x, y));
            const float4 flow = flows[idx];
            const float weight = EvaluateWeight(float2(x, y), spatial_precision);
            const float influence = flow.z * weight * EvaluateWeight(flow.xy - ref_flow, precision);

            velocity += flow.xy * influence;
            consistency += flow.w * influence;
            mass.x += influence;
            mass.y += weight;
        }
    }

    const float confidence = saturate(mass.x * rcp(max(mass.y, kEpsilon)));

    if (mass.x > kEpsilon && confidence >= kMinimumConfidence) {
        const float scale = rcp(mass.x);
        output_flow_texture[input.dtid.xy] = float4(velocity * scale, confidence, saturate(consistency * scale));
    } else {
        output_flow_texture[input.dtid.xy] = base_flow;
    }
}
