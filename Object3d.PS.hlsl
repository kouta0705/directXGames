#include "Object3d.hlsli"

struct Material
{
    float4 color;
    int enableLighting;
    float3 padding; 
    float4x4 uvTransform; // UVTransform用
};

struct DirectionalLight
{
    float4 color; //!< ライトの色
    float3 direction; //!< ライトの向き
    float intensity; //!< 輝度
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
    // UV座標をz=0の(3+1)次元同次座標と考えてUVTransformを適用する
    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);

    if (gMaterial.enableLighting != 0)
    {
        // Half Lambert
        // NdotL: 法線とライト方向の内積
        float NdotL = dot(normalize(input.normal), -gDirectionalLight.direction);
        float cos = pow(NdotL * 0.5f + 0.5f, 2.0f);
        output.color = gMaterial.color * textureColor
                     * gDirectionalLight.color
                     * cos
                     * gDirectionalLight.intensity;
    }
    else
    {
        // Lightingなし
        output.color = gMaterial.color * textureColor;
    }

    return output;
}
