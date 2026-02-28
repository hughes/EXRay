// EXRay tone mapping shader
// Vertex shader: transforms image quad by pan/zoom matrix
// Pixel shader: applies exposure and gamma for display

cbuffer ViewportCB : register(b0)
{
    float4x4 transform;
    float exposure;
    float gamma;
    float2 _pad;
};

struct VS_INPUT
{
    float2 pos : POSITION;
    float2 uv : TEXCOORD0;
};

struct VS_OUTPUT
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

Texture2D<float4> imageTexture : register(t0);
SamplerState imageSampler : register(s0);

VS_OUTPUT VSMain(VS_INPUT input)
{
    VS_OUTPUT output;
    output.pos = mul(transform, float4(input.pos, 0.0, 1.0));
    output.uv = input.uv;
    return output;
}

float4 PSMain(VS_OUTPUT input) : SV_TARGET
{
    float4 hdr = imageTexture.Sample(imageSampler, input.uv);

    float exposureMul = exp2(exposure);
    float3 exposed = hdr.rgb * exposureMul;

    // Gamma tone map (clamp, then gamma curve)
    float3 mapped = pow(saturate(exposed), gamma);

    return float4(mapped, 1.0);
}
