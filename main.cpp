#include <Windows.h>
#include <cstdint>
#include <string>
#include <format>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cassert>
#include <dxcapi.h>
// d3d12.lib / dxgi.lib / dxcompiler.lib のリンクは DirectXCommon.cpp に移植済み
#include "externals/DirectXTex/DirectXTex.h"
#include "externals/DirectXTex/d3dx12.h"
#include <vector>
#include <wrl.h>
#include "Vector3.h"
#include "Matrix4x4.h"
#include "Transform.h"
#include "DebugCamera.h"
// 入力クラス (DirectInputのdefine/includeはInput.h側に移動済み)
#include "Input.h"
// WindowsAPI
#include "WinApp.h"
// DirectX基盤
#include "DirectXCommon.h"
// ログ出力
#include "Logger.h"
// 文字列変換
#include "StringUtility.h"
// リソースリークチェック
#include "D3DResourceLeakChecker.h"
// OBJファイル読み込みに必要
#include <fstream>
#include <sstream>
// XAudio2
#include <xaudio2.h>
#pragma comment(lib, "xaudio2.lib")
#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
// ImGui_ImplWin32_WndProcHandler の extern 宣言は WinApp.cpp に移動
#endif

struct Vector2 {
    float x, y;
};

struct Vector4 {
    float x, y, z, w;
};

// 法線付き頂点データ
struct VertexData {
    Vector4 position;
    Vector2 texCoord;
    Vector3 normal;
};

// マテリアルデータ
struct MaterialData {
    std::string textureFilePath;
};

// モデルデータ
struct ModelData {
    std::vector<VertexData> vertices;
    MaterialData material;
};

// enableLighting フラグ付きマテリアル
struct Material {
    Vector4     color;
    int32_t     enableLighting;
    float       padding[3];   // HLSLのAlignment(16Byte境界)に合わせるためのパディング
    Matrix4x4   uvTransform;  // UVTransform用 (Matrix3x3はアライメント問題でMatrix4x4を使用)
};

// WorldMatrix付き変換行列
struct TransformationMatrix {
    Matrix4x4 WVP;
    Matrix4x4 World;
};

// 平行光源
struct DirectionalLight {
    Vector4 color;     //!< ライトの色
    Vector3 direction; //!< ライトの向き
    float   intensity; //!< 輝度
};

// ※ ConvertString は StringUtility に、Log は Logger に移動した
// ※ CreateDescriptorHeap / CreateDepthStencilTextureResource /
//    GetCPUDescriptorHandle / GetGPUDescriptorHandle は DirectXCommon に移動した

// ※ CreateBufferResource / CreateTextureResource / LoadTexture / CompileShader / UploadTextureData は DirectXCommon に移動した
// ※ D3DResourceLeakChecker は D3DResourceLeakChecker.h / .cpp に分離した

// MTLファイルを読み込む関数
MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename) {
    MaterialData materialData;
    std::string line;

    // 1. ファイルを開く
    std::ifstream file(directoryPath + "/" + filename);
    assert(file.is_open());

    // 2. ファイルを1行ずつ読み、MaterialDataを構築
    while (std::getline(file, line)) {
        std::string identifier;
        std::istringstream s(line);
        s >> identifier;

        // identifierに応じた処理
        if (identifier == "map_Kd") {
            std::string textureFilename;
            s >> textureFilename;
            // 連結してファイルパスにする
            materialData.textureFilePath = directoryPath + "/" + textureFilename;
        }
    }

    // 3. MaterialDataを返す
    return materialData;
}

