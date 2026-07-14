#include <Windows.h>
#include <cstdint>
#include <string>
#include <format>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cassert>
#include <dxcapi.h>
#pragma comment(lib, "dxcompiler.lib")
#include <dxgidebug.h>
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#include "externals/DirectXTex/DirectXTex.h"
#include "externals/DirectXTex/d3dx12.h"
#include <vector>
#include <wrl.h>
#include "Vector3.h"
#include "Matrix4x4.h"
#include "Transform.h"
#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

struct Vector4 {
    float x, y, z, w;
};

//texCoord・法線付き頂点データ 
struct VertexData {
    Vector4 position;
    struct { float x, y; } texCoord;
    Vector3 normal;
};

struct Material {
    Vector4 color;
    int32_t enableLighting;
    float padding[3];
};

struct DirectionalLight {
    Vector4 color;     // ライトの色
    Vector3 direction; // ライトの向き
    float intensity;   // 輝度
};

struct TransformationMatrix {
    Matrix4x4 WVP;
    Matrix4x4 World;
};

std::wstring ConvertString(const std::string& str) {
    if (str.empty()) return {};
    auto sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), NULL, 0);
    std::wstring result(sizeNeeded, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), sizeNeeded);
    return result;
}

std::string ConvertString(const std::wstring& wstr) {
    if (wstr.empty()) return {};
    auto sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), NULL, 0, NULL, NULL);
    std::string result(sizeNeeded, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), result.data(), sizeNeeded, NULL, NULL);
    return result;
}

void Log(const std::string& message) {
    OutputDebugStringA(message.c_str());
}

ID3D12Resource* CreateBufferResource(ID3D12Device* device, size_t sizeInBytes) {
    D3D12_HEAP_PROPERTIES uploadHeapProperties{ D3D12_HEAP_TYPE_UPLOAD }; // Heapの設定
    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Width = sizeInBytes;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ID3D12Resource* resource = nullptr;
    device->CreateCommittedResource(
        &uploadHeapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&resource)
    );
    return resource;
}

ID3D12DescriptorHeap* CreateDescriptorHeap(
    ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE heapType,
    UINT numDescriptors, bool shaderVisible)
{
    ID3D12DescriptorHeap* descriptorHeap = nullptr;
    D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
    descriptorHeapDesc.Type = heapType;
    descriptorHeapDesc.NumDescriptors = numDescriptors;
    if (shaderVisible) {
        descriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    }
    else {
        descriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    }
    HRESULT hr = device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&descriptorHeap));
    assert(SUCCEEDED(hr));
    return descriptorHeap;
}


//Textureデータを読む
DirectX::ScratchImage LoadTexture(const std::string& filePath) {
    // テクスチャファイルを読む
    DirectX::ScratchImage image{};
    std::wstring filePathW = ConvertString(filePath);
    HRESULT hr = DirectX::LoadFromWICFile(filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
    assert(SUCCEEDED(hr));

    // ミップマップを作成
    DirectX::ScratchImage mipImages{};
    hr = DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::TEX_FILTER_SRGB, 0, mipImages);
    assert(SUCCEEDED(hr));

    // ミップマップ付きデータを返す
    return mipImages;
}

//DirectX12のTextureResourceを作る
ID3D12Resource* CreateTextureResource(ID3D12Device* device, const DirectX::TexMetadata& metadata) {
    // metadataを基にResourceの設定
    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Width = UINT(metadata.width);
    resourceDesc.Height = UINT(metadata.height);
    resourceDesc.MipLevels = UINT16(metadata.mipLevels);
    resourceDesc.DepthOrArraySize = UINT16(metadata.arraySize);
    resourceDesc.Format = metadata.format;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION(metadata.dimension);

    // Heapの設定
    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

    // Resourceを生成
    ID3D12Resource* resource = nullptr;
    HRESULT hr = device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, // データ転送される設定
        nullptr,
        IID_PPV_ARGS(&resource));
    assert(SUCCEEDED(hr));
    return resource;
}

