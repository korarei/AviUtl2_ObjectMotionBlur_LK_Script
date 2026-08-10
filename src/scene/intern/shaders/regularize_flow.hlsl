#include "weight.hlsli"

struct CSInput {
    uint3 gid : SV_GroupID;
    uint3 gtid : SV_GroupThreadID;
    uint gidx : SV_GroupIndex;
    uint3 dtid : SV_DispatchThreadID;
};

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
Texture2D source_texture : register(t1);
cbuffer params : register(b0) {
    uint2 resolution;
    uint grid_size;
    float flow_scale;
}

groupshared float4 flows[kSharedSize * kSharedSize];
groupshared float4 colors[kSharedSize * kSharedSize];

inline int ToIndex(int2 loc) {
    return loc.y * kStride + loc.x;
}

inline float EvaluateWeight(float2 offset, float precision) {
    return exp(-dot(offset, offset) * precision);
}

[numthreads(8, 8, 1)]
void main(CSInput input) {
    {
        uint2 size;
        input_flow_texture.GetDimensions(size.x, size.y);

        const int2 origin = int2(input.gid.xy * kGroupSize) - kMaximumRadius;

        for (uint i = input.gidx; i < kSharedSize * kSharedSize; i += kGroupSize * kGroupSize) {
            const int3 loc = int3(clamp(origin + int2(i % kSharedSize, i / kSharedSize), 0, int2(size) - 1), 0);
            flows[i] = input_flow_texture.Load(loc);
            colors[i] = source_texture.Load(loc);
        }

        GroupMemoryBarrierWithGroupSync();

        if (any(input.dtid.xy >= size)) {
            return;
        }
    }

    const int base_index = ToIndex(int2(input.gtid.xy) + kMaximumRadius);
    const float4 base_flow = flows[base_index];

    if (all(base_flow.zw >= kMinimumQuality)) {
        output_flow_texture[input.dtid.xy] = base_flow;
        return;
    }

    const int radius = clamp(grid_size, 2, kMaximumRadius);
    const float spatial_precision = rcp(max(float(radius * radius), 1.0));
    const float4 base_color = colors[base_index];
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
                const float4 color = colors[idx];

                const float spatial_weight = EvaluateWeight(float2(x, y), spatial_precision);
                const float guide_weight = EvaluateGuideWeight(base_color, color);
                const float score = flow.z * flow.w * spatial_weight * guide_weight;

                if (score > best) {
                    ref_flow = flow.xy;
                    best = score;
                }
            }
        }
    }

    float2 velocity = 0.0;
    float consistency = 0.0;
    float2 mass = 0.0;

    {
        const float grid_sigma = float(grid_size) * lerp(kSigma.z, kSigma.w, base_flow.w * base_flow.w);
        const float motion_sigma = length(ref_flow) * kSigma.y;
        const float sigma = max(kSigma.x, max(grid_sigma, motion_sigma));
        const float precision = rcp(2.0 * sigma * sigma);

        for (int y = -radius; y <= radius; ++y) {
            for (int x = -radius; x <= radius; ++x) {
                const int idx = base_index + ToIndex(int2(x, y));
                const float4 flow = flows[idx];
                const float4 color = colors[idx];

                const float spatial_weight = EvaluateWeight(float2(x, y), spatial_precision);
                const float guide_weight = EvaluateGuideWeight(base_color, color);
                const float weight = spatial_weight * guide_weight;
                const float influence = flow.z * weight * EvaluateWeight(flow.xy - ref_flow, precision);

                velocity += flow.xy * influence;
                consistency += flow.w * influence;
                mass.x += influence;
                mass.y += weight;
            }
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