// OBJファイルを読み込む関数
ModelData LoadObjFile(const std::string& directoryPath, const std::string& filename) {
    ModelData modelData;

    // 1. 中で必要となる変数の宣言
    std::vector<Vector4> positions; // 位置
    std::vector<Vector3> normals;   // 法線
    std::vector<Vector2> texcoords; // テクスチャ座標
    std::string line;

    // 2. ファイルを開く
    std::ifstream file(directoryPath + "/" + filename);
    assert(file.is_open());

    // 3. 実際にファイルを読み、ModelDataを構築していく
    while (std::getline(file, line)) {
        std::string identifier;
        std::istringstream s(line);
        s >> identifier; // 先頭の識別子を読む

        // identifierに応じた処理
        if (identifier == "v") {
            // 頂点位置
            Vector4 position;
            s >> position.x >> position.y >> position.z;
            position.w = 1.0f;
            // 右手系→左手系: xを反転
            position.x *= -1.0f;
            positions.push_back(position);

        }
        else if (identifier == "vt") {
            // テクスチャ座標
            Vector2 texcoord;
            s >> texcoord.x >> texcoord.y;
            // objのTexture座標は左下原点なので、左上原点に変換
            texcoord.y = 1.0f - texcoord.y;
            texcoords.push_back(texcoord);

        }
        else if (identifier == "vn") {
            // 法線
            Vector3 normal;
            s >> normal.x >> normal.y >> normal.z;
            // 右手系→左手系: xを反転
            normal.x *= -1.0f;
            normals.push_back(normal);

        }
        else if (identifier == "f") {
            // 面(三角形限定)
            VertexData triangle[3];

            for (int32_t faceVertex = 0; faceVertex < 3; ++faceVertex) {
                std::string vertexDefinition;
                s >> vertexDefinition;

                // 頂点の要素へのIndexは「位置/UV/法線」で格納されているので、分解してIndexを取得する
                std::istringstream v(vertexDefinition);
                uint32_t elementIndices[3];
                for (int32_t element = 0; element < 3; ++element) {
                    std::string index;
                    std::getline(v, index, '/'); // /区切りでインデックスを読んでいく
                    elementIndices[element] = std::stoi(index);
                }

                // 要素へのIndexから、実際の要素の値を取得して、頂点を構築する
                // インデックスは1始まりなので-1する
                Vector4 position = positions[elementIndices[0] - 1];
                Vector2 texcoord = texcoords[elementIndices[1] - 1];
                Vector3 normal = normals[elementIndices[2] - 1];

                triangle[faceVertex] = { position, texcoord, normal };
            }

            // 右手系→左手系: 頂点を逆順で登録することで、回り順を逆にする
            modelData.vertices.push_back(triangle[2]);
            modelData.vertices.push_back(triangle[1]);
            modelData.vertices.push_back(triangle[0]);

        }
        else if (identifier == "mtllib") {
            // materialTemplateLibraryファイルの名前を取得する
            std::string materialFilename;
            s >> materialFilename;
            // 基本的にobjファイルと同一階層にmtlは存在させるので、ディレクトリ名とファイル名を渡す
            modelData.material = LoadMaterialTemplateFile(directoryPath, materialFilename);
        }
    }

    // 4. ModelDataを返す
    return modelData;
}

// ======================================================
// XAudio2 サウンド関連
// ======================================================

// チャンクヘッダ
struct ChunkHeader
{
    char     id[4];    // チャンク毎のID
    int32_t  size;     // チャンクサイズ
};

// RIFFヘッダチャンク
struct RiffHeader
{
    ChunkHeader chunk;  // "RIFF"
    char        type[4]; // "WAVE"
};

// FMTチャンク
struct FormatChunk
{
    ChunkHeader  chunk; // "fmt "
    WAVEFORMATEX fmt;   // 波形フォーマット
};

// 音声データ
struct SoundData
{
    WAVEFORMATEX wfex;        // 波形フォーマット
    BYTE* pBuffer;     // バッファの先頭アドレス
    unsigned int bufferSize;  // バッファのサイズ
};

