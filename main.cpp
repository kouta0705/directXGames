#include <Windows.h>
#include <cstdint>
#include <string>
#include <format>
#include <vector>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cassert>
#include <cmath>
#include <chrono>

#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")
#include <dxgidebug.h>
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_win32.h"
#include "externals/imgui/imgui_impl_dx12.h"
#endif

#ifdef USE_IMGUI
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

std::wstring ConvertString(const std::string& str) {
    if (str.empty()) return std::wstring();
    auto sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), NULL, 0);
    if (sizeNeeded == 0) return std::wstring();
    std::wstring result(sizeNeeded, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), sizeNeeded);
    return result;
}

std::string ConvertString(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    auto sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), NULL, 0, NULL, NULL);
    if (sizeNeeded == 0) return std::string();
    std::string result(sizeNeeded, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), result.data(), sizeNeeded, NULL, NULL);
    return result;
}

void Log(const std::string& message) { OutputDebugStringA(message.c_str()); }

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
#ifdef USE_IMGUI
    if (ImGui_ImplWin32_WndProcHandler(hwnd, uMsg, wParam, lParam)) return true;
#endif
    if (uMsg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

// ★ 16バイトアライメントの行列構造体
struct Matrix4x4 {
    float m[4][4];
};

// ★ Y軸回転行列を生成
Matrix4x4 MakeRotationY(float angle) {
    float c = cosf(angle);
    float s = sinf(angle);
    Matrix4x4 mat{};
    mat.m[0][0] = c;  mat.m[0][1] = 0; mat.m[0][2] = s; mat.m[0][3] = 0;
    mat.m[1][0] = 0;  mat.m[1][1] = 1; mat.m[1][2] = 0; mat.m[1][3] = 0;
    mat.m[2][0] = -s; mat.m[2][1] = 0; mat.m[2][2] = c; mat.m[2][3] = 0;
    mat.m[3][0] = 0;  mat.m[3][1] = 0; mat.m[3][2] = 0; mat.m[3][3] = 1;
    return mat;
}

// ★ 平行移動行列を生成
Matrix4x4 MakeTranslation(float x, float y, float z) {
    Matrix4x4 mat{};
    mat.m[0][0] = 1; mat.m[1][1] = 1; mat.m[2][2] = 1; mat.m[3][3] = 1;
    mat.m[3][0] = x; mat.m[3][1] = y; mat.m[3][2] = z;
    return mat;
}

// ★ 定数バッファ用構造体（256バイトアライメント必須）
struct alignas(256) ConstantBufferData {
    Matrix4x4 worldMatrix;
};

// ★ マテリアル用の定数バッファ（色のみ、デフォルトは白）
struct alignas(256) MaterialCBData {
    float color[4];
};

// ★ スプライト用 Transform（CPUで保持）
struct SpriteTransform {
    float translate[3] = { 0.0f, 0.0f, 0.0f };
};

int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {
    HRESULT hr = S_OK;

    WNDCLASSW wc{};
    wc.lpfnWndProc = WindowProc;
    wc.lpszClassName = L"CG2WindowClass";
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    const int32_t kClientWidth = 1280;
    const int32_t kClientHeight = 720;
    RECT wrc = { 0, 0, kClientWidth, kClientHeight };
    AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hwnd = CreateWindowW(wc.lpszClassName, L"CG2", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, wrc.right - wrc.left, wrc.bottom - wrc.top,
        nullptr, nullptr, wc.hInstance, nullptr);
    if (hwnd == nullptr) return 0;
    ShowWindow(hwnd, SW_SHOW);

#ifdef _DEBUG
    ID3D12Debug1* debugController = nullptr;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
        debugController->EnableDebugLayer();
        debugController->SetEnableGPUBasedValidation(TRUE);
    }
#endif

    IDXGIFactory7* dxgiFactory = nullptr;
    hr = CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory));
    assert(SUCCEEDED(hr));

    IDXGIAdapter4* useAdapter = nullptr;
    for (UINT i = 0; dxgiFactory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAdapter)) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC3 adapterDesc{};
        useAdapter->GetDesc3(&adapterDesc);
        if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) {
            Log(ConvertString(std::format(L"Use Adapter:{}\n", adapterDesc.Description)));
            break;
        }
        useAdapter->Release();
        useAdapter = nullptr;
    }
    assert(useAdapter != nullptr);

    ID3D12Device* device = nullptr;
    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0 };
    for (auto level : featureLevels) {
        if (SUCCEEDED(D3D12CreateDevice(useAdapter, level, IID_PPV_ARGS(&device)))) break;
    }
    assert(device != nullptr);
    Log("Complete create D3D12Device!!!\n");