// DepthStencilTexture用のResourceを作る
ID3D12Resource* CreateDepthStencilTextureResource(ID3D12Device* device, int32_t width, int32_t height) {
    // Resourceの設定
    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Width = width;                                   // Textureの幅
    resourceDesc.Height = height;                                 // Textureの高さ
    resourceDesc.MipLevels = 1;                                   // mipmapの数
    resourceDesc.DepthOrArraySize = 1;                            // 奥行き or 配列Textureの配列数
    resourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;          // DepthStencilとして利用可能なフォーマット
    resourceDesc.SampleDesc.Count = 1;                            // サンプリングカウント。1固定。
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;  // 2次元
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL; // DepthStencilとして使う通知

    // Heapの設定
    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT; // VRAM上に作る

    // クリア設定
    D3D12_CLEAR_VALUE depthClearValue{};
    depthClearValue.DepthStencil.Depth = 1.0f;              // 最大値でクリア
    depthClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // フォーマット。Resourceと合わせる

    // Resourceを生成
    ID3D12Resource* resource = nullptr;
    HRESULT hr = device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, // 深度値を書き込む状態にしておく
        &depthClearValue, IID_PPV_ARGS(&resource));
    assert(SUCCEEDED(hr));
    return resource;
}

// データをVRAMに転送する。IntermediateResourceは転送完了まで呼び出し元で保持すること
[[nodiscard]]
ID3D12Resource* UploadTextureData(ID3D12Resource* texture, const DirectX::ScratchImage& mipImages,
    ID3D12Device* device, ID3D12GraphicsCommandList* commandList)
{
    // SubresourceDataを作成
    std::vector<D3D12_SUBRESOURCE_DATA> subresources;
    DirectX::PrepareUpload(device, mipImages.GetImages(), mipImages.GetImageCount(), mipImages.GetMetadata(), subresources);

    // IntermediateResourceのサイズを計算
    uint64_t intermediateSize = GetRequiredIntermediateSize(texture, 0, UINT(subresources.size()));

    // IntermediateResourceを作成
    ID3D12Resource* intermediateResource = CreateBufferResource(device, intermediateSize);

    // 転送コマンドを積む
    UpdateSubresources(commandList, texture, intermediateResource, 0, 0, UINT(subresources.size()), subresources.data());

    // ResourceStateをGENERIC_READへ変更
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = texture;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
    commandList->ResourceBarrier(1, &barrier);

    return intermediateResource;
}

D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index)
{
    D3D12_CPU_DESCRIPTOR_HANDLE handleCPU = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
    handleCPU.ptr += (descriptorSize * index);
    return handleCPU;
}

D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index)
{
    D3D12_GPU_DESCRIPTOR_HANDLE handleGPU = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
    handleGPU.ptr += (descriptorSize * index);
    return handleGPU;
}

IDxcBlob* CompileShader(
    const std::wstring& filePath,
    const wchar_t* profile,
    IDxcUtils* dxcUtils,
    IDxcCompiler3* dxcCompiler,
    IDxcIncludeHandler* includeHandler)
{
    Log(ConvertString(std::format(L"Begin CompileShader, path:{}, profile:{}\n", filePath, profile)));

    IDxcBlobEncoding* shaderSource = nullptr;
    HRESULT hr = dxcUtils->LoadFile(filePath.c_str(), nullptr, &shaderSource);
    assert(SUCCEEDED(hr));

    DxcBuffer shaderSourceBuffer;
    shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
    shaderSourceBuffer.Size = shaderSource->GetBufferSize();
    shaderSourceBuffer.Encoding = DXC_CP_UTF8;

    LPCWSTR arguments[] = {
        filePath.c_str(), L"-E", L"main", L"-T", profile,
        L"-Zi", L"-Qembed_debug", L"-Od", L"-Zpr",
    };

    IDxcResult* shaderResult = nullptr;
    hr = dxcCompiler->Compile(&shaderSourceBuffer, arguments, _countof(arguments), includeHandler, IID_PPV_ARGS(&shaderResult));
    assert(SUCCEEDED(hr));

    IDxcBlobUtf8* shaderError = nullptr;
    shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);
    if (shaderError != nullptr && shaderError->GetStringLength() != 0) {
        Log(shaderError->GetStringPointer());
        assert(false);
    }

    IDxcBlob* shaderBlob = nullptr;
    hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
    assert(SUCCEEDED(hr));
    Log(ConvertString(std::format(L"Compile Succeeded, path:{}, profile:{}\n", filePath, profile)));
    shaderSource->Release();
    shaderResult->Release();
    return shaderBlob;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
