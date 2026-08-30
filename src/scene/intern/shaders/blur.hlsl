struct Accumulation {
    float4 color;
    float norm;
    float coverage;
};

static const float kEpsilon = 1.0e-5;

Texture2D source_image : register(t0);
Texture2D flow_maps[2] : register(t1);
SamplerState linear_sampler : register(s0);
cbuffer params : register(b0) {
    float2 texel;
    float2 mix;
    float2 falloff;
    int sample_limit;
};

Accumulation Blur(int index, float2 pos) {
    const float3 flow = flow_maps[index].Load(int3(pos, 0)).xyz;
    const float2 velocity = flow.xy;
    const float coverage = flow.z;

    Accumulation result = {float4(0.0, 0.0, 0.0, 0.0), 0.0, 0.0};

    if (dot(velocity, velocity) < kEpsilon || coverage < kEpsilon) {
        return result;
    }

    result.coverage = coverage;

    const int samples = max(1, min(sample_limit, max(1, int(ceil(length(velocity))) + 1)));

    for (int i = 0; i < samples; ++i) {
        const float t = (float(i) + 0.5) * rcp(float(samples));

        const float weight = coverage * smoothstep(0.0, falloff.y, t) * smoothstep(0.0, falloff.x, 1.0 - t);

        result.color += source_image.SampleLevel(linear_sampler, (pos - velocity * t) * texel, 0.0) * weight;
        result.norm += weight;
    }

    return result;
}

float4 main(float4 pos : SV_Position) : SV_Target {
    const float4 src = source_image.Load(int3(pos.xy, 0));
    const float4 dry = src * mix.x;

    const Accumulation first = Blur(0, pos.xy);
    const Accumulation second = Blur(1, pos.xy);

    const float norm = first.norm + second.norm;
    const float4 blurred = lerp(src, (first.color + second.color) * rcp(max(norm, kEpsilon)), step(kEpsilon, norm));
    const float4 wet = lerp(src, blurred, max(first.coverage, second.coverage));

    return mad(wet, mix.y, dry);
}
