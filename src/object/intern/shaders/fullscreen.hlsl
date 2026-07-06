static const float4 kPositions[3] = {
    float4(-1.0,  1.0, 0.0, 1.0),
    float4( 3.0,  1.0, 0.0, 1.0),
    float4(-1.0, -3.0, 0.0, 1.0)
};

float4 main(uint id : SV_VertexID) : SV_Position {
    return kPositions[id];
}
