Texture2D flow_map : register(t0);
cbuffer params : register(b0) {
    float scale;
};

float4 main(float4 pos : SV_Position) : SV_Target {
    const float2 flow = flow_map.Load(int3(pos.xy, 0)).xy;
    return float4(saturate(flow * scale * 0.5 + 0.5), 0.5, 1.0);
}
