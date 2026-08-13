#include "layer_candidate.hlsli"

struct ColorDepthSample {
    float4 color;
    float depth;
};

struct MotionPath {
    float2 start;
    float2 end;
    float support;
    float depth;
};

struct TileCandidates {
    float4 first;
    float4 second;
};

struct PathEndpoint {
    float2 position;
    float support;
};

static const float kEpsilon = 1.0e-5;
static const uint kTileSize = 16u;
static const uint kCandidateRadius = 1u;
static const float kCandidateTilePrecision = 2.0;
static const float kCandidateFootprintPrecision = 1.0 / 512.0;
static const float kPathErrorPrecision = 0.25;
static const int kPathIterations = 2;

Texture2D source_texture : register(t0);
Texture2D regularized_flow_texture : register(t1);
Texture2D classified_flow_texture : register(t2);
Texture2D motion_candidate_texture[2] : register(t3);
SamplerState linear_sampler : register(s0);
cbuffer params : register(b0) {
    float2 texel;
    float2 shutter;
    float2 mix;
    float falloff;
    int sample_limit;
}

inline ColorDepthSample SampleColorDepth(float2 pos) {
    const float2 uv = pos * texel;

    ColorDepthSample smp;
    smp.color = source_texture.SampleLevel(linear_sampler, uv, 0.0);
    smp.depth = classified_flow_texture.SampleLevel(linear_sampler, uv, 0.0).w;

    return smp;
}

inline float4 SampleFlow(float2 pos) {
    return regularized_flow_texture.Sample(linear_sampler, pos * texel);
}

inline TileCandidates LoadTileCandidates(int2 pos, int2 upper) {
    const int3 loc = int3(clamp(pos, 0, upper), 0);

    TileCandidates candidates;
    candidates.first = motion_candidate_texture[0].Load(loc);
    candidates.second = motion_candidate_texture[1].Load(loc);

    return candidates;
}

inline float EvaluateCandidateWeight(float2 pos, int2 tile_loc, float4 candidate) {
    const float2 tile_center = (float2(tile_loc) + 0.5) * float(kTileSize);
    const float2 tile_offset = (pos - tile_center) * rcp(float(kTileSize));
    const float tile_weight = exp(-dot(tile_offset, tile_offset) * kCandidateTilePrecision);

    const float2 origin = UnpackOrigin(candidate.w) * float(kTileSize);
    const float4 sweep = float4(
        candidate.xy * shutter.x,
        candidate.xy * shutter.y);
    const float2 sweep_min = min(sweep.xy, sweep.zw);
    const float2 sweep_max = max(sweep.xy, sweep.zw);
    const float2 candidate_min = origin + sweep_min;
    const float2 candidate_max = origin + float(kTileSize) + sweep_max;
    const float2 outside_distance = max(
        max(candidate_min - pos, pos - candidate_max),
        0.0);
    const float footprint_weight =
        exp(-dot(outside_distance, outside_distance) * kCandidateFootprintPrecision);

    return tile_weight * footprint_weight;
}

inline PathEndpoint ResolvePathEndpoint(float2 pos, float t, float2 candidate_flow, float candidate_score) {
    PathEndpoint endpoint;
    endpoint.position = pos;
    endpoint.support = 0.0;

    if (candidate_score <= 0.0) {
        return endpoint;
    }

    float2 p = pos - candidate_flow * t;

    [unroll]
    for (int i = 0; i < kPathIterations; ++i) {
        const float4 flow = SampleFlow(p);
        p = lerp(p, pos - flow.xy * t, flow.z * flow.w);
    }

    const float4 flow = SampleFlow(p);
    const float2 error = p + lerp(candidate_flow, flow.xy, flow.z * flow.w) * t - pos;
    const float weight = exp(-dot(error, error) * kPathErrorPrecision);

    endpoint.position = p;
    endpoint.support = candidate_score * weight;

    return endpoint;
}

inline MotionPath ResolveCandidatePath(float2 pos, float4 candidate) {
    const float score = saturate(candidate.z);
    const float depth = UnpackCandidateDepth(candidate.w);
    const PathEndpoint start =
        ResolvePathEndpoint(pos, shutter.x, candidate.xy, score);
    const PathEndpoint end =
        ResolvePathEndpoint(pos, shutter.y, candidate.xy, score);

    MotionPath path;
    path.start = start.position;
    path.end = end.position;
    path.support = min(start.support, end.support);
    path.depth = depth;

    return path;
}

inline MotionPath ResolveMotionPath(float2 pos) {
    const int2 base_tile_loc = int2(uint2(pos) / kTileSize);
    const int2 tile_upper = int2(ceil(rcp(texel * float(kTileSize)))) - 1;

    TileCandidates candidates;
    candidates.first = 0.0;
    candidates.second = 0.0;

    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            const int2 tile_loc = base_tile_loc + int2(x, y);
            const TileCandidates src = LoadTileCandidates(tile_loc, tile_upper);

            InsertFlowCandidate(
                src.first,
                EvaluateCandidateWeight(pos, tile_loc, src.first),
                candidates.first,
                candidates.second);
            InsertFlowCandidate(
                src.second,
                EvaluateCandidateWeight(pos, tile_loc, src.second),
                candidates.first,
                candidates.second);
        }
    }

    const float4 candidate = lerp(
        candidates.first,
        candidates.second,
        step(candidates.first.z, candidates.second.z));

    return ResolveCandidatePath(pos, candidate);
}

float4 main(float4 pos : SV_Position) : SV_Target {
    const MotionPath path = ResolveMotionPath(pos.xy);

    float4 wet = 0.0;

    if (path.support > 0.0) {
        const int required_samples =
            max(1, int(ceil(length(path.end - path.start))) + 1);
        const int samples = max(1, min(sample_limit, required_samples));
        const float step = rcp(float(max(samples - 1, 1)));
        const float decay = pow(max(1.0 - falloff, kEpsilon), step);

        float shutter_weight = 1.0;
        float norm = 0.0;

        for (int i = 0; i < samples; ++i) {
            const ColorDepthSample smp = SampleColorDepth(
                lerp(path.start, path.end, float(i) * step));
            const float depth_weight = EvaluateDepthWeight(path.depth, smp.depth);
            const float weight = shutter_weight * depth_weight;

            wet += smp.color * weight;
            norm += weight;
            shutter_weight *= decay;
        }

        wet *= rcp(max(norm, kEpsilon));
    }

    const float4 src = source_texture.Load(int3(pos.xy, 0));
    const float4 dry = src * mix.x;

    return mad(1.0 - dry.a, mad(1.0 - wet.a, src, wet) * mix.y, dry);
}
