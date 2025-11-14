Texture2D src : register(t0);
SamplerState smp : register(s0);
cbuffer params : register(b0) {
    column_major float3x3 htm_base;
    float2 drift;
    float2 res;
    float2 pivot;
    float n;
    float mix;
};

struct PS_Input {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD;
};

float4 motion_blur(PS_Input input) : SV_Target {
    const float2 texel = rcp(res);
    const uint count = uint(n);

    float3 pos = float3(mad(input.uv, res, -pivot), 1.0);
    column_major float3x3 htm = htm_base;
    column_major float3x3 pose = htm_base;
    pose._m02_m12_m22 = float3(0.0, 0.0, 1.0);

    float4 col = src.Load(int3(input.pos.xy, 0));
    for (uint i = 1; i <= count; ++i) {
        pos = mul(htm, pos);
        float2 coord = mad(drift, float2(i.xx), pos.xy + pivot) * texel;
        col += src.Sample(smp, coord);
        htm._m02_m12_m22 = mul(pose, htm._m02_m12_m22);
    }

    col = saturate(col * rcp(n + 1.0));
    return col + src.Load(int3(input.pos.xy, 0)) * (1.0 - col.a) * mix;
}
