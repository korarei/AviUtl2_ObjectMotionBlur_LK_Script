#pragma pack_matrix(row_major)

typedef float3 Float2x3[2];

Texture2D source_texture : register(t0);
StructuredBuffer<Float2x3> subframe_transforms : register(t1);
SamplerState linear_sampler : register(s0);
cbuffer params : register(b0) {
    float2x3 transform;
    float2 origin;
    float2 texel;
    float2 mix;
    float decay;
    int samples;
}

inline float4 Sample(float2 pos) {
    return source_texture.Sample(linear_sampler, pos * texel);
}

float4 main(float4 pos : SV_Position) : SV_Target {
    pos.xyz = float3(pos.xy + origin, 1.0);
    pos.xy = float2(dot(transform[0], pos.xyz), dot(transform[1], pos.xyz));

    Float2x3 xform = subframe_transforms[0];
    float weight = 1.0;
    float norm = weight;
    float4 wet = Sample(float2(dot(xform[0], pos.xyz), dot(xform[1], pos.xyz)));
    const float4 dry = wet * mix.x;

    for (int i = 1; i < samples; ++i) {
        xform = subframe_transforms[i];
        weight *= decay;
        wet += Sample(float2(dot(xform[0], pos.xyz), dot(xform[1], pos.xyz))) * weight;
        norm += weight;
    }

    return mad(1.0 - dry.a, wet * rcp(norm) * mix.y, dry);
}