// 音声読み込み関数
SoundData SoundLoadWave(const char* filename)
{

    // ① ファイルオープン
    std::ifstream file;
    file.open(filename, std::ios_base::binary);
    assert(file.is_open());

    // ② .wavデータ読み込み

    // RIFFヘッダーの読み込み
    RiffHeader riff;
    file.read((char*)&riff, sizeof(riff));
    // ファイルがRIFFかチェック
    if (strncmp(riff.chunk.id, "RIFF", 4) != 0) {
        assert(0);
    }
    // タイプがWAVEかチェック
    if (strncmp(riff.type, "WAVE", 4) != 0) {
        assert(0);
    }

    // Formatチャンクの読み込み
    FormatChunk format = {};
    // チャンクヘッダーの確認
    file.read((char*)&format, sizeof(ChunkHeader));
    if (strncmp(format.chunk.id, "fmt ", 4) != 0) {
        assert(0);
    }
    // チャンク本体の読み込み
    assert(format.chunk.size <= sizeof(format.fmt));
    file.read((char*)&format.fmt, format.chunk.size);

    // Dataチャンクの読み込み
    ChunkHeader data;
    file.read((char*)&data, sizeof(data));
    // JUNKチャンクを検出した場合
    if (strncmp(data.id, "JUNK", 4) == 0) {
        // 読み取り位置をJUNKチャンクの終わりまで進める
        file.seekg(data.size, std::ios_base::cur);
        // 再読み込み
        file.read((char*)&data, sizeof(data));
    }
    if (strncmp(data.id, "data", 4) != 0) {
        assert(0);
    }
    // Dataチャンクのデータ部（波形データ）の読み込み
    char* pBuffer = new char[data.size];
    file.read(pBuffer, data.size);

    // ③ ファイルクローズ
    file.close();

    // ④ 読み込んだ音声データをreturn
    SoundData soundData = {};
    soundData.wfex = format.fmt;
    soundData.pBuffer = reinterpret_cast<BYTE*>(pBuffer);
    soundData.bufferSize = data.size;

    return soundData;
}

// 音声データ解放
void SoundUnload(SoundData* soundData)
{
    // バッファのメモリを解放
    delete[] soundData->pBuffer;

    soundData->pBuffer = 0;
    soundData->bufferSize = 0;
    soundData->wfex = {};
}

// 音声再生
void SoundPlayWave(IXAudio2* xAudio2, const SoundData& soundData)
{
    HRESULT result;

    // 波形フォーマットを元にSourceVoiceの生成
    IXAudio2SourceVoice* pSourceVoice = nullptr;
    result = xAudio2->CreateSourceVoice(&pSourceVoice, &soundData.wfex);
    assert(SUCCEEDED(result));

    // 再生する波形データの設定
    XAUDIO2_BUFFER buf{};
    buf.pAudioData = soundData.pBuffer;
    buf.AudioBytes = soundData.bufferSize;
    buf.Flags = XAUDIO2_END_OF_STREAM;

    // 波形データの再生
    result = pSourceVoice->SubmitSourceBuffer(&buf);
    result = pSourceVoice->Start();
}

// ※ WindowProc は WinApp::WindowProc に移動したので、ここには無い

