#include "Object3d.hlsli"

struct TransformationMatrix
{
    float4x4 WVP;
    float4x4 World; // 追加：法線変換に使う
};

ConstantBuffer<TransformationMatrix> gTransformationMatrix : register(b0);

struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0; // 追加
};

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;

    // 頂点座標をWVP変換
    output.position = mul(input.position, gTransformationMatrix.WVP);

    output.texcoord = input.texcoord;

    // 法線をワールド空間に変換
    // 変換後に正規化して単位ベクトルにする
    output.normal = normalize(mul(input.normal, (float3x3) gTransformationMatrix.World));

    return output;
}