#ifdef _DEBUG
    ID3D12InfoQueue* infoQueue = nullptr;
    if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);
        D3D12_MESSAGE_ID denyIds[] = { D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE };
        D3D12_MESSAGE_SEVERITY severities[] = { D3D12_MESSAGE_SEVERITY_INFO };
        D3D12_INFO_QUEUE_FILTER filter{};
        filter.DenyList.NumIDs = _countof(denyIds);
        filter.DenyList.pIDList = denyIds;
        filter.DenyList.NumSeverities = _countof(severities);
        filter.DenyList.pSeverityList = severities;
        infoQueue->PushStorageFilter(&filter);
        infoQueue->Release();
    }
#endif

    ID3D12CommandQueue* commandQueue = nullptr;
    D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
    hr = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue));
    assert(SUCCEEDED(hr));

    ID3D12CommandAllocator* commandAllocator = nullptr;
    hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));
    assert(SUCCEEDED(hr));

    ID3D12GraphicsCommandList* commandList = nullptr;
    hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator, nullptr, IID_PPV_ARGS(&commandList));
    assert(SUCCEEDED(hr));
    commandList->Close();

    IDXGISwapChain4* swapChain = nullptr;
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
    swapChainDesc.Width = kClientWidth;
    swapChainDesc.Height = kClientHeight;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = 2;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    hr = dxgiFactory->CreateSwapChainForHwnd(commandQueue, hwnd, &swapChainDesc, nullptr, nullptr, reinterpret_cast<IDXGISwapChain1**>(&swapChain));
    assert(SUCCEEDED(hr));

    ID3D12DescriptorHeap* rtvDescriptorHeap = nullptr;
    D3D12_DESCRIPTOR_HEAP_DESC rtvDescriptorHeapDesc{};
    rtvDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDescriptorHeapDesc.NumDescriptors = 2;
    hr = device->CreateDescriptorHeap(&rtvDescriptorHeapDesc, IID_PPV_ARGS(&rtvDescriptorHeap));
    assert(SUCCEEDED(hr));

    ID3D12Resource* swapChainResources[2] = { nullptr };
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2];
    UINT rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtvStartHandle = rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

    for (UINT i = 0; i < 2; ++i) {
        hr = swapChain->GetBuffer(i, IID_PPV_ARGS(&swapChainResources[i]));
        assert(SUCCEEDED(hr));
        rtvHandles[i].ptr = rtvStartHandle.ptr + (i * rtvDescriptorSize);
        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
        rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        device->CreateRenderTargetView(swapChainResources[i], &rtvDesc, rtvHandles[i]);
    }

    ID3D12Fence* fence = nullptr;
    uint64_t fenceValue = 0;
    hr = device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    assert(SUCCEEDED(hr));
    HANDLE fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    assert(fenceEvent != nullptr);

    // ★ 変換行列・テクスチャを使う頂点シェーダー
    const char* vsCode = R"(
cbuffer TransformCB : register(b0) {
    float4x4 worldMatrix;
};
struct VSInput {
    float4 pos : POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
};
struct VSOutput {
    float4 pos : SV_POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
};
VSOutput main(VSInput input) {
    VSOutput output;
    output.pos = mul(input.pos, worldMatrix);
    output.color = input.color;
    output.uv = input.uv;
    return output;
}
)";

    // ★ テクスチャ＋マテリアルカラーを乗算するピクセルシェーダー
    const char* psCode = R"(