int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {

    // リークチェッカーをmainの先頭で作る
    // デストラクタの呼び出しはコンストラクタと逆順なので、
    // 全ComPtrが解放された後に呼ばれる
    D3DResourceLeakChecker leakCheck;

    // ポインタ
    WinApp* winApp = nullptr;
    DirectXCommon* dxCommon = nullptr;

    // WindowsAPIの初期化
    // (COMの初期化・ウィンドウクラス登録・ウィンドウ生成と表示を含む)
    winApp = new WinApp();
    winApp->Initialize();

    // DirectXの初期化
    dxCommon = new DirectXCommon();
    dxCommon->Initialize(winApp);

    // ======================================================
    // 初期化部分 (device / commandList / DXCの各オブジェクトなどは dxCommon のgetter経由で取得する)
    // ======================================================
    // XAudio2関連変数
    // IXAudio2はCOMオブジェクトなのでComPtrで管理
    Microsoft::WRL::ComPtr<IXAudio2> xAudio2;
    // IXAudio2MasteringVoiceにはRelease()がないのでComPtrで管理できない
    IXAudio2MasteringVoice* masterVoice;

    HRESULT hr;

    // DescriptorRange
    D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
    descriptorRange[0].BaseShaderRegister = 0;
    descriptorRange[0].NumDescriptors = 1;
    descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // RootParameter: 4つに拡張
    // [0] Material        (PS, b0)
    // [1] TransformMatrix (VS, b0)
    // [2] Texture         (PS, DescriptorTable t0)
    // [3] DirectionalLight(PS, b1)
    D3D12_ROOT_PARAMETER rootParameters[4] = {};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[0].Descriptor.ShaderRegister = 0;

    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[1].Descriptor.ShaderRegister = 0;

    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRange;
    rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange);

    // DirectionalLight 用 CBV
    rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[3].Descriptor.ShaderRegister = 1;

    D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
    staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[0].ShaderRegister = 0;
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
    descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    descriptionRootSignature.pParameters = rootParameters;
    descriptionRootSignature.NumParameters = _countof(rootParameters);
    descriptionRootSignature.pStaticSamplers = staticSamplers;
    descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);

    Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, signatureBlob.GetAddressOf(), errorBlob.GetAddressOf());
    assert(SUCCEEDED(hr));
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    dxCommon->GetDevice()->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(rootSignature.GetAddressOf()));

    // InputLayout: POSITION / TEXCOORD / NORMAL の3要素
    D3D12_INPUT_ELEMENT_DESC inputElementDescriptions[3] = {};
    inputElementDescriptions[0].SemanticName = "POSITION";
    inputElementDescriptions[0].SemanticIndex = 0;
    inputElementDescriptions[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    inputElementDescriptions[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDescriptions[1].SemanticName = "TEXCOORD";
    inputElementDescriptions[1].SemanticIndex = 0;
    inputElementDescriptions[1].Format = DXGI_FORMAT_R32G32_FLOAT;
    inputElementDescriptions[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDescriptions[2].SemanticName = "NORMAL";
    inputElementDescriptions[2].SemanticIndex = 0;
    inputElementDescriptions[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    inputElementDescriptions[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

    D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
    inputLayoutDesc.pInputElementDescs = inputElementDescriptions;
    inputLayoutDesc.NumElements = _countof(inputElementDescriptions);

    D3D12_BLEND_DESC blendDesc{};
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    D3D12_RASTERIZER_DESC rasterizerDesc{};
    rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
    rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

    Microsoft::WRL::ComPtr<IDxcBlob> vertexShaderBlob = dxCommon->CompileShader(L"Object3d.VS.hlsl", L"vs_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> pixelShaderBlob = dxCommon->CompileShader(L"Object3d.PS.hlsl", L"ps_6_0");

    D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
    depthStencilDesc.DepthEnable = true;
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};
    graphicsPipelineStateDesc.pRootSignature = rootSignature.Get();
    graphicsPipelineStateDesc.InputLayout = inputLayoutDesc;
    graphicsPipelineStateDesc.VS = { vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize() };
    graphicsPipelineStateDesc.PS = { pixelShaderBlob->GetBufferPointer(),  pixelShaderBlob->GetBufferSize() };
    graphicsPipelineStateDesc.BlendState = blendDesc;
    graphicsPipelineStateDesc.RasterizerState = rasterizerDesc;
    graphicsPipelineStateDesc.NumRenderTargets = 1;
    graphicsPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    graphicsPipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    graphicsPipelineStateDesc.SampleDesc.Count = 1;
    graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    graphicsPipelineStateDesc.DepthStencilState = depthStencilDesc;
    graphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState;
    hr = dxCommon->GetDevice()->CreateGraphicsPipelineState(&graphicsPipelineStateDesc, IID_PPV_ARGS(graphicsPipelineState.GetAddressOf()));
    assert(SUCCEEDED(hr));

    // モデル読み込み
    ModelData modelData = LoadObjFile("resources/axis", "axis.obj");

    // 頂点リソースを作る
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource = dxCommon->CreateBufferResource(sizeof(VertexData) * modelData.vertices.size());

    D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
    vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();
    vertexBufferView.SizeInBytes = UINT(sizeof(VertexData) * modelData.vertices.size());
    vertexBufferView.StrideInBytes = sizeof(VertexData);

    // 頂点リソースにデータを書き込む
    VertexData* vertexData = nullptr;
    vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
    std::memcpy(vertexData, modelData.vertices.data(), sizeof(VertexData) * modelData.vertices.size());
    vertexResource->Unmap(0, nullptr);

    // モデル用 Material
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource = dxCommon->CreateBufferResource(sizeof(Material));
    Material* materialData2 = nullptr;
    materialResource->Map(0, nullptr, reinterpret_cast<void**>(&materialData2));
    materialData2->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    materialData2->enableLighting = true;
    materialData2->uvTransform = MakeIdentity4x4();

    // モデル用 TransformationMatrix
    Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource = dxCommon->CreateBufferResource(sizeof(TransformationMatrix));
    TransformationMatrix* transformationMatrixData = nullptr;
    wvpResource->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixData));
    transformationMatrixData->WVP = MakeIdentity4x4();
    transformationMatrixData->World = MakeIdentity4x4();

    Transform transform{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
    Transform cameraTransform{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -10.0f} };

    // デバッグカメラ（初期状態は通常カメラ＝ImGuiのCamera項目が効く状態、SPACEで切り替え）
    DebugCamera debugCamera;
    debugCamera.Initialize();
    bool isDebugCamera = false;

    // Sprite
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResourceSprite = dxCommon->CreateBufferResource(sizeof(VertexData) * 4);

    D3D12_VERTEX_BUFFER_VIEW vertexBufferViewSprite{};
    vertexBufferViewSprite.BufferLocation = vertexResourceSprite->GetGPUVirtualAddress();
    vertexBufferViewSprite.SizeInBytes = sizeof(VertexData) * 4;
    vertexBufferViewSprite.StrideInBytes = sizeof(VertexData);

    VertexData* vertexDataSprite = nullptr;
    vertexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&vertexDataSprite));
    //左下
    vertexDataSprite[0].position = { 0.0f,   360.0f, 0.0f, 1.0f };
    vertexDataSprite[0].texCoord = { 0.0f, 1.0f };
    //左上
    vertexDataSprite[1].position = { 0.0f,   0.0f,   0.0f, 1.0f };
    vertexDataSprite[1].texCoord = { 0.0f, 0.0f };
    //右下
    vertexDataSprite[2].position = { 640.0f, 360.0f, 0.0f, 1.0f };
    vertexDataSprite[2].texCoord = { 1.0f, 1.0f };
    //右上
    vertexDataSprite[3].position = { 640.0f, 0.0f,   0.0f, 1.0f };
    vertexDataSprite[3].texCoord = { 1.0f, 0.0f };
    // Spriteの法線は-z方向
    for (int i = 0; i < 4; ++i) {
        vertexDataSprite[i].normal = { 0.0f, 0.0f, -1.0f };
    }
    vertexResourceSprite->Unmap(0, nullptr);

    // Sprite用 IndexResource
    Microsoft::WRL::ComPtr<ID3D12Resource> indexResourceSprite = dxCommon->CreateBufferResource(sizeof(uint32_t) * 6);

    D3D12_INDEX_BUFFER_VIEW indexBufferViewSprite{};
    indexBufferViewSprite.BufferLocation = indexResourceSprite->GetGPUVirtualAddress();
    indexBufferViewSprite.SizeInBytes = sizeof(uint32_t) * 6;
    indexBufferViewSprite.Format = DXGI_FORMAT_R32_UINT;

    // インデックスリソースにデータを書き込む
    uint32_t* indexDataSprite = nullptr;
    indexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&indexDataSprite));
    indexDataSprite[0] = 0;    indexDataSprite[1] = 1;    indexDataSprite[2] = 2;
    indexDataSprite[3] = 1;    indexDataSprite[4] = 3;    indexDataSprite[5] = 2;
    indexResourceSprite->Unmap(0, nullptr);

    // Sprite用 TransformationMatrix
    Microsoft::WRL::ComPtr<ID3D12Resource> transformationMatrixResourceSprite = dxCommon->CreateBufferResource(sizeof(TransformationMatrix));
    TransformationMatrix* transformationMatrixDataSprite = nullptr;
    transformationMatrixResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixDataSprite));
    transformationMatrixDataSprite->WVP = MakeIdentity4x4();
    transformationMatrixDataSprite->World = MakeIdentity4x4();

    // Sprite用 Material
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResourceSprite = dxCommon->CreateBufferResource(sizeof(Material));
    Material* materialDataSprite = nullptr;
    materialResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&materialDataSprite));
    materialDataSprite->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    materialDataSprite->enableLighting = false;
    materialDataSprite->uvTransform = MakeIdentity4x4();

    Transform transformSprite{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {500.0f, 340.0f, 0.0f} };
    // UVTransform用の変数
    Transform uvTransformSprite{
        {1.0f, 1.0f, 1.0f},  // scale
        {0.0f, 0.0f, 0.0f},  // rotate
        {0.0f, 0.0f, 0.0f},  // translate
    };

    // DirectionalLight リソース
    Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource = dxCommon->CreateBufferResource(sizeof(DirectionalLight));
    DirectionalLight* directionalLightData = nullptr;
    directionalLightResource->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData));
    directionalLightData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    directionalLightData->direction = { 0.0f, -1.0f, 0.0f };
    directionalLightData->intensity = 1.0f;

    bool useMonsterBall = true;

    DirectX::ScratchImage mipImages = DirectXCommon::LoadTexture("resources/uvChecker.png");
    const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
    Microsoft::WRL::ComPtr<ID3D12Resource> textureResource = dxCommon->CreateTextureResource(metadata);
    Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource = dxCommon->UploadTextureData(textureResource, mipImages);

    // モデルのテクスチャをMaterialDataのパスから読み込む
    DirectX::ScratchImage mipImages2 = DirectXCommon::LoadTexture(modelData.material.textureFilePath);
    const DirectX::TexMetadata& metadata2 = mipImages2.GetMetadata();
    Microsoft::WRL::ComPtr<ID3D12Resource> textureResource2 = dxCommon->CreateTextureResource(metadata2);
    Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource2 = dxCommon->UploadTextureData(textureResource2, mipImages2);

    // コマンドを実行してGPUの転送完了を待つ
    dxCommon->ExecuteCommandAndWait();

    // ComPtrなので自動解放。ここでスコープを外すためにResetで明示的に解放
    intermediateResource.Reset();
    intermediateResource2.Reset();

    // SRV作成 (uvChecker)
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = metadata.format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);
    D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU = DirectXCommon::GetCPUDescriptorHandle(dxCommon->GetSrvDescriptorHeap(), dxCommon->GetDescriptorSizeSRV(), 1);
    D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU = DirectXCommon::GetGPUDescriptorHandle(dxCommon->GetSrvDescriptorHeap(), dxCommon->GetDescriptorSizeSRV(), 1);
    dxCommon->GetDevice()->CreateShaderResourceView(textureResource.Get(), &srvDesc, textureSrvHandleCPU);

    // SRV作成 (モデルのテクスチャ)
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc2{};
    srvDesc2.Format = metadata2.format;
    srvDesc2.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc2.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc2.Texture2D.MipLevels = UINT(metadata2.mipLevels);
    D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU2 = DirectXCommon::GetCPUDescriptorHandle(dxCommon->GetSrvDescriptorHeap(), dxCommon->GetDescriptorSizeSRV(), 2);
    D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU2 = DirectXCommon::GetGPUDescriptorHandle(dxCommon->GetSrvDescriptorHeap(), dxCommon->GetDescriptorSizeSRV(), 2);
    dxCommon->GetDevice()->CreateShaderResourceView(textureResource2.Get(), &srvDesc2, textureSrvHandleCPU2);

    // XAudio2エンジンのインスタンスを生成
    hr = XAudio2Create(&xAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);
    // マスターボイスを生成
    hr = xAudio2->CreateMasteringVoice(&masterVoice);

    // 音声読み込み
    SoundData soundData1 = SoundLoadWave("Resources/fanfare.wav");
    // 音声再生
    SoundPlayWave(xAudio2.Get(), soundData1);

    // 入力の初期化 (DirectInput/キーボードの生成はInputクラス内で行う)
    Input* input = new Input();
    input->Initialize(winApp);

    //メインループ
    while (true) {
        // Windowsのメッセージ処理
        if (winApp->ProcessMessage()) {
            // ゲームループを抜ける
            break;
        }

        // 入力の更新
        input->Update();

        // 左キーが押されていたら
        if (input->PushKey(DIK_LEFT)) {
            OutputDebugStringA("Hit LEFT\n");
        }
        // 右キーが押されていたら
        if (input->PushKey(DIK_RIGHT)) {
            OutputDebugStringA("Hit RIGHT\n");
        }
        // 上キーが押されていたら
        if (input->PushKey(DIK_UP)) {
            OutputDebugStringA("Hit UP\n");
        }
        // 下キーが押されていたら
        if (input->PushKey(DIK_DOWN)) {
            OutputDebugStringA("Hit DOWN\n");
        }

        // SPACEキーが押された瞬間にデバッグカメラ／通常カメラを切り替え
        if (input->TriggerKey(DIK_SPACE)) {
            isDebugCamera = !isDebugCamera;
        }

        // デバッグカメラ更新（デバッグカメラモードのときのみ）
        if (isDebugCamera) {
            debugCamera.Update(winApp->GetHwnd());
        }

#ifdef USE_IMGUI
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
#endif

        // 通常カメラモードのときだけ自動回転
        if (!isDebugCamera) {
            transform.rotate.y += 0.03f;
        }

        // モデル用 行列計算
        Matrix4x4 worldMatrix = MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);

        Matrix4x4 viewMatrix;
        if (isDebugCamera) {
            // デバッグカメラのビュー行列を使う
            viewMatrix = debugCamera.GetViewMatrix();
        }
        else {
            // 通常カメラのビュー行列
            Matrix4x4 cameraMatrix = MakeAffineMatrix(cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);
            viewMatrix = Inverse(cameraMatrix);
        }

        Matrix4x4 projectionMatrix = MakePerspectiveFovMatrix(
            0.45f,
            float(WinApp::kClientWidth) / float(WinApp::kClientHeight),
            0.1f,
            100.0f
        );
        Matrix4x4 worldViewProjectionMatrix = Multiply(worldMatrix, Multiply(viewMatrix, projectionMatrix));
        transformationMatrixData->WVP = worldViewProjectionMatrix;
        transformationMatrixData->World = worldMatrix;