#ifdef USE_IMGUI
    if (ImGui_ImplWin32_WndProcHandler(hwnd, uMsg, wParam, lParam)) {
        return true;
    }
#endif
    if (uMsg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {

    //COMの初期化
    HRESULT hrCom = CoInitializeEx(0, COINIT_MULTITHREADED);
    assert(SUCCEEDED(hrCom));

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
    ShowWindow(hwnd, SW_SHOW);

    ID3D12Debug1* debugController = nullptr;
#ifdef _DEBUG
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
        debugController->EnableDebugLayer();
        // GPUベース検証はVRAMが少ない環境や一部ドライバでデバイス生成自体を失敗させることがあるため、
        // まずは無効化してデバイス生成が通るか確認する
        // debugController->SetEnableGPUBasedValidation(TRUE);
    }
#endif

    IDXGIFactory7* dxFactory = nullptr;
    HRESULT hr = CreateDXGIFactory(IID_PPV_ARGS(&dxFactory));
    assert(SUCCEEDED(hr));

    IDXGIAdapter4* useAdapter = nullptr;
    for (UINT i = 0; dxFactory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAdapter)) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC3 adapterDesc{};
        useAdapter->GetDesc3(&adapterDesc);
        if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) break;
        useAdapter->Release();
    }
    // アダプタが見つからなかった場合はここで気付けるようにする
    assert(useAdapter != nullptr);

    ID3D12Device* device = nullptr;
    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0 };
    for (auto level : featureLevels) {
        HRESULT hrDevice = D3D12CreateDevice(useAdapter, level, IID_PPV_ARGS(&device));
        if (SUCCEEDED(hrDevice)) {
            Log(ConvertString(std::format(L"FeatureLevel : {}\n", (int)level)));
            break;
        }
    }
    // ここでdeviceがnullptrなら、対応FeatureLevelのデバイス生成に全て失敗している
    assert(device != nullptr);