cbuffer MaterialCB : register(b1) {
    float4 materialColor;
};
Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);
struct PSInput {
    float4 pos : SV_POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
};
float4 main(PSInput input) : SV_TARGET {
    float4 texColor = gTexture.Sample(gSampler, input.uv);
    return texColor * materialColor * input.color;
}
)";

    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;

    hr = D3DCompile(vsCode, strlen(vsCode), nullptr, nullptr, nullptr,
        "main", "vs_5_0", D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION, 0, &vsBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) { OutputDebugStringA((char*)errorBlob->GetBufferPointer()); errorBlob->Release(); }
        assert(false);
    }
    hr = D3DCompile(psCode, strlen(psCode), nullptr, nullptr, nullptr,
        "main", "ps_5_0", D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION, 0, &psBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) { OutputDebugStringA((char*)errorBlob->GetBufferPointer()); errorBlob->Release(); }
        assert(false);
    }

    // ★ ルートシグネチャ：b0(World行列/VS), b1(マテリアルカラー/PS), t0(テクスチャ/PS) + 静的サンプラー
    ID3D12RootSignature* rootSignature = nullptr;

    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 0;
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParams[3]{};
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[0].Descriptor.ShaderRegister = 0; // b0 (World行列)
    rootParams[0].Descriptor.RegisterSpace = 0;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[1].Descriptor.ShaderRegister = 1; // b1 (マテリアルカラー)
    rootParams[1].Descriptor.RegisterSpace = 0;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[2].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[2].DescriptorTable.pDescriptorRanges = &srvRange;
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC staticSampler{};
    staticSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    staticSampler.MaxLOD = D3D12_FLOAT32_MAX;
    staticSampler.ShaderRegister = 0; // s0
    staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc{};
    rootSigDesc.NumParameters = _countof(rootParams);
    rootSigDesc.pParameters = rootParams;
    rootSigDesc.NumStaticSamplers = 1;
    rootSigDesc.pStaticSamplers = &staticSampler;
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ID3DBlob* sigBlob = nullptr;
    hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errorBlob);
    assert(SUCCEEDED(hr));
    hr = device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
    assert(SUCCEEDED(hr));
    sigBlob->Release();

    // ★ TEXCOORDを追加した入力レイアウト
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = rootSignature;
    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;

    ID3D12PipelineState* pipelineState = nullptr;
    hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState));
    assert(SUCCEEDED(hr));

    // ★ 頂点カラーは常に白(1,1,1,1)
    struct Vertex { float pos[4]; float color[4]; float uv[2]; };

    // ★ 既存の大きい三角形
    Vertex vertices[] = {
        { {  0.0f,  0.5f, 0.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.5f, 0.0f } },
        { {  0.5f, -0.5f, 0.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f } },
        { { -0.5f, -0.5f, 0.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f } },
    };
    UINT vertexBufferSize = sizeof(vertices);

    // ★ 既存の小さい三角形（左下・右下のZ=0.1f）
    Vertex verticesSmall[] = {
        { {  0.0f,  0.5f, 0.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.5f, 0.0f } },
        { {  0.5f, -0.5f, 0.1f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f } },
        { { -0.5f, -0.5f, 0.1f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f } },
    };
    UINT vertexBufferSizeSmall = sizeof(verticesSmall);

    // ★ スプライト用頂点（四角形 = 2Triangle、NDC空間で幅0.4・高さ0.4のQuad）
    //    左上(-0.2, 0.2) 右上(0.2, 0.2) 左下(-0.2,-0.2) 右下(0.2,-0.2)
    Vertex verticesSprite[] = {
        { { -0.2f,  0.2f, 0.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f } }, // 左上
        { {  0.2f,  0.2f, 0.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f } }, // 右上
        { { -0.2f, -0.2f, 0.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f } }, // 左下
        { {  0.2f,  0.2f, 0.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f } }, // 右上
        { {  0.2f, -0.2f, 0.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f } }, // 右下
        { { -0.2f, -0.2f, 0.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f } }, // 左下
    };
    UINT vertexBufferSizeSprite = sizeof(verticesSprite);

    auto CreateUploadBuffer = [&](UINT64 size) -> ID3D12Resource* {
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = size;
        rd.Height = 1;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ID3D12Resource* res = nullptr;
        HRESULT r = device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&res));
        assert(SUCCEEDED(r));
        return res;
        };

    // ★ 既存 頂点バッファ
    ID3D12Resource* vertexBuffer = CreateUploadBuffer(vertexBufferSize);
    Vertex* mappedVertices = nullptr;
    vertexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mappedVertices));
    memcpy(mappedVertices, vertices, vertexBufferSize);

    D3D12_VERTEX_BUFFER_VIEW vbView{};
    vbView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
    vbView.SizeInBytes = vertexBufferSize;
    vbView.StrideInBytes = sizeof(Vertex);

    // ★ 既存 小さい三角形 頂点バッファ
    ID3D12Resource* vertexBufferSmall = CreateUploadBuffer(vertexBufferSizeSmall);
    Vertex* mappedVerticesSmall = nullptr;
    vertexBufferSmall->Map(0, nullptr, reinterpret_cast<void**>(&mappedVerticesSmall));
    memcpy(mappedVerticesSmall, verticesSmall, vertexBufferSizeSmall);

    D3D12_VERTEX_BUFFER_VIEW vbViewSmall{};
    vbViewSmall.BufferLocation = vertexBufferSmall->GetGPUVirtualAddress();
    vbViewSmall.SizeInBytes = vertexBufferSizeSmall;
    vbViewSmall.StrideInBytes = sizeof(Vertex);

    // ★ スプライト用 VertexResource & VBV（別途用意）
    ID3D12Resource* vertexBufferSprite = CreateUploadBuffer(vertexBufferSizeSprite);
    Vertex* mappedVerticesSprite = nullptr;
    vertexBufferSprite->Map(0, nullptr, reinterpret_cast<void**>(&mappedVerticesSprite));
    memcpy(mappedVerticesSprite, verticesSprite, vertexBufferSizeSprite);

    D3D12_VERTEX_BUFFER_VIEW vbViewSprite{};
    vbViewSprite.BufferLocation = vertexBufferSprite->GetGPUVirtualAddress();
    vbViewSprite.SizeInBytes = vertexBufferSizeSprite;
    vbViewSprite.StrideInBytes = sizeof(Vertex);

    // ★ 既存 三角形用 定数バッファ（回転行列）
    ID3D12Resource* constantBuffer = CreateUploadBuffer(sizeof(ConstantBufferData));
    ConstantBufferData* mappedCB = nullptr;
    constantBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mappedCB));

    // ★ スプライト用 TransformMatrix CBV（独立した定数バッファ）
    ID3D12Resource* spriteConstantBuffer = CreateUploadBuffer(sizeof(ConstantBufferData));
    ConstantBufferData* mappedSpriteCB = nullptr;
    spriteConstantBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mappedSpriteCB));

    // ★ マテリアルカラー用の定数バッファ（共通・白）
    ID3D12Resource* materialBuffer = CreateUploadBuffer(sizeof(MaterialCBData));
    MaterialCBData* mappedMaterial = nullptr;
    materialBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mappedMaterial));
    mappedMaterial->color[0] = 1.0f;
    mappedMaterial->color[1] = 1.0f;
    mappedMaterial->color[2] = 1.0f;
    mappedMaterial->color[3] = 1.0f;

    D3D12_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(kClientWidth);
    viewport.Height = static_cast<float>(kClientHeight);
    viewport.MaxDepth = 1.0f;
    D3D12_RECT scissorRect{};
    scissorRect.right = kClientWidth;
    scissorRect.bottom = kClientHeight;

    // ★ テクスチャ用リソースの作成（チェッカー柄、256x256 RGBA8）
    const UINT kTexWidth = 256;
    const UINT kTexHeight = 256;
    const UINT kCellSize = 32;
    std::vector<uint8_t> textureData(static_cast<size_t>(kTexWidth) * kTexHeight * 4);
    for (UINT y = 0; y < kTexHeight; ++y) {
        for (UINT x = 0; x < kTexWidth; ++x) {
            bool checker = ((x / kCellSize) % 2) ^ ((y / kCellSize) % 2);
            uint8_t v = checker ? 255 : 180;
            size_t idx = (static_cast<size_t>(y) * kTexWidth + x) * 4;
            textureData[idx + 0] = v;
            textureData[idx + 1] = v;
            textureData[idx + 2] = v;
            textureData[idx + 3] = 255;
        }
    }

    D3D12_HEAP_PROPERTIES texHeapProps{};
    texHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC texResDesc{};
    texResDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texResDesc.Width = kTexWidth;
    texResDesc.Height = kTexHeight;
    texResDesc.DepthOrArraySize = 1;
    texResDesc.MipLevels = 1;
    texResDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texResDesc.SampleDesc.Count = 1;
    texResDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    ID3D12Resource* textureResource = nullptr;
    hr = device->CreateCommittedResource(&texHeapProps, D3D12_HEAP_FLAG_NONE, &texResDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&textureResource));
    assert(SUCCEEDED(hr));

    UINT64 uploadBufferSize = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT texFootprint{};
    device->GetCopyableFootprints(&texResDesc, 0, 1, 0, &texFootprint, nullptr, nullptr, &uploadBufferSize);

    ID3D12Resource* textureUploadHeap = CreateUploadBuffer(uploadBufferSize);
    uint8_t* mappedUpload = nullptr;
    textureUploadHeap->Map(0, nullptr, reinterpret_cast<void**>(&mappedUpload));
    for (UINT y = 0; y < kTexHeight; ++y) {
        memcpy(mappedUpload + y * texFootprint.Footprint.RowPitch,
            textureData.data() + static_cast<size_t>(y) * kTexWidth * 4, static_cast<size_t>(kTexWidth) * 4);
    }
    textureUploadHeap->Unmap(0, nullptr);

    // ★ テクスチャアップロード
    commandList->Reset(commandAllocator, nullptr);
    {
        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = textureResource;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = textureUploadHeap;
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = texFootprint;
        commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

        D3D12_RESOURCE_BARRIER texBarrier{};
        texBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        texBarrier.Transition.pResource = textureResource;
        texBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        texBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        texBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandList->ResourceBarrier(1, &texBarrier);
    }
    commandList->Close();
    {
        ID3D12CommandList* initCommandLists[] = { commandList };
        commandQueue->ExecuteCommandLists(1, initCommandLists);
    }
    fenceValue++;
    commandQueue->Signal(fence, fenceValue);
    if (fence->GetCompletedValue() < fenceValue) {
        fence->SetEventOnCompletion(fenceValue, fenceEvent);
        WaitForSingleObject(fenceEvent, INFINITE);
    }
    textureUploadHeap->Release();
    textureUploadHeap = nullptr;

    commandAllocator->Reset();
    commandList->Reset(commandAllocator, nullptr);

    // ★ SRVヒープ：0番=テクスチャ、1番=ImGuiフォント
    ID3D12DescriptorHeap* srvDescriptorHeap = nullptr;
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc{};
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
#ifdef USE_IMGUI
    srvHeapDesc.NumDescriptors = 2;
