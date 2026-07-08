struct SampleTransform {
    float3 row0;
    float3 row1;
};

Texture2D source_texture : register(t0);
StructuredBuffer<SampleTransform> sample_transform_buffer : register(t1);
SamplerState linear_sampler : register(s0);
cbuffer params : register(b0) {
    float3 kBaseTransformRow0;
    float3 kBaseTransformRow1;
    float2 kPivot;
    float2 kOrigin;
    float2 kTexel;
    float kAmount;
    int kSamples;
    float2 kMix;
}

inline float4 Sample(float2 pos) {
    return source_texture.Sample(linear_sampler, (pos + 0.5) * kTexel);
}

float4 main(float4 pos : SV_Position) : SV_Target {
    pos.xyz = float3(pos.xy + kOrigin, 1.0);

    float4 wet = Sample(pos.xy);
    const float4 dry = wet * kMix.x;

    pos.xy -= kPivot;
    pos.xy = float2(dot(kBaseTransformRow0, pos.xyz), dot(kBaseTransformRow1, pos.xyz));

    for (int i = 1; i < kSamples; ++i) {
        const SampleTransform xform = sample_transform_buffer[i - 1];
        wet += Sample(float2(dot(xform.row0, pos.xyz), dot(xform.row1, pos.xyz)));
    }

    wet *= rcp(float(kSamples)) * kMix.y;

    return mad(1.0 - dry.a, wet, dry);
}
