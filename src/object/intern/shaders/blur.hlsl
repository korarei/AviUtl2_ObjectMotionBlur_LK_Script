#pragma pack_matrix(row_major)

typedef float3 Float2x3[2];

static const float kEpsilon = 1.0e-5;
static const float3 kBT709 = float3(0.2126, 0.7152, 0.0722);

Texture2D target_image : register(t0);
Texture2D map : register(t1);
StructuredBuffer<Float2x3> trajectory : register(t2);
SamplerState linear_sampler : register(s0);
cbuffer params : register(b0) {
    float2x3 transform;
    float2 origin;
    float2 texel;
    float2 mix;
    float2 falloff;
    int samples;
    float map_inset;
    float alpha_mode;
    float seed;
}

/*
The following function is a modified version of pcg4d function
Original implementation by Mark Jarzynski & Marc Olano
https://github.com/markjarzynski/PCG3D/blob/master/LICENSE
*/

uint4 pcg4d(uint4 v) {
    v = v * 1664525u + 1013904223u;

    v.x += v.y * v.w;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v.w += v.y * v.z;

    v = v ^ v >> 16u;

    v.x += v.y * v.w;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v.w += v.y * v.z;

    return v;
}

inline float hash(float2 p, float2 s) {
    return dot(pcg4d(uint4(p, s)), 1u) / 4294967295.0;
}

inline float4 Sample(float2 pos) {
    return target_image.Sample(linear_sampler, pos * texel);
}

inline float4 Tint(float4 color, float t) {
    const float y = saturate(dot(color.rgb, kBT709) * rcp(max(color.a, kEpsilon)));
    const float4 src = map.Sample(linear_sampler, float2(lerp(map_inset, 1.0 - map_inset, y), t));

    color.rgb = mad(color.rgb, 1.0 - src.a, src.rgb * color.a);

    return color;
}

float4 main(float4 pos : SV_Position) : SV_Target {
    const float dither = hash(pos.xy, seed.xx);

    pos.xyz = float3(pos.xy + origin, 1.0);

    const float4 dry = Sample(pos.xy) * mix.x;

    pos.xy = float2(dot(transform[0], pos.xyz), dot(transform[1], pos.xyz));

    float4 wet = float4(0.0, 0.0, 0.0, 0.0);
    float norm = 0.0;

    for (int i = 0; i < samples; ++i) {
        const Float2x3 node = trajectory[i];
        const float t = (float(i) + 0.5) * rcp(float(samples));
        const float weight = smoothstep(0.0, falloff.x, t) * smoothstep(0.0, falloff.y, 1.0 - t);

        wet += Tint(Sample(float2(dot(node[0], pos.xyz), dot(node[1], pos.xyz))), t) * weight;
        norm += weight;
    }

    wet *= rcp(norm);

    const float4 dissolved = wet * rcp(max(wet.a, kEpsilon)) * step(dither + kEpsilon, wet.a);

    return mad(1.0 - dry.a, lerp(wet, dissolved, alpha_mode) * mix.y, dry);
}
