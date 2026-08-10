static const uint kTileSize = 16u;

Texture2D regularized_flow_texture : register(t0);
Texture2D classified_flow_texture : register(t1);
Texture2D layer_tile_texture[2] : register(t2);
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
        const float depth = classified_flow_texture.Load(int3(pos.xy, 0)).w;
        return float4(depth.xxx, 1.0);
    }

    if (mode == 5) {
        const int3 loc = int3(uint2(pos.xy) / kTileSize, 0);
        const float4 tile = layer_tile_texture[0].Load(loc);
        return float4(saturate(tile.xy * scale * 0.5 + 0.5), 0.5, 1.0);
    }

    if (mode == 6) {
        const int3 loc = int3(uint2(pos.xy) / kTileSize, 0);
        const float4 tile = layer_tile_texture[1].Load(loc);
        return float4(saturate(tile.xy * scale * 0.5 + 0.5), 0.5, 1.0);
    }

    return float4(0.0, 0.0, 0.0, 0.0);
}
