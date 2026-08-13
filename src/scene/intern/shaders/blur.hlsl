struct ColorDepthSample {
    float4 color;
    float depth;
};

static const float kEpsilon = 1.0e-5;
static const float kMinimumPropagationScore = 0.02;
static const float kDepthSupportRange = 0.10;
static const float3 kLumaCoefficients = float3(0.2126, 0.7152, 0.0722);

Texture2D source_texture : register(t0);
Texture2D propagated_flow_texture : register(t1);
Texture2D depth_texture : register(t2);
SamplerState linear_sampler : register(s0);
cbuffer params : register(b0) {
    float2 texel;
    float2 shutter;
    float2 mix;
    float falloff;
    int sample_limit;
}

inline ColorDepthSample SampleColorDepth(float2 pos) {
    const float2 uv = pos * texel;

    ColorDepthSample smp;
    smp.color = source_texture.SampleLevel(linear_sampler, uv, 0.0);
    smp.depth = saturate(dot(depth_texture.SampleLevel(linear_sampler, uv, 0.0).rgb, kLumaCoefficients));

    return smp;
}

inline float EvaluateForegroundWeight(float base, float src) {
    return 1.0 - smoothstep(0.0, kDepthSupportRange, max(base - src, 0.0));
}

float4 main(float4 pos : SV_Position) : SV_Target {
    const int3 loc = int3(pos.xy, 0);
    const float4 motion = propagated_flow_texture.Load(loc);

    float4 wet = 0.0;

    if (motion.z >= kMinimumPropagationScore) {
        const int required_samples = max(1, int(ceil(length(motion.xy) * abs(shutter.y - shutter.x))) + 1);
        const int samples = max(1, min(sample_limit, required_samples));
        const float scale = rcp(float(max(samples - 1, 1)));
        const float decay = pow(max(1.0 - falloff, kEpsilon), scale);

        float shutter_weight = 1.0;
        float norm = 0.0;

        for (int i = 0; i < samples; ++i) {
            const float t = lerp(shutter.x, shutter.y, float(i) * scale);
            const ColorDepthSample smp = SampleColorDepth(pos.xy - motion.xy * t);
            const float depth_weight = EvaluateForegroundWeight(motion.w, smp.depth);
            const float weight = shutter_weight * depth_weight;

            wet += smp.color * weight;
            norm += weight;
            shutter_weight *= decay;
        }

        wet *= rcp(max(norm, kEpsilon));
    }

    const float4 src = source_texture.Load(loc);
    const float4 dry = src * mix.x;

    return mad(1.0 - dry.a, mad(1.0 - wet.a, src, wet) * mix.y, dry);
}
