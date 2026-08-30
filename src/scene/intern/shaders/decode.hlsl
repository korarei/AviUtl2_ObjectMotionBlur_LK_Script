struct SampleCoords {
    float2 position[2][2];
    float2 t;
};

static const float kEpsilon = 1.0e-5;
static const float kAlpha1 = 0.01;
static const float kAlpha2 = 0.5;

Texture2D<int2> flow_textures[2] : register(t0);
Texture2D<uint> cost_textures[2] : register(t2);
cbuffer params : register(b0) {
    uint2 resolution;
    uint grid_size;
    float flow_scale;
}

inline SampleCoords GetSampleCoords(float2 pos, uint2 size) {
    const int2 upper = int2(size) - 1;

    pos = (pos + 0.5) * rcp(float(grid_size)) - 0.5;
    const int2 base = int2(floor(pos));

    SampleCoords coords;
    coords.position[0][0] = clamp(base, 0, upper);
    coords.position[1][0] = clamp(base + int2(1, 0), 0, upper);
    coords.position[0][1] = clamp(base + int2(0, 1), 0, upper);
    coords.position[1][1] = clamp(base + int2(1, 1), 0, upper);
    coords.t = pos - float2(base);

    return coords;
}

inline float3 LoadForward(int2 loc) {
    const int3 l = int3(loc, 0);
    const float2 flow = flow_textures[0].Load(l);
    const float cost = cost_textures[0].Load(l);
    return float3(flow * rcp(32.0) * flow_scale, cost * rcp(255.0));
}

inline float3 LoadBackward(int2 loc) {
    const int3 l = int3(loc, 0);
    const float2 flow = flow_textures[1].Load(l);
    const float cost = cost_textures[1].Load(l);
    return float3(flow * rcp(32.0) * flow_scale, cost * rcp(255.0));
}

float3 SampleForward(float2 pos, uint2 size) {
    const SampleCoords coords = GetSampleCoords(pos, size);

    const float3 f00 = LoadForward(coords.position[0][0]);
    const float3 f10 = LoadForward(coords.position[1][0]);
    const float3 f01 = LoadForward(coords.position[0][1]);
    const float3 f11 = LoadForward(coords.position[1][1]);

    return lerp(lerp(f00, f10, coords.t.x), lerp(f01, f11, coords.t.x), coords.t.y);
}

float3 SampleBackward(float2 pos, uint2 size) {
    const SampleCoords coords = GetSampleCoords(pos, size);

    const float3 f00 = LoadBackward(coords.position[0][0]);
    const float3 f10 = LoadBackward(coords.position[1][0]);
    const float3 f01 = LoadBackward(coords.position[0][1]);
    const float3 f11 = LoadBackward(coords.position[1][1]);

    return lerp(lerp(f00, f10, coords.t.x), lerp(f01, f11, coords.t.x), coords.t.y);
}

inline float InBoundsMask(float2 pos) {
    const float2 lower = step(0.0, pos);
    const float2 upper = step(pos, float2(resolution - 1u));
    return lower.x * lower.y * upper.x * upper.y;
}

inline float EvaluateAgreement(float2 fwd, float2 bwd) {
    const float2 residual = fwd + bwd;
    return exp(-dot(residual, residual) * rcp(max(kAlpha1 * (dot(fwd, fwd) + dot(bwd, bwd)) + kAlpha2, kEpsilon)));
}

float4 main(float4 pos : SV_Position) : SV_Target {
    uint2 size;
    flow_textures[0].GetDimensions(size.x, size.y);

    pos.xy -= 0.5;
    const float3 fwd = SampleForward(pos.xy, size);
    const float2 dst = pos.xy + fwd.xy;
    const float3 bwd = SampleBackward(dst, size);
    const float mask = InBoundsMask(dst);

    const float agreement = EvaluateAgreement(fwd.xy, bwd.xy);

    const float2 confidence = saturate(1.0 - float2(fwd.z, bwd.z));
    const float consistency = agreement * mask;

    return float4(fwd.xy, lerp(confidence.x, sqrt(confidence.x * confidence.y) * agreement, mask), consistency);
}