#ifdef USE_IMGUI
        // ImGuiのSettingsウィンドウ (カメラの種類に関係なく常に表示する)
        ImGui::Begin("Settings");
        ImGui::DragFloat3("CameraTranslate", &cameraTransform.translate.x, 0.01f);
        ImGui::DragFloat("CameraRotateX", &cameraTransform.rotate.x, 0.01f);
        ImGui::DragFloat("CameraRotateY", &cameraTransform.rotate.y, 0.01f);
        ImGui::DragFloat("CameraRotateZ", &cameraTransform.rotate.z, 0.01f);
        ImGui::SliderAngle("SphereRotateX", &transform.rotate.x);
        ImGui::SliderAngle("SphereRotateY", &transform.rotate.y);
        ImGui::SliderAngle("SphereRotateZ", &transform.rotate.z);
        ImGui::ColorEdit4("color", &materialData2->color.x);
        ImGui::Checkbox("enableLighting", reinterpret_cast<bool*>(&materialData2->enableLighting));
        ImGui::ColorEdit4("colorSprite", &materialDataSprite->color.x);
        ImGui::DragFloat3("translateSprite", &transformSprite.translate.x, 1.0f);
        ImGui::Checkbox("useMonsterBall", &useMonsterBall);
        ImGui::ColorEdit4("LightColor", &directionalLightData->color.x);
        ImGui::DragFloat3("LightDirection", &directionalLightData->direction.x, 0.01f);
        ImGui::DragFloat("Intensity", &directionalLightData->intensity, 0.01f);
        // UVTransform編集 (UV座標系はxy平面なので回転軸はz)
        ImGui::DragFloat2("UVTranslate", &uvTransformSprite.translate.x, 0.01f, -10.0f, 10.0f);
        ImGui::DragFloat2("UVScale", &uvTransformSprite.scale.x, 0.01f, -10.0f, 10.0f);
        ImGui::SliderAngle("UVRotate", &uvTransformSprite.rotate.z);
        ImGui::End();

        // UVTransform行列を生成してSpriteのMaterialにセット (SRTの順)
        Matrix4x4 uvTransformMatrix = MakeScaleMatrix(uvTransformSprite.scale);
        uvTransformMatrix = Multiply(uvTransformMatrix, MakeRotateZMatrix(uvTransformSprite.rotate.z));
        uvTransformMatrix = Multiply(uvTransformMatrix, MakeTranslateMatrix(uvTransformSprite.translate));
        materialDataSprite->uvTransform = uvTransformMatrix;