#else
    srvHeapDesc.NumDescriptors = 1;
#endif
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&srvDescriptorHeap));
    assert(SUCCEEDED(hr));

    UINT srvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE srvHeapCpuStart = srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE srvHeapGpuStart = srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = texResDesc.Format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(textureResource, &srvDesc, srvHeapCpuStart);
    D3D12_GPU_DESCRIPTOR_HANDLE textureGpuHandle = srvHeapGpuStart;

#ifdef USE_IMGUI
    D3D12_CPU_DESCRIPTOR_HANDLE imguiFontCpuHandle{ srvHeapCpuStart.ptr + srvDescriptorSize };
    D3D12_GPU_DESCRIPTOR_HANDLE imguiFontGpuHandle{ srvHeapGpuStart.ptr + srvDescriptorSize };
#endif

#ifdef USE_IMGUI
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX12_Init(
        device, 2,
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
        srvDescriptorHeap,
        imguiFontCpuHandle,
        imguiFontGpuHandle
    );
#endif

    // ★ マテリアルカラー（共通、ImGuiで変更可能）
    float materialColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    // ★ 三角形の回転
    float rotationSpeed = 1.0f;
    float rotationAngle = 0.0f;
    // ★ スプライト用 Transform（CPUで保持）
    SpriteTransform spriteTransform{};

    auto prevTime = std::chrono::steady_clock::now();

    MSG msg{};
    while (msg.message != WM_QUIT) {
        if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else {
            auto now = std::chrono::steady_clock::now();
            float deltaTime = std::chrono::duration<float>(now - prevTime).count();
            prevTime = now;

            rotationAngle += rotationSpeed * deltaTime;

#ifdef USE_IMGUI
            ImGui_ImplDX12_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            ImGui::Begin("Settings");
            ImGui::ColorEdit4("Material Color", materialColor);
            ImGui::SliderFloat("Rotation Speed", &rotationSpeed, -5.0f, 5.0f);
            ImGui::Text("Angle: %.2f rad", rotationAngle);
            ImGui::Separator();
            // ★ スプライトのTranslateをImGuiで操作
            ImGui::Text("Sprite Transform");
            ImGui::SliderFloat("translateX", &spriteTransform.translate[0], -1.0f, 1.0f);
            ImGui::SliderFloat("translateY", &spriteTransform.translate[1], -1.0f, 1.0f);
            ImGui::SliderFloat("translateZ", &spriteTransform.translate[2], -1.0f, 1.0f);
            ImGui::End();

            ImGui::Render();
#endif

            // ★ 三角形の定数バッファに回転行列を書き込む
            mappedCB->worldMatrix = MakeRotationY(rotationAngle);

            // ★ スプライトの定数バッファにTranslate行列を書き込む
            mappedSpriteCB->worldMatrix = MakeTranslation(
                spriteTransform.translate[0],
                spriteTransform.translate[1],
                spriteTransform.translate[2]
            );

            // ★ マテリアルカラーを書き込む
            mappedMaterial->color[0] = materialColor[0];
            mappedMaterial->color[1] = materialColor[1];
            mappedMaterial->color[2] = materialColor[2];
            mappedMaterial->color[3] = materialColor[3];

            UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();

            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barrier.Transition.pResource = swapChainResources[backBufferIndex];
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            commandList->ResourceBarrier(1, &barrier);

            commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], false, nullptr);
            float clearColor[] = { 0.1f, 0.25f, 0.5f, 1.0f };
            commandList->ClearRenderTargetView(rtvHandles[backBufferIndex], clearColor, 0, nullptr);

            ID3D12DescriptorHeap* heaps[] = { srvDescriptorHeap };
            commandList->SetDescriptorHeaps(1, heaps);
            commandList->SetGraphicsRootSignature(rootSignature);
            commandList->SetGraphicsRootConstantBufferView(1, materialBuffer->GetGPUVirtualAddress());
            commandList->SetGraphicsRootDescriptorTable(2, textureGpuHandle);
            commandList->SetPipelineState(pipelineState);
            commandList->RSSetViewports(1, &viewport);
            commandList->RSSetScissorRects(1, &scissorRect);
            commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            // ★ 大きい三角形（回転行列CBV）
            commandList->SetGraphicsRootConstantBufferView(0, constantBuffer->GetGPUVirtualAddress());
            commandList->IASetVertexBuffers(0, 1, &vbView);
            commandList->DrawInstanced(3, 1, 0, 0);

            // ★ 小さい三角形（同じ回転行列CBV）
            commandList->IASetVertexBuffers(0, 1, &vbViewSmall);
            commandList->DrawInstanced(3, 1, 0, 0);

            // ★ スプライト描画（独立したTranslate CBV & VBV）
            commandList->SetGraphicsRootConstantBufferView(0, spriteConstantBuffer->GetGPUVirtualAddress());
            commandList->IASetVertexBuffers(0, 1, &vbViewSprite);
            commandList->DrawInstanced(6, 1, 0, 0);

