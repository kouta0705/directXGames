#include "Object3d.hlsli"

cbuffer MaterialCB : register(b1)
{
    float32_t4 materialColor;
};

Texture2D<float32_t4> gTexture : register(t0);
SamplerState gSampler : register(s0);

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    output.color = gTexture.Sample(gSampler, input.texcoord) * materialColor * input.color;
    return output;
}
