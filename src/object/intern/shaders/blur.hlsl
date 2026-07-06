struct Transform {
    float4 row0;
    float4 row1;
};

Texture2D input_texture : register(t0);
StructuredBuffer<Transform> transform_buffer : register(t1);
SamplerState linear_sampler : register(s0);
cbuffer params : register(b0) {
    column_major float3x3 kTransform;
    float4 kPivot;
    float2 kOrigin;
    float2 kTexel;
    float kAmount;
    int kSamples;
    float2 kMix;
}

inline float4 Sample(float2 pos) {
    return input_texture.Sample(linear_sampler, (pos + 0.5) * kTexel);
}

float4 main(float4 pos : SV_Position) : SV_Target {
    pos.xy += kOrigin.xy;

    float4 wet = Sample(pos.xy);
    const float4 dry = wet * kMix.x;

    pos.xy = mul(kTransform, float3(pos.xy - kPivot.xy, 1.0)).xy;

    for (int i = 1; i < kSamples; ++i) {
        const Transform xform = transform_buffer[i - 1];
        const float3 p = float3(pos.xy, 1.0);

        wet += Sample(float2(dot(xform.row0.xyz, p), dot(xform.row1.xyz, p)));
    }

    wet *= rcp(float(kSamples)) * kMix.y;

    return mad(1.0 - dry.a, wet, dry);
}