#ifdef _DEBUG
    ID3D12InfoQueue* infoQueue = nullptr;
    if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
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

    IDXGISwapChain4* swapChain = nullptr;
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
    swapChainDesc.Width = kClientWidth;
    swapChainDesc.Height = kClientHeight;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = 2;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    hr = dxFactory->CreateSwapChainForHwnd(commandQueue, hwnd, &swapChainDesc, nullptr, nullptr, reinterpret_cast<IDXGISwapChain1**>(&swapChain));
    assert(SUCCEEDED(hr));

    ID3D12DescriptorHeap* rtvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);
    ID3D12DescriptorHeap* srvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true);

    ID3D12Resource* swapChainResources[2] = {};
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2];
    // DescriptorSize を取得
    const uint32_t desriptorSizeSRV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const uint32_t desriptorSizeRTV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    const uint32_t desriptorSizeDSV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    UINT rtvDescriptorSize = desriptorSizeRTV;
    D3D12_CPU_DESCRIPTOR_HANDLE rtvStartHandle = rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < 2; ++i) {
        swapChain->GetBuffer(i, IID_PPV_ARGS(&swapChainResources[i]));
        rtvHandles[i].ptr = rtvStartHandle.ptr + (i * rtvDescriptorSize);
        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
        rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        device->CreateRenderTargetView(swapChainResources[i], &rtvDesc, rtvHandles[i]);
    }

    ID3D12Fence* fence = nullptr;
    uint64_t fenceValue = 0;
    device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    HANDLE fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    IDxcUtils* dxcUtils = nullptr;
    IDxcCompiler3* dxcCompiler = nullptr;
    DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
    DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
    IDxcIncludeHandler* includeHandler = nullptr;
    dxcUtils->CreateDefaultIncludeHandler(&includeHandler);

    // DescriptorRange
    D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
    descriptorRange[0].BaseShaderRegister = 0;  // t0から始まる
    descriptorRange[0].NumDescriptors = 1;       // 数は1つ
    descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; // SRVを使う
    descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND; // Offsetを自動計算

    // RootParameter
    D3D12_ROOT_PARAMETER rootParameters[4] = {};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[0].Descriptor.ShaderRegister = 0;

    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[1].Descriptor.ShaderRegister = 0;

    // DescriptorTable
    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRange;
    rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange);

    // DirectionalLight (b1, PixelShaderで使用)
    rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[3].Descriptor.ShaderRegister = 1;

    // StaticSampler
    D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
    staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR; // バイリニアフィルタ
    staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER; // 比較しない
    staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[0].ShaderRegister = 0;
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使う

    D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
    descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    descriptionRootSignature.pParameters = rootParameters;
    descriptionRootSignature.NumParameters = _countof(rootParameters);
    descriptionRootSignature.pStaticSamplers = staticSamplers;
    descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);

    ID3DBlob* signatureBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;
    hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
    assert(SUCCEEDED(hr));
    ID3D12RootSignature* rootSignature = nullptr;
    device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));

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

    IDxcBlob* vertexShaderBlob = CompileShader(L"Object3d.VS.hlsl", L"vs_6_0", dxcUtils, dxcCompiler, includeHandler);
    IDxcBlob* pixelShaderBlob = CompileShader(L"Object3d.PS.hlsl", L"ps_6_0", dxcUtils, dxcCompiler, includeHandler);

    // DepthStencilState
    D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
    depthStencilDesc.DepthEnable = true;                                   // Depthの機能を有効化する
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;          // 書き込みします
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;         // 比較関数はLessEqual。つまり、近ければ描画される

    D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};
    graphicsPipelineStateDesc.pRootSignature = rootSignature;
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

    ID3D12PipelineState* graphicsPipelineState = nullptr;
    hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc, IID_PPV_ARGS(&graphicsPipelineState));
    assert(SUCCEEDED(hr));

    // 球の分割数
    const uint32_t kSubdivision = 16;
    const uint32_t kSphereVertexCount = kSubdivision * kSubdivision * 6;

    // 頂点バッファ
    ID3D12Resource* vertexResource = CreateBufferResource(device, sizeof(VertexData) * kSphereVertexCount);

    D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
    vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();
    vertexBufferView.SizeInBytes = sizeof(VertexData) * kSphereVertexCount;
    vertexBufferView.StrideInBytes = sizeof(VertexData);

    VertexData* vertexData = nullptr;
    vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));

    // 球の頂点データを生成
    {
        const float pi = 3.14159265358979323846f;
        // 経度1分割の角度
        const float kLonEvery = pi * 2.0f / float(kSubdivision);
        // 緯度1分割の角度
        const float kLatEvery = pi / float(kSubdivision);

        // 緯度ループ
        for (uint32_t latIndex = 0; latIndex < kSubdivision; ++latIndex) {
            float lat = -pi / 2.0f + kLatEvery * float(latIndex); // θ

            // 経度ループ
            for (uint32_t lonIndex = 0; lonIndex < kSubdivision; ++lonIndex) {
                float lon = lonIndex * kLonEvery; // φ

                uint32_t start = (latIndex * kSubdivision + lonIndex) * 6;

                float u = float(lonIndex) / float(kSubdivision);
                float v = 1.0f - float(latIndex) / float(kSubdivision);
                float uNext = float(lonIndex + 1) / float(kSubdivision);
                float vNext = 1.0f - float(latIndex + 1) / float(kSubdivision);

                // 頂点a
                vertexData[start + 0].position = {
                    std::cosf(lat) * std::cosf(lon),
                    std::sinf(lat),
                    std::cosf(lat) * std::sinf(lon),
                    1.0f
                };
                vertexData[start + 0].texCoord = { u, v };
                vertexData[start + 0].normal = {
                    vertexData[start + 0].position.x,
                    vertexData[start + 0].position.y,
                    vertexData[start + 0].position.z
                };

                // 頂点b
                vertexData[start + 1].position = {
                    std::cosf(lat + kLatEvery) * std::cosf(lon),
                    std::sinf(lat + kLatEvery),
                    std::cosf(lat + kLatEvery) * std::sinf(lon),
                    1.0f
                };
                vertexData[start + 1].texCoord = { u, vNext };
                vertexData[start + 1].normal = {
                    vertexData[start + 1].position.x,
                    vertexData[start + 1].position.y,
                    vertexData[start + 1].position.z
                };

                // 頂点c
                vertexData[start + 2].position = {
                    std::cosf(lat) * std::cosf(lon + kLonEvery),
                    std::sinf(lat),
                    std::cosf(lat) * std::sinf(lon + kLonEvery),
                    1.0f
                };
                vertexData[start + 2].texCoord = { uNext, v };
                vertexData[start + 2].normal = {
                    vertexData[start + 2].position.x,
                    vertexData[start + 2].position.y,
                    vertexData[start + 2].position.z
                };

                // 三角形2枚目
                vertexData[start + 3].position = vertexData[start + 2].position;
                vertexData[start + 3].texCoord = vertexData[start + 2].texCoord;
                vertexData[start + 3].normal = vertexData[start + 2].normal;

                vertexData[start + 4].position = vertexData[start + 1].position;
                vertexData[start + 4].texCoord = vertexData[start + 1].texCoord;
                vertexData[start + 4].normal = vertexData[start + 1].normal;

                // 頂点d
                vertexData[start + 5].position = {
                    std::cosf(lat + kLatEvery) * std::cosf(lon + kLonEvery),
                    std::sinf(lat + kLatEvery),
                    std::cosf(lat + kLatEvery) * std::sinf(lon + kLonEvery),
                    1.0f
                };
                vertexData[start + 5].texCoord = { uNext, vNext };
                vertexData[start + 5].normal = {
                    vertexData[start + 5].position.x,
                    vertexData[start + 5].position.y,
                    vertexData[start + 5].position.z
                };
            }
        }
    }
    vertexResource->Unmap(0, nullptr);

    ID3D12Resource* materialResource = CreateBufferResource(device, sizeof(Material));
    Material* materialData = nullptr;
    materialResource->Map(0, nullptr, reinterpret_cast<void**>(&materialData));
    materialData->color = { 1.0f, 1.0f, 1.0f, 1.0f }; // 白
    materialData->enableLighting = true;

    // Sprite用マテリアル（ライティングは適用しない）
    ID3D12Resource* materialResourceSprite = CreateBufferResource(device, sizeof(Material));
    Material* materialDataSprite = nullptr;
    materialResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&materialDataSprite));
    materialDataSprite->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    materialDataSprite->enableLighting = false;

    ID3D12Resource* wvpResource = CreateBufferResource(device, sizeof(TransformationMatrix));
    TransformationMatrix* transformationMatrixData = nullptr;
    wvpResource->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixData));
    transformationMatrixData->WVP = MakeIdentity4x4();
    transformationMatrixData->World = MakeIdentity4x4();

    // 平行光源用リソース
    ID3D12Resource* directionalLightResource = CreateBufferResource(device, sizeof(DirectionalLight));
    DirectionalLight* directionalLightData = nullptr;
    directionalLightResource->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData));
    directionalLightData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    directionalLightData->direction = { 0.0f, -1.0f, 0.0f };
    directionalLightData->intensity = 1.0f;

    Transform transform{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
    Transform cameraTransform{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -10.0f} };

    // Sprite用頂点リソース（重複を無くし、頂点4つだけにする）
    ID3D12Resource* vertexResourceSprite = CreateBufferResource(device, sizeof(VertexData) * 4);

    D3D12_VERTEX_BUFFER_VIEW vertexBufferViewSprite{};
    vertexBufferViewSprite.BufferLocation = vertexResourceSprite->GetGPUVirtualAddress();
    vertexBufferViewSprite.SizeInBytes = sizeof(VertexData) * 4;
    vertexBufferViewSprite.StrideInBytes = sizeof(VertexData);

    VertexData* vertexDataSprite = nullptr;
    vertexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&vertexDataSprite));
    // 頂点0：左下
    vertexDataSprite[0].position = { 0.0f, 360.0f, 0.0f, 1.0f };
    vertexDataSprite[0].texCoord = { 0.0f, 1.0f };
    vertexDataSprite[0].normal = { 0.0f, 0.0f, -1.0f };
    // 頂点1：左上
    vertexDataSprite[1].position = { 0.0f, 0.0f, 0.0f, 1.0f };
    vertexDataSprite[1].texCoord = { 0.0f, 0.0f };
    vertexDataSprite[1].normal = { 0.0f, 0.0f, -1.0f };
    // 頂点2：右下
    vertexDataSprite[2].position = { 640.0f, 360.0f, 0.0f, 1.0f };
    vertexDataSprite[2].texCoord = { 1.0f, 1.0f };
    vertexDataSprite[2].normal = { 0.0f, 0.0f, -1.0f };
    // 頂点3：右上
    vertexDataSprite[3].position = { 640.0f, 0.0f, 0.0f, 1.0f };
    vertexDataSprite[3].texCoord = { 1.0f, 0.0f };
    vertexDataSprite[3].normal = { 0.0f, 0.0f, -1.0f };
    vertexResourceSprite->Unmap(0, nullptr);

    // Sprite用インデックスリソース（三角形「0,1,2」と「1,3,2」を頂点インデックスで表現）
    ID3D12Resource* indexResourceSprite = CreateBufferResource(device, sizeof(uint32_t) * 6);

    D3D12_INDEX_BUFFER_VIEW indexBufferViewSprite{};
    indexBufferViewSprite.BufferLocation = indexResourceSprite->GetGPUVirtualAddress();
    indexBufferViewSprite.SizeInBytes = sizeof(uint32_t) * 6;
    indexBufferViewSprite.Format = DXGI_FORMAT_R32_UINT;

    uint32_t* indexDataSprite = nullptr;
    indexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&indexDataSprite));
    indexDataSprite[0] = 0; indexDataSprite[1] = 1; indexDataSprite[2] = 2; // 1枚目の三角形
    indexDataSprite[3] = 1; indexDataSprite[4] = 3; indexDataSprite[5] = 2; // 2枚目の三角形
    indexResourceSprite->Unmap(0, nullptr);

    // Sprite用TransformationMatrixリソース（VSのcbufferと同じ構造にする）
    ID3D12Resource* transformationMatrixResourceSprite = CreateBufferResource(device, sizeof(TransformationMatrix));
    TransformationMatrix* transformationMatrixDataSprite = nullptr;
    transformationMatrixResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixDataSprite));
    transformationMatrixDataSprite->WVP = MakeIdentity4x4();
    transformationMatrixDataSprite->World = MakeIdentity4x4();

    Transform transformSprite{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };

    bool useMonsterBall = true;

    D3D12_VIEWPORT viewport{ 0.0f, 0.0f, (float)kClientWidth, (float)kClientHeight, 0.0f, 1.0f };
    D3D12_RECT scissorRect{ 0, 0, kClientWidth, kClientHeight };

