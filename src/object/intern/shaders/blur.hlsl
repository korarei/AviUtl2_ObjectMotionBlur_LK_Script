struct AffineMatrix {
    float3 row0;
    float3 row1;
};

Texture2D source_texture : register(t0);
StructuredBuffer<AffineMatrix> subframe_transforms : register(t1);
SamplerState linear_sampler : register(s0);
cbuffer params : register(b0) {
    AffineMatrix base_transform;
    float2 pivot;
    float2 origin;
    float amount;
    int samples;
    float2 mix;
}

inline float4 Sample(float2 pos, float2 texel) {
    return source_texture.Sample(linear_sampler, (pos + 0.5) * texel);
}

float4 main(float4 pos : SV_Position) : SV_Target {
    float2 size;
    source_texture.GetDimensions(size.x, size.y);
    const float2 texel = rcp(size);

    pos.xyz = float3(pos.xy + origin, 1.0);

    float4 wet = Sample(pos.xy, texel);
    const float4 dry = wet * mix.x;

    pos.xy -= pivot;
    pos.xy = float2(dot(base_transform.row0, pos.xyz), dot(base_transform.row1, pos.xyz));

    for (int i = 1; i < samples; ++i) {
        const AffineMatrix xform = subframe_transforms[i - 1];
        wet += Sample(float2(dot(xform.row0, pos.xyz), dot(xform.row1, pos.xyz)), texel);
    }

    wet *= rcp(float(samples)) * mix.y;

    return mad(1.0 - dry.a, wet, dry);
}
