Texture2D regularized_flow_texture : register(t0);
Texture2D propagated_flow_texture : register(t1);
Texture2D depth_texture : register(t2);
SamplerState linear_sampler : register(s0);
cbuffer params : register(b0) {
    float2 texel;
    int mode;
    float scale;
}

float4 main(float4 pos : SV_Position) : SV_Target {
    if (mode >= 1 && mode <= 3) {
        const float4 flow = regularized_flow_texture.Load(int3(pos.xy, 0));

        switch (mode) {
            case 1:
                return float4(saturate(flow.xy * scale * 0.5 + 0.5), 0.5, 1.0);
            case 2:
                return float4(1.0 - flow.zzz, 1.0);
            case 3:
                return float4(flow.www, 1.0);
        }
    }

    if (mode == 4) {
        const float depth = saturate(
            dot(depth_texture.SampleLevel(linear_sampler, pos.xy * texel, 0.0).rgb,
                float3(0.2126, 0.7152, 0.0722)));
        return float4(depth.xxx, 1.0);
    }

    if (mode == 5) {
        const float4 flow = propagated_flow_texture.Load(int3(pos.xy, 0));
        return float4(saturate(flow.xy * scale * 0.5 + 0.5), 0.5, 1.0);
    }

    if (mode == 6) {
        const float score = propagated_flow_texture.Load(int3(pos.xy, 0)).z;
        return float4(score.xxx, 1.0);
    }

    return float4(0.0, 0.0, 0.0, 0.0);
}
