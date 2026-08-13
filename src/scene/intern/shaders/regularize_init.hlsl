#include "flow_params.hlsli"

Texture2D input_flow_texture : register(t0);
RWTexture2D<float4> output_flow_texture : register(u0);

[numthreads(8, 8, 1)]
void main(FlowComputeInput input) {
    if (any(input.dtid.xy >= resolution)) {
        return;
    }

    output_flow_texture[input.dtid.xy] = input_flow_texture.Load(int3(input.dtid.xy, 0));
}
