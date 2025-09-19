Texture2D src : register(t0);
SamplerState src_smp : register(s0);
cbuffer params : register(b0) {
    column_major float3x3 init_htm;
    float2 drift;
    float2 res;
    float2 pivot;
    float smp;
    float mix;
};

static const float2 texel = rcp(res);
static const float2 min_uv = 0.5 * texel;
static const float2 max_uv = 1.0 - min_uv;

struct PS_INPUT {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

inline float4 pick_col(float2 pos) {
    float2 uv = pos * texel;
    float2 mask = step(0.0, uv) * step(uv, 1.0);
    return src.Sample(src_smp, clamp(uv, min_uv, max_uv)) * mask.x * mask.y;
}

float4 apply_blur(float3 pos, float n) {
    float4 col = pick_col(pos.xy + pivot);

    column_major float3x3 htm = init_htm;
    column_major float3x3 adj_mat = init_htm;

    adj_mat._m02_m12_m22 = float3(0.0, 0.0, 1.0);

    for (uint i = 1; i <= uint(n); ++i) {
        pos = mul(htm, pos);
        col += pick_col(pos.xy + pivot + drift * i);
        htm._m02_m12_m22 = mul(adj_mat, htm._m02_m12_m22);
    }

    return saturate(col * rcp(n));
}

float4 motion_blur(PS_INPUT input) : SV_Target {
    float3 pos = float3(input.uv * res - pivot, 1.0);
    float4 col = apply_blur(pos, smp);
    return col + src.Load(int3(input.pos.xy, 0)) * (1.0 - col.a) * mix;
}
