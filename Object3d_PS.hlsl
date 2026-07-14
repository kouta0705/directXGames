#include "Object3d.hlsli"

struct Material
{
    float4 color;
    int enableLighting;
    float3 padding;
    float4x4 uvTransform;
};

struct DirectionalLight
{
    float4 color;     // ライトの色
    float3 direction; // ライトの向き
    float intensity;  // 輝度
};

ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    // UV変換を適用してからサンプリングする
    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);

    if (gMaterial.enableLighting != 0)
    {
        float3 normal = normalize(input.normal);
        float NdotL = dot(normal, -normalize(gDirectionalLight.direction));

        // HalfLambert: -1〜1の内積を0〜1に収めてから2乗し、暗部の落ち方を柔らかくする
        float halfLambertFactor = pow(NdotL * 0.5f + 0.5f, 2.0f);

        output.color.rgb = gMaterial.color.rgb * textureColor.rgb *
            gDirectionalLight.color.rgb * halfLambertFactor * gDirectionalLight.intensity;
        output.color.a = gMaterial.color.a * textureColor.a;
    }
    else
    {
        output.color = gMaterial.color * textureColor;
    }

    return output;
}
