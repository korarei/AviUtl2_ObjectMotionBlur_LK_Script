struct FlowComputeInput {
    uint3 gid : SV_GroupID;
    uint3 gtid : SV_GroupThreadID;
    uint gidx : SV_GroupIndex;
    uint3 dtid : SV_DispatchThreadID;
};

cbuffer params : register(b0) {
    uint2 resolution;
    uint grid_size;
    float flow_scale;
    float2 shutter;
    int propagation_step;
};
