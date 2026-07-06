struct Transform {
    float4 position;
    float4 scale;
    float2 rotation;
    float2 padding;
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

inline column_major float2x2 RotationMatrix2D(float angle) {
    const float s = sin(angle), c = cos(angle);
    return float2x2(c, s, -s, c);
}

float4 main(float4 pos : SV_Position) : SV_Target {
    uint2 size;
    transform_buffer.GetDimensions(size.x, size.y);

    pos.xy += kOrigin.xy;

    float4 wet = Sample(pos.xy);
    const float4 dry = wet * kMix.x;

    pos.xy = mul(kTransform, float3(pos.xy - kPivot.xy, 1.0)).xy;

    {
        const float step = kAmount * rcp(float(kSamples - 1));

        for (int i = 1; i < kSamples; ++i) {
            const float t = step * float(i);

            float2 p = pos.xy;

            for (uint j = 0u; j < size.x; ++j) {
                const Transform xform = transform_buffer[j];

                const float2 trans = lerp(xform.position.xy, xform.position.zw, t);
                const float2x2 rot = RotationMatrix2D(lerp(xform.rotation.x, xform.rotation.y, t));
                const float2 scale = lerp(rcp(xform.scale.xy), rcp(xform.scale.zw), t);

                p = mul(rot, p - trans) * scale;
            }

            wet += Sample(p + lerp(kPivot.xy, kPivot.zw, t));
        }
    }

    wet *= rcp(float(kSamples)) * kMix.y;

    return mad(1.0 - dry.a, wet, dry);
}
