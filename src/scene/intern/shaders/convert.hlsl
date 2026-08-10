Texture2D source_texture : register(t0);

float4 main(float4 pos : SV_Position) : SV_Target {
    return source_texture.Load(int3(pos.xy, 0));
}