#ifdef USE_IMGUI
            ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
#endif

            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
            commandList->ResourceBarrier(1, &barrier);

            commandList->Close();
            ID3D12CommandList* commandLists[] = { commandList };
            commandQueue->ExecuteCommandLists(1, commandLists);

            swapChain->Present(1, 0);

            fenceValue++;
            commandQueue->Signal(fence, fenceValue);
            if (fence->GetCompletedValue() < fenceValue) {
                fence->SetEventOnCompletion(fenceValue, fenceEvent);
                WaitForSingleObject(fenceEvent, INFINITE);
            }

            commandAllocator->Reset();
            commandList->Reset(commandAllocator, nullptr);
        }
    }

#ifdef USE_IMGUI
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
#endif

    spriteConstantBuffer->Unmap(0, nullptr);
    if (spriteConstantBuffer) spriteConstantBuffer->Release();
    materialBuffer->Unmap(0, nullptr);
    if (materialBuffer) materialBuffer->Release();
    if (textureResource) textureResource->Release();
    constantBuffer->Unmap(0, nullptr);
    if (constantBuffer) constantBuffer->Release();
    vertexBufferSprite->Unmap(0, nullptr);
    if (vertexBufferSprite) vertexBufferSprite->Release();
    vertexBufferSmall->Unmap(0, nullptr);
    if (vertexBufferSmall) vertexBufferSmall->Release();
    vertexBuffer->Unmap(0, nullptr);
    if (vertexBuffer) vertexBuffer->Release();
    if (pipelineState) pipelineState->Release();
    if (rootSignature) rootSignature->Release();
    if (vsBlob) vsBlob->Release();
    if (psBlob) psBlob->Release();
    CloseHandle(fenceEvent);
    if (fence) fence->Release();
    if (srvDescriptorHeap) srvDescriptorHeap->Release();
    if (rtvDescriptorHeap) rtvDescriptorHeap->Release();
    if (swapChainResources[0]) swapChainResources[0]->Release();
    if (swapChainResources[1]) swapChainResources[1]->Release();
    if (swapChain) swapChain->Release();
    if (commandList) commandList->Release();
    if (commandAllocator) commandAllocator->Release();
    if (commandQueue) commandQueue->Release();
    if (device) device->Release();
    if (useAdapter) useAdapter->Release();
    if (dxgiFactory) dxgiFactory->Release();

#ifdef _DEBUG
    if (debugController) debugController->Release();
    IDXGIDebug1* debug;
    if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)))) {
        debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
        debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
        debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
        debug->Release();
    }
#endif

    CloseWindow(hwnd);
    return 0;
}