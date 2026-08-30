struct CSInput {
    uint3 gid : SV_GroupID;
    uint3 gtid : SV_GroupThreadID;
    uint gidx : SV_GroupIndex;
    uint3 dtid : SV_DispatchThreadID;
};

#define THREADS_X 16
#define THREADS_Y 16
#define THREAD_COUNT (THREADS_X * THREADS_Y)

#define RADIUS 4
#define TILE_W (THREADS_X + 2 * RADIUS)
#define TILE_H (THREADS_Y + 2 * RADIUS)
#define TILE_SIZE (TILE_W * TILE_H)

static const float kSigmaSpatial = 2.5;
static const float kSigmaVelocity = 8.0;
static const float kSigmaQuality = 0.3;

static const float kSpatialPrec = 1.0 / (2.0 * kSigmaSpatial * kSigmaSpatial);
static const float kVelocityPrec = 1.0 / (2.0 * kSigmaVelocity * kSigmaVelocity);
static const float kQualityPrec = 1.0 / (2.0 * kSigmaQuality * kSigmaQuality);

static const float2 kConfidenceRange = float2(0.1, 0.5);
static const float3 kBT709 = float3(0.2126, 0.7152, 0.0722);

RWTexture2D<float4> output_first_flow : register(u0);
RWTexture2D<float4> output_second_flow : register(u1);
Texture2D input_flow_map : register(t0);
Texture2D depth_map : register(t1);
SamplerState linear_sampler : register(s0);
cbuffer params : register(b0) {
    uint2 resolution;
};

groupshared float4 cached_flow[TILE_H][TILE_W];

inline uint PackOrigin(uint2 origin) {
    return origin.x | (origin.y << 16u);
}

inline float SampleDepth(float2 uv) {
    return saturate(dot(depth_map.SampleLevel(linear_sampler, uv, 0.0).rgb, kBT709));
}

[numthreads(THREADS_X, THREADS_Y, 1)]
void main(CSInput input) {
    const int2 origin = int2(input.gid.xy * uint2(THREADS_X, THREADS_Y)) - RADIUS;
    const int2 upper = int2(resolution) - 1;

    [unroll]
    for (uint i = input.gidx; i < TILE_SIZE; i += THREAD_COUNT) {
        const uint y = i / TILE_W;
        const uint x = i % TILE_W;
        cached_flow[y][x] = input_flow_map.Load(int3(clamp(origin + int2(x, y), 0, upper), 0));
    }

    GroupMemoryBarrierWithGroupSync();

    if (any(input.dtid.xy >= resolution)) {
        return;
    }

    const uint2 base_pos = input.gtid.xy + RADIUS;
    const float4 base_flow = cached_flow[base_pos.y][base_pos.x];

    float4 flow = base_flow;
    float weight = 1.0;

    [unroll]
    for (int y = -RADIUS; y <= RADIUS; ++y) {
        [unroll]
        for (int x = -RADIUS; x <= RADIUS; ++x) {
            if (x == 0 && y == 0) {
                continue;
            }

            const float quadrance = float(x * x + y * y);

            if (quadrance > float(RADIUS * RADIUS)) {
                continue;
            }

            const float spatial_w = exp(-quadrance * kSpatialPrec);
            const float4 nbr_flow = cached_flow[base_pos.y + y][base_pos.x + x];

            const float2 dv = nbr_flow.xy - base_flow.xy;
            const float2 dq = nbr_flow.zw - base_flow.zw;

            const float w = spatial_w * exp(-(dot(dv, dv) * kVelocityPrec + dot(dq, dq) * kQualityPrec));

            flow += nbr_flow * w;
            weight += w;
        }
    }

    flow *= rcp(weight);

    const float confidence = smoothstep(kConfidenceRange.x, kConfidenceRange.y, flow.z);
    const float depth = SampleDepth((float2(input.dtid.xy) + 0.5) * rcp(float2(resolution)));

    flow = float4(flow.xy * confidence, depth, asfloat(PackOrigin(input.dtid.xy)));

    output_first_flow[input.dtid.xy] = flow;
    output_second_flow[input.dtid.xy] = flow;
}
