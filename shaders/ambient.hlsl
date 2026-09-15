cbuffer Params : register(b0)
{
    float4 rect;
    float2 stepSize;
    float mixAmount;
    float strength;
    uint mode;
    uint encoding;
    uint outputLinear;
    float mip;
};
Texture2D<float4> source : register(t0);
Texture2D<float4> history : register(t1);
RWTexture2D<float4> target : register(u0);
SamplerState mirrorSampler : register(s0);

float3 decode(float3 c)
{
    if (encoding == 2) return c; // scRGB
    if (encoding == 0) return pow(max(c, 0), 2.2);
    float3 p = pow(saturate(c), 1.0 / 78.84375);
    c = pow(max(p - 0.8359375, 0) / (18.8515625 - 18.6875 * p), 1.0 / 0.1593017578125);
    // PQ / BT.2020 to scRGB: 1.0 is 80 nits.
    return mul(float3x3(1.660491, -0.587641, -0.072850,
                      -0.124550, 1.132900, -0.008349,
                      -0.018151, -0.100579, 1.118730), c) * 125.0;
}

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint w, h;
    target.GetDimensions(w, h);
    if (id.x >= w || id.y >= h) return;
    float2 uv = (float2(id.xy) + 0.5) / float2(w, h);
    float4 c;
    if (mode == 0) { // Exact pixel conversion before filtering.
        c = source.Load(int3(id.xy + uint2(rect.xy), 0));
        c = float4(decode(c.rgb), 1);
    } else if (mode == 1) { // Center crop to fill the background's aspect ratio.
        c = source.SampleLevel(mirrorSampler, rect.xy + uv * rect.zw, mip);
    } else if (mode == 2) { // Dual Kawase downsample.
        c = source.SampleLevel(mirrorSampler, uv, 0) * 4;
        c += source.SampleLevel(mirrorSampler, uv + stepSize, 0);
        c += source.SampleLevel(mirrorSampler, uv - stepSize, 0);
        c += source.SampleLevel(mirrorSampler, uv + float2(stepSize.x, -stepSize.y), 0);
        c += source.SampleLevel(mirrorSampler, uv + float2(-stepSize.x, stepSize.y), 0);
        c /= 8;
    } else if (mode == 3) { // Dual Kawase upsample.
        c = source.SampleLevel(mirrorSampler, uv + float2(2 * stepSize.x, 0), 0);
        c += source.SampleLevel(mirrorSampler, uv - float2(2 * stepSize.x, 0), 0);
        c += source.SampleLevel(mirrorSampler, uv + float2(0, 2 * stepSize.y), 0);
        c += source.SampleLevel(mirrorSampler, uv - float2(0, 2 * stepSize.y), 0);
        c += source.SampleLevel(mirrorSampler, uv + stepSize, 0) * 2;
        c += source.SampleLevel(mirrorSampler, uv - stepSize, 0) * 2;
        c += source.SampleLevel(mirrorSampler, uv + float2(stepSize.x, -stepSize.y), 0) * 2;
        c += source.SampleLevel(mirrorSampler, uv + float2(-stepSize.x, stepSize.y), 0) * 2;
        c /= 12;
    } else if (mode == 4) {
        c = source.Load(int3(id.xy, 0));
        if (mixAmount < 1) c = lerp(history.Load(int3(id.xy, 0)), c, mixAmount);
    } else {
        // Never smooth geometry or alpha across the video boundary.
        if (id.x >= rect.x && id.y >= rect.y && id.x < rect.z && id.y < rect.w) {
            target[id.xy] = 0;
            return;
        }
        c = source.SampleLevel(mirrorSampler, uv, 0);
        c.rgb *= strength;
        if (!outputLinear) c.rgb = pow(saturate(c.rgb), 1.0 / 2.2);
        c.a = 1;
    }
    target[id.xy] = c;
}
