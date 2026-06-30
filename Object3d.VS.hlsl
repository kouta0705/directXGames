#include "Object3d.hlsli"

cbuffer TransformCB : register(b0)
{
    float32_t4x4 worldMatrix;
};

struct VertexShaderInput
{
    float32_t4 position : POSITION0;
    float32_t4 color : COLOR0;
    float32_t2 texcoord : TEXCOORD0;
};

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;
    output.position = mul(input.position, worldMatrix);
    output.color = input.color;
    output.texcoord = input.texcoord;
    return output;
}
