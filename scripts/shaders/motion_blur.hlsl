Texture2D src : register(t0);
SamplerState smp : register(s0);
cbuffer params : register(b0) {
    column_major float3x3 xform_base;
    column_major float3x3 scale;
    float3 drift;
    float2 res;
    float2 pivot;
    float n;
    float mix;
};

struct PS_Input {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD;
};

inline float2 to_uv(float3 pos, float2 texel) {
    return (pos.xy + pivot) * texel;
}

float4 motion_blur(PS_Input input) : SV_Target {
    const float2 texel = rcp(res);
    const uint count = uint(n);

    float3 pos = float3(mad(input.uv, res, -pivot), 1.0);
    float3 d = drift;
    float3x3 scl = scale;
    float3x3 xform = xform_base;
    float3x3 pose = xform_base;
    pose._13_23_33 = float3(0.0, 0.0, 1.0);

    float4 col = src.Load(int3(input.pos.xy, 0));
    for (uint i = 1; i <= count; ++i) {
        pos = mul(xform, pos);
        float2 uv = to_uv(mul(scl, pos) + d, texel);
        col += src.Sample(smp, uv);

        d += drift;
        scl = mul(scl, scale);
        xform._13_23_33 = mul(pose, xform._13_23_33);
    }

    col = saturate(col * rcp(n + 1.0));
    return col + src.Load(int3(input.pos.xy, 0)) * (1.0 - col.a) * mix;
}