#ifdef USE_IMGUI
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX12_Init(device,
        swapChainDesc.BufferCount,
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
        srvDescriptorHeap,
        srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
        srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Build();
#endif

    // uvChecker読み込み
    DirectX::ScratchImage mipImages = LoadTexture("resources/uvChecker.png");
    const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
    ID3D12Resource* textureResource = CreateTextureResource(device, metadata);
    ID3D12Resource* intermediateResource = UploadTextureData(textureResource, mipImages, device, commandList);

    // monsterBall読み込み
    DirectX::ScratchImage mipImages2 = LoadTexture("resources/monsterBall.png");
    const DirectX::TexMetadata& metadata2 = mipImages2.GetMetadata();
    ID3D12Resource* textureResource2 = CreateTextureResource(device, metadata2);
    ID3D12Resource* intermediateResource2 = UploadTextureData(textureResource2, mipImages2, device, commandList);

    // CommandListを閉じてキック
    commandList->Close();
    ID3D12CommandList* commandLists[] = { commandList };
    commandQueue->ExecuteCommandLists(1, commandLists);

    // 実行完了を待つ
    fenceValue++;
    commandQueue->Signal(fence, fenceValue);
    if (fence->GetCompletedValue() < fenceValue) {
        fence->SetEventOnCompletion(fenceValue, fenceEvent);
        WaitForSingleObject(fenceEvent, INFINITE);
    }

    // allocatorとcommandListをリセット
    commandAllocator->Reset();
    commandList->Reset(commandAllocator, nullptr);

    // intermediateResourceを解放
    intermediateResource->Release();
    intermediateResource2->Release();

    // SRV作成(uvChecker)
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = metadata.format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);
    D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU = GetCPUDescriptorHandle(srvDescriptorHeap, desriptorSizeSRV, 1);
    D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU = GetGPUDescriptorHandle(srvDescriptorHeap, desriptorSizeSRV, 1);
    device->CreateShaderResourceView(textureResource, &srvDesc, textureSrvHandleCPU);

    // SRV作成(monsterBall) 
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc2{};
    srvDesc2.Format = metadata2.format;
    srvDesc2.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc2.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc2.Texture2D.MipLevels = UINT(metadata2.mipLevels);
    D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU2 = GetCPUDescriptorHandle(srvDescriptorHeap, desriptorSizeSRV, 2);
    D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU2 = GetGPUDescriptorHandle(srvDescriptorHeap, desriptorSizeSRV, 2);
    device->CreateShaderResourceView(textureResource2, &srvDesc2, textureSrvHandleCPU2);

    // DepthStencilTextureを作成
    ID3D12Resource* depthStencilResource = CreateDepthStencilTextureResource(device, kClientWidth, kClientHeight);

    // DSV用DescriptorHeap
    ID3D12DescriptorHeap* dsvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);

    // DSVの設定
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;          // Format。基本的にはResourceに合わせる
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;   // 2dTexture
    // DSVを作成
    device->CreateDepthStencilView(depthStencilResource, &dsvDesc, dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

    MSG msg{};
    while (msg.message != WM_QUIT) {
        if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else {
#ifdef USE_IMGUI
            ImGui_ImplDX12_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
#endif

            transform.rotate.y += 0.03f;

            Matrix4x4 worldMatrix = MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
            Matrix4x4 cameraMatrix = MakeAffineMatrix(cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);
            Matrix4x4 viewMatrix = Inverse(cameraMatrix);
            Matrix4x4 projectionMatrix = MakePerspectiveFovMatrix(
                0.45f,
                float(kClientWidth) / float(kClientHeight),
                0.1f,
                100.0f
            );
            Matrix4x4 worldViewProjectionMatrix = Multiply(worldMatrix, Multiply(viewMatrix, projectionMatrix));
            transformationMatrixData->WVP = worldViewProjectionMatrix;
            transformationMatrixData->World = worldMatrix;

            // Sprite用のWorldViewProjectionMatrixを作る
            Matrix4x4 worldMatrixSprite = MakeAffineMatrix(transformSprite.scale, transformSprite.rotate, transformSprite.translate);
            Matrix4x4 viewMatrixSprite = MakeIdentity4x4();
            Matrix4x4 projectionMatrixSprite = MakeOrthographicMatrix(0.0f, 0.0f, float(kClientWidth), float(kClientHeight), 0.0f, 100.0f);
            Matrix4x4 worldViewProjectionMatrixSprite = Multiply(worldMatrixSprite, Multiply(viewMatrixSprite, projectionMatrixSprite));
            transformationMatrixDataSprite->WVP = worldViewProjectionMatrixSprite;
            transformationMatrixDataSprite->World = worldMatrixSprite;

#ifdef USE_IMGUI
            ImGui::Begin("Settings");
            ImGui::ColorEdit4("MaterialColor", &materialData->color.x);
            ImGui::DragFloat3("translateSprite", &transformSprite.translate.x, 1.0f);
            ImGui::Checkbox("useMonsterBall", &useMonsterBall);
            bool enableLighting = materialData->enableLighting != 0;
            if (ImGui::Checkbox("enableLighting", &enableLighting)) {
                materialData->enableLighting = enableLighting ? 1 : 0;
            }
            ImGui::ColorEdit3("LightColor", &directionalLightData->color.x);
            ImGui::DragFloat3("LightDirection", &directionalLightData->direction.x, 0.01f, -1.0f, 1.0f);
            ImGui::DragFloat("LightIntensity", &directionalLightData->intensity, 0.01f, 0.0f, 10.0f);
            ImGui::End();
#endif

#ifdef USE_IMGUI
            ImGui::Render();
#endif

            UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();

            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = swapChainResources[backBufferIndex];
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            commandList->ResourceBarrier(1, &barrier);

            // RTVとDSVを設定
            D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
            commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], false, &dsvHandle);
            float clearColor[] = { 0.1f, 0.25f, 0.5f, 1.0f };
            commandList->ClearRenderTargetView(rtvHandles[backBufferIndex], clearColor, 0, nullptr);
            // 深度をクリア
            commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

#ifdef USE_IMGUI
            ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap };
            commandList->SetDescriptorHeaps(1, descriptorHeaps);
