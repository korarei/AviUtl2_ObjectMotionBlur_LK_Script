struct Affine2D {
    float3 row0;
    float3 row1;
};

Texture2D source_texture : register(t0);
StructuredBuffer<Affine2D> subframe_transforms : register(t1);
SamplerState linear_sampler : register(s0);
cbuffer params : register(b0) {
    Affine2D transform;
    float2 origin;
    float2 texel;
    int samples;
    int blend_mode;
    float falloff;
    float amp;
}

inline float4 Sample(float2 pos) {
    return source_texture.Sample(linear_sampler, (pos + 0.5) * texel);
}

float4 main(float4 pos : SV_Position) : SV_Target {
    pos.xyz = float3(pos.xy + origin, 1.0);
    pos.xy = float2(dot(transform.row0, pos.xyz), dot(transform.row1, pos.xyz));

    Affine2D xform = subframe_transforms[samples - 1];
    float weight = 1.0 - falloff;
    float norm = weight;
    float4 blended = Sample(float2(dot(xform.row0, pos.xyz), dot(xform.row1, pos.xyz)));

    switch (blend_mode) {
        default:
            for (int i = 1; i < samples; ++i) {
                xform = subframe_transforms[samples - 1 - i];
                weight *= amp;
                blended += Sample(float2(dot(xform.row0, pos.xyz), dot(xform.row1, pos.xyz))) * weight;
                norm += weight;
            }

            return blended *= rcp(norm);
    }
}