#endif

        // Sprite用 行列計算 (ImGuiでの変更をそのフレームで反映するため、ImGuiの後に行う)
        Matrix4x4 worldMatrixSprite = MakeAffineMatrix(transformSprite.scale, transformSprite.rotate, transformSprite.translate);
        Matrix4x4 viewMatrixSprite = MakeIdentity4x4();
        Matrix4x4 projectionMatrixSprite = MakeOrthographicMatrix(0.0f, 0.0f, float(WinApp::kClientWidth), float(WinApp::kClientHeight), 0.0f, 100.0f);
        Matrix4x4 worldViewProjectionMatrixSprite = Multiply(worldMatrixSprite, Multiply(viewMatrixSprite, projectionMatrixSprite));
        transformationMatrixDataSprite->WVP = worldViewProjectionMatrixSprite;
        transformationMatrixDataSprite->World = worldMatrixSprite;

#ifdef USE_IMGUI
        ImGui::Render();
#endif

        // 描画前処理 (バリア切り替え・クリア・ビューポート設定などはDirectXCommonに移植済み)
        dxCommon->PreDraw();

        ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();
        commandList->SetGraphicsRootSignature(rootSignature.Get());
        commandList->SetPipelineState(graphicsPipelineState.Get());
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // モデルの描画 (今は表示しないので無効化中。戻すときはコメントを外す)
        //commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
        //commandList->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress());
        //commandList->SetGraphicsRootConstantBufferView(1, wvpResource->GetGPUVirtualAddress());
        //if (useMonsterBall) {
        //    commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU2);
        //}
        //else {
        //    commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU);
        //}
        //commandList->DrawInstanced(UINT(modelData.vertices.size()), 1, 0, 0);

        // ルートパラメータ3(ライト)はモデルの描画で設定していたが、ここで設定しておく
        commandList->SetGraphicsRootConstantBufferView(3, directionalLightResource->GetGPUVirtualAddress());

        // Spriteの描画
        commandList->IASetVertexBuffers(0, 1, &vertexBufferViewSprite);
        commandList->IASetIndexBuffer(&indexBufferViewSprite);
        commandList->SetGraphicsRootConstantBufferView(0, materialResourceSprite->GetGPUVirtualAddress());
        commandList->SetGraphicsRootConstantBufferView(1, transformationMatrixResourceSprite->GetGPUVirtualAddress());
        commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU);
        commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);

#ifdef USE_IMGUI
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
#endif

        // 描画後処理 (バリア切り替え・コマンド実行・Present・フェンス待ち・リセットはDirectXCommonに移植済み)
        dxCommon->PostDraw();
    }

    // 入力解放
    delete input;

    // XAudio2解放（先にインスタンスをリセットしてからデータを解放する）
    xAudio2.Reset();
    // 音声データ解放
    SoundUnload(&soundData1);

    // DirectX解放 (ImGui終了・フェンスイベントのクローズはデストラクタで行う)
    delete dxCommon;

    // WindowsAPIの終了処理 (CoUninitialize もここに含まれる)
    winApp->Finalize();

    // WindowsAPI解放
    delete winApp;

    return 0;
}