#endif

            commandList->RSSetViewports(1, &viewport);
            commandList->RSSetScissorRects(1, &scissorRect);
            commandList->SetGraphicsRootSignature(rootSignature);
            commandList->SetPipelineState(graphicsPipelineState);
            commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
            commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            commandList->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress());
            commandList->SetGraphicsRootConstantBufferView(1, wvpResource->GetGPUVirtualAddress());
            commandList->SetGraphicsRootConstantBufferView(3, directionalLightResource->GetGPUVirtualAddress());

            // 球用SRVを切り替えて設定
            commandList->SetGraphicsRootDescriptorTable(2, useMonsterBall ? textureSrvHandleGPU2 : textureSrvHandleGPU);
            commandList->DrawInstanced(kSphereVertexCount, 1, 0, 0);

            // Sprite描画（ライティングなしのマテリアルを使用）
            commandList->SetGraphicsRootConstantBufferView(0, materialResourceSprite->GetGPUVirtualAddress());
            commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU);
            commandList->IASetVertexBuffers(0, 1, &vertexBufferViewSprite);
            commandList->IASetIndexBuffer(&indexBufferViewSprite);
            commandList->SetGraphicsRootConstantBufferView(1, transformationMatrixResourceSprite->GetGPUVirtualAddress());
            commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);

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

    // textureResource解放
    textureResource->Release();
    textureResource2->Release();
    depthStencilResource->Release();
    dsvDescriptorHeap->Release();

    wvpResource->Release();
    materialResource->Release();
    materialResourceSprite->Release();
    directionalLightResource->Release();
    vertexResource->Release();
    transformationMatrixResourceSprite->Release();
    vertexResourceSprite->Release();
    indexResourceSprite->Release();
    graphicsPipelineState->Release();
    signatureBlob->Release();
    if (errorBlob) errorBlob->Release();
    rootSignature->Release();
    pixelShaderBlob->Release();
    vertexShaderBlob->Release();
    includeHandler->Release();
    dxcCompiler->Release();
    dxcUtils->Release();
    CloseHandle(fenceEvent);
    fence->Release();
    srvDescriptorHeap->Release();
    rtvDescriptorHeap->Release();
    swapChainResources[0]->Release();
    swapChainResources[1]->Release();
    swapChain->Release();
    commandList->Release();
    commandAllocator->Release();
    commandQueue->Release();
    device->Release();
    useAdapter->Release();
    dxFactory->Release();
    if (debugController) debugController->Release();

    // COMの終了処理 
    CoUninitialize();


    return 0;
}