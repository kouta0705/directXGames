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
#include <fstream>
#include <sstream>
#include <cstring>
#include "Vector3.h"
#include "Matrix4x4.h"
#include "Transform.h"
#include "SoundPlayer.h"
#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

using Microsoft::WRL::ComPtr;

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
    Matrix4x4 uvTransform;
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

// texcoord読み取り用の一時構造体
struct TexcoordTemp { float x, y; };

// objが参照するmtlの情報
struct MaterialData {
    std::string textureFilePath;
};

// objファイル全体の情報
struct ModelData {
    std::vector<VertexData> vertices;
    MaterialData material;
};

// 自動Resource解放マン：ComPtrで管理しきれないDXGI/D3D12のリソースリークを
// プログラム終了時にチェックしてくれるクラス。
// mainの先頭でインスタンス化することで、他の全てのComPtrがデストラクトされた
// 一番最後にDestructorが呼ばれ、正しくリーク検出ができる。
struct D3DResourceLeakChecker {
    ~D3DResourceLeakChecker() {
        // リソースリークチェック
        ComPtr<IDXGIDebug1> debug;
        if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)))) {
            debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
            debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
            debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
        }
    }
};

// mtlファイルを読む
MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename) {
    MaterialData materialData;                   // 構築するMaterialData
    std::string line;                             // ファイルから読んだ1行を格納するもの

    std::ifstream file(directoryPath + "/" + filename); // ファイルを開く
    assert(file.is_open());                        // とりあえず開けなかったら止める

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
    return materialData;
}

// objファイルを読む
ModelData LoadObjFile(const std::string& directoryPath, const std::string& filename) {
    ModelData modelData;                          // 構築するModelData
    std::vector<Vector4> positions;                // 位置
    std::vector<Vector3> normals;                  // 法線
    std::vector<TexcoordTemp> texcoords;           // テクスチャ座標
    std::string line;                              // ファイルから読んだ1行を格納するもの

    std::ifstream file(directoryPath + "/" + filename); // ファイルを開く
    assert(file.is_open());                        // とりあえず開けなかったら止める

    while (std::getline(file, line)) {
        std::string identifier;
        std::istringstream s(line);
        s >> identifier; // 先頭の識別子を読む

        // identifierに応じた処理
        if (identifier == "v") {
            Vector4 position;
            s >> position.x >> position.y >> position.z;
            position.x *= -1.0f; // 右手系->左手系変換のためxを反転
            position.w = 1.0f;
            positions.push_back(position);
        }
        else if (identifier == "vt") {
            TexcoordTemp texcoord;
            s >> texcoord.x >> texcoord.y;
            texcoord.y = 1.0f - texcoord.y; // Blenderは左下原点系のため、上下反転して左上原点系に合わせる
            texcoords.push_back(texcoord);
        }
        else if (identifier == "vn") {
            Vector3 normal;
            s >> normal.x >> normal.y >> normal.z;
            normal.x *= -1.0f; // 右手系->左手系変換のためxを反転
            normals.push_back(normal);
        }
        else if (identifier == "f") {
            VertexData triangle[3];
            // 面は三角形限定。その他の場合は未対応
            for (int32_t faceVertex = 0; faceVertex < 3; ++faceVertex) {
                std::string vertexDefinition;
                s >> vertexDefinition;
                // 頂点の要素へのindexは「位置/UV/法線」で格納されているので、分解してindexを取得する
                std::istringstream v(vertexDefinition);
                uint32_t elementIndices[3];
                for (int32_t element = 0; element < 3; ++element) {
                    std::string index;
                    std::getline(v, index, '/'); // /区切りでインデックスを読んでいく
                    elementIndices[element] = std::stoi(index);
                }
                // 要素へのindexから、実際の要素の値を取得して、頂点を構築する
                Vector4 position = positions[elementIndices[0] - 1];
                TexcoordTemp texcoord = texcoords[elementIndices[1] - 1];
                Vector3 normal = normals[elementIndices[2] - 1];
                triangle[faceVertex].position = position;
                triangle[faceVertex].texCoord = { texcoord.x, texcoord.y };
                triangle[faceVertex].normal = normal;
            }
            // 右手系->左手系変換のため、頂点の順番を逆にして面の向き（表裏）を合わせる
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
    return modelData;
}

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

// ★ComPtr化: device引数を const ComPtr<ID3D12Device>& に変更
ComPtr<ID3D12Resource> CreateBufferResource(const ComPtr<ID3D12Device>& device, size_t sizeInBytes) {
    D3D12_HEAP_PROPERTIES uploadHeapProperties{ D3D12_HEAP_TYPE_UPLOAD }; // Heapの設定
    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Width = sizeInBytes;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> resource;
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

// ★ComPtr化: device引数を const ComPtr<ID3D12Device>& に変更
ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(
    const ComPtr<ID3D12Device>& device, D3D12_DESCRIPTOR_HEAP_TYPE heapType,
    UINT numDescriptors, bool shaderVisible)
{
    ComPtr<ID3D12DescriptorHeap> descriptorHeap;
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

// ★ComPtr化: device引数を const ComPtr<ID3D12Device>& に変更
//DirectX12のTextureResourceを作る
ComPtr<ID3D12Resource> CreateTextureResource(const ComPtr<ID3D12Device>& device, const DirectX::TexMetadata& metadata) {
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
    ComPtr<ID3D12Resource> resource;
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

// ★ComPtr化: device引数を const ComPtr<ID3D12Device>& に変更
// DepthStencilTexture用のResourceを作る
ComPtr<ID3D12Resource> CreateDepthStencilTextureResource(const ComPtr<ID3D12Device>& device, int32_t width, int32_t height) {
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
    ComPtr<ID3D12Resource> resource;
    HRESULT hr = device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, // 深度値を書き込む状態にしておく
        &depthClearValue, IID_PPV_ARGS(&resource));
    assert(SUCCEEDED(hr));
    return resource;
}

// ★ComPtr化: texture / device / commandList をすべて const ComPtr<T>& に変更
// データをVRAMに転送する。IntermediateResourceは転送完了まで呼び出し元で保持すること
[[nodiscard]]
ComPtr<ID3D12Resource> UploadTextureData(
    const ComPtr<ID3D12Resource>& texture, const DirectX::ScratchImage& mipImages,
    const ComPtr<ID3D12Device>& device, const ComPtr<ID3D12GraphicsCommandList>& commandList)
{
    // SubresourceDataを作成
    std::vector<D3D12_SUBRESOURCE_DATA> subresources;
    // ※DirectXTex側のAPIは生ポインタしか受け取れないため、ここでのみ.Get()を使用する
    DirectX::PrepareUpload(device.Get(), mipImages.GetImages(), mipImages.GetImageCount(), mipImages.GetMetadata(), subresources);

    // IntermediateResourceのサイズを計算
    uint64_t intermediateSize = GetRequiredIntermediateSize(texture.Get(), 0, UINT(subresources.size()));

    // IntermediateResourceを作成
    ComPtr<ID3D12Resource> intermediateResource = CreateBufferResource(device, intermediateSize);

    // 転送コマンドを積む
    UpdateSubresources(commandList.Get(), texture.Get(), intermediateResource.Get(), 0, 0, UINT(subresources.size()), subresources.data());

    // ResourceStateをGENERIC_READへ変更
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = texture.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
    commandList->ResourceBarrier(1, &barrier);

    return intermediateResource;
}

// ★ComPtr化: descriptorHeap引数を const ComPtr<ID3D12DescriptorHeap>& に変更
D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(const ComPtr<ID3D12DescriptorHeap>& descriptorHeap, uint32_t descriptorSize, uint32_t index)
{
    D3D12_CPU_DESCRIPTOR_HANDLE handleCPU = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
    handleCPU.ptr += (descriptorSize * index);
    return handleCPU;
}

// ★ComPtr化: descriptorHeap引数を const ComPtr<ID3D12DescriptorHeap>& に変更
D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(const ComPtr<ID3D12DescriptorHeap>& descriptorHeap, uint32_t descriptorSize, uint32_t index)
{
    D3D12_GPU_DESCRIPTOR_HANDLE handleGPU = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
    handleGPU.ptr += (descriptorSize * index);
    return handleGPU;
}

// ★ComPtr化: dxcUtils / dxcCompiler / includeHandler をすべて const ComPtr<T>& に変更
ComPtr<IDxcBlob> CompileShader(
    const std::wstring& filePath,
    const wchar_t* profile,
    const ComPtr<IDxcUtils>& dxcUtils,
    const ComPtr<IDxcCompiler3>& dxcCompiler,
    const ComPtr<IDxcIncludeHandler>& includeHandler)
{
    Log(ConvertString(std::format(L"Begin CompileShader, path:{}, profile:{}\n", filePath, profile)));

    ComPtr<IDxcBlobEncoding> shaderSource;
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

    ComPtr<IDxcResult> shaderResult;
    // ※IDxcCompiler3::CompileのincludeHandler引数はAPI仕様で生ポインタのみ受け付けるため.Get()を使用
    hr = dxcCompiler->Compile(&shaderSourceBuffer, arguments, _countof(arguments), includeHandler.Get(), IID_PPV_ARGS(&shaderResult));
    assert(SUCCEEDED(hr));

    ComPtr<IDxcBlobUtf8> shaderError;
    shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);
    if (shaderError != nullptr && shaderError->GetStringLength() != 0) {
        Log(shaderError->GetStringPointer());
        assert(false);
    }

    ComPtr<IDxcBlob> shaderBlob;
    hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
    assert(SUCCEEDED(hr));
    Log(ConvertString(std::format(L"Compile Succeeded, path:{}, profile:{}\n", filePath, profile)));
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

    // 自動Resource解放マン。mainの先頭に置くことで一番最後にDestructorが呼ばれ、
    // すべてのComPtrの解放処理の後にリークチェックが行われるようにする
    D3DResourceLeakChecker leakCheck;

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

    ComPtr<ID3D12Debug1> debugController;
#ifdef _DEBUG
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
        debugController->EnableDebugLayer();
        // GPUベース検証はVRAMが少ない環境や一部ドライバでデバイス生成自体を失敗させることがあるため、
        // まずは無効化してデバイス生成が通るか確認する
        // debugController->SetEnableGPUBasedValidation(TRUE);
    }
#endif

    ComPtr<IDXGIFactory7> dxFactory;
    HRESULT hr = CreateDXGIFactory(IID_PPV_ARGS(&dxFactory));
    assert(SUCCEEDED(hr));

    ComPtr<IDXGIAdapter4> useAdapter;
    for (UINT i = 0; dxFactory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAdapter)) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC3 adapterDesc{};
        useAdapter->GetDesc3(&adapterDesc);
        if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) break;
        // 条件に合わなければ次のループでIID_PPV_ARGSが自動的に前のComPtrを解放して上書きする
    }
    // アダプタが見つからなかった場合はここで気付けるようにする
    assert(useAdapter != nullptr);

    ComPtr<ID3D12Device> device;
    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0 };
    for (auto level : featureLevels) {
        HRESULT hrDevice = D3D12CreateDevice(useAdapter.Get(), level, IID_PPV_ARGS(&device));
        if (SUCCEEDED(hrDevice)) {
            Log(ConvertString(std::format(L"FeatureLevel : {}\n", (int)level)));
            break;
        }
    }
    // ここでdeviceがnullptrなら、対応FeatureLevelのデバイス生成に全て失敗している
    assert(device != nullptr);

#ifdef _DEBUG
    ComPtr<ID3D12InfoQueue> infoQueue;
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
    }
#endif

    ComPtr<ID3D12CommandQueue> commandQueue;
    D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
    hr = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue));
    assert(SUCCEEDED(hr));

    ComPtr<ID3D12CommandAllocator> commandAllocator;
    hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));
    assert(SUCCEEDED(hr));

    ComPtr<ID3D12GraphicsCommandList> commandList;
    hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList));
    assert(SUCCEEDED(hr));

    ComPtr<IDXGISwapChain4> swapChain;
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
    swapChainDesc.Width = kClientWidth;
    swapChainDesc.Height = kClientHeight;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = 2;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    hr = dxFactory->CreateSwapChainForHwnd(commandQueue.Get(), hwnd, &swapChainDesc, nullptr, nullptr, reinterpret_cast<IDXGISwapChain1**>(swapChain.GetAddressOf()));
    assert(SUCCEEDED(hr));

    ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);
    ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true);

    ComPtr<ID3D12Resource> swapChainResources[2] = {};
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
        device->CreateRenderTargetView(swapChainResources[i].Get(), &rtvDesc, rtvHandles[i]);
    }

    ComPtr<ID3D12Fence> fence;
    uint64_t fenceValue = 0;
    device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    HANDLE fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    ComPtr<IDxcUtils> dxcUtils;
    ComPtr<IDxcCompiler3> dxcCompiler;
    DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
    DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
    ComPtr<IDxcIncludeHandler> includeHandler;
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

    ComPtr<ID3DBlob> signatureBlob;
    ComPtr<ID3DBlob> errorBlob;
    hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
    assert(SUCCEEDED(hr));
    ComPtr<ID3D12RootSignature> rootSignature;
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

    // ★修正: Half Lambertの実装が含まれる正しいシェーダーファイル名を指定
    ComPtr<IDxcBlob> vertexShaderBlob = CompileShader(L"Object3d_VS.hlsl", L"vs_6_0", dxcUtils, dxcCompiler, includeHandler);
    ComPtr<IDxcBlob> pixelShaderBlob = CompileShader(L"Object3d_PS.hlsl", L"ps_6_0", dxcUtils, dxcCompiler, includeHandler);

    // DepthStencilState
    D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
    depthStencilDesc.DepthEnable = true;                                   // Depthの機能を有効化する
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;          // 書き込みします
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;         // 比較関数はLessEqual。つまり、近ければ描画される

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

    ComPtr<ID3D12PipelineState> graphicsPipelineState;
    hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc, IID_PPV_ARGS(&graphicsPipelineState));
    assert(SUCCEEDED(hr));

    // objファイルの読み込み（resources/以下に配置したモデルを読む）
    ModelData modelData = LoadObjFile("resources", "plane.obj");
    const uint32_t kSphereVertexCount = static_cast<uint32_t>(modelData.vertices.size());

    // 頂点バッファ（objから読み込んだ頂点数ぶん確保する）
    ComPtr<ID3D12Resource> vertexResource = CreateBufferResource(device, sizeof(VertexData) * kSphereVertexCount);

    D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
    vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();
    vertexBufferView.SizeInBytes = sizeof(VertexData) * kSphereVertexCount;
    vertexBufferView.StrideInBytes = sizeof(VertexData);

    VertexData* vertexData = nullptr;
    vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
    // objファイルから読み込んだ頂点データをそのままVRAM用バッファへコピーする
    std::memcpy(vertexData, modelData.vertices.data(), sizeof(VertexData) * kSphereVertexCount);
    vertexResource->Unmap(0, nullptr);

    ComPtr<ID3D12Resource> materialResource = CreateBufferResource(device, sizeof(Material));
    Material* materialData = nullptr;
    materialResource->Map(0, nullptr, reinterpret_cast<void**>(&materialData));
    materialData->color = { 1.0f, 1.0f, 1.0f, 1.0f }; // 白
    materialData->enableLighting = true;
    materialData->uvTransform = MakeIdentity4x4();

    // Sprite用マテリアル（ライティングは適用しない）
    ComPtr<ID3D12Resource> materialResourceSprite = CreateBufferResource(device, sizeof(Material));
    Material* materialDataSprite = nullptr;
    materialResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&materialDataSprite));
    materialDataSprite->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    materialDataSprite->enableLighting = false;
    materialDataSprite->uvTransform = MakeIdentity4x4();

    // Sprite用UVTransform（CPU側のスケール・回転・平行移動）
    Transform uvTransformSprite{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };

    ComPtr<ID3D12Resource> wvpResource = CreateBufferResource(device, sizeof(TransformationMatrix));
    TransformationMatrix* transformationMatrixData = nullptr;
    wvpResource->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixData));
    transformationMatrixData->WVP = MakeIdentity4x4();
    transformationMatrixData->World = MakeIdentity4x4();

    // 平行光源用リソース
    ComPtr<ID3D12Resource> directionalLightResource = CreateBufferResource(device, sizeof(DirectionalLight));
    DirectionalLight* directionalLightData = nullptr;
    directionalLightResource->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData));
    directionalLightData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    directionalLightData->direction = { 0.0f, -1.0f, 0.0f };
    directionalLightData->intensity = 1.0f;

    Transform transform{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
    Transform cameraTransform{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -10.0f} };

    // Sprite用頂点リソース（重複を無くし、頂点4つだけにする）
    ComPtr<ID3D12Resource> vertexResourceSprite = CreateBufferResource(device, sizeof(VertexData) * 4);

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
    ComPtr<ID3D12Resource> indexResourceSprite = CreateBufferResource(device, sizeof(uint32_t) * 6);

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
    ComPtr<ID3D12Resource> transformationMatrixResourceSprite = CreateBufferResource(device, sizeof(TransformationMatrix));
    TransformationMatrix* transformationMatrixDataSprite = nullptr;
    transformationMatrixResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixDataSprite));
    transformationMatrixDataSprite->WVP = MakeIdentity4x4();
    transformationMatrixDataSprite->World = MakeIdentity4x4();

    Transform transformSprite{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };

    bool useMonsterBall = true;

    D3D12_VIEWPORT viewport{ 0.0f, 0.0f, (float)kClientWidth, (float)kClientHeight, 0.0f, 1.0f };
    D3D12_RECT scissorRect{ 0, 0, kClientWidth, kClientHeight };

    // サウンド再生用クラスの初期化とfanfare.wavの読み込み
    SoundPlayer fanfareSound;
    bool soundInitialized = fanfareSound.Initialize();
    assert(soundInitialized);
    bool waveLoaded = fanfareSound.LoadWave("resources/fanfare.wav");
    assert(waveLoaded);

#ifdef USE_IMGUI
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX12_Init(device.Get(),
        swapChainDesc.BufferCount,
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
        srvDescriptorHeap.Get(),
        srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
        srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Build();
#endif

    // objが参照しているテクスチャがあればそれを、無ければuvCheckerを読み込む
    std::string modelTexturePath = modelData.material.textureFilePath.empty()
        ? "resources/uvChecker.png"
        : modelData.material.textureFilePath;
    DirectX::ScratchImage mipImages = LoadTexture(modelTexturePath);
    const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
    ComPtr<ID3D12Resource> textureResource = CreateTextureResource(device, metadata);
    ComPtr<ID3D12Resource> intermediateResource = UploadTextureData(textureResource, mipImages, device, commandList);

    // monsterBall読み込み
    DirectX::ScratchImage mipImages2 = LoadTexture("resources/monsterBall.png");
    const DirectX::TexMetadata& metadata2 = mipImages2.GetMetadata();
    ComPtr<ID3D12Resource> textureResource2 = CreateTextureResource(device, metadata2);
    ComPtr<ID3D12Resource> intermediateResource2 = UploadTextureData(textureResource2, mipImages2, device, commandList);

    // CommandListを閉じてキック
    commandList->Close();
    ID3D12CommandList* commandLists[] = { commandList.Get() };
    commandQueue->ExecuteCommandLists(1, commandLists);

    // 実行完了を待つ
    fenceValue++;
    commandQueue->Signal(fence.Get(), fenceValue);
    if (fence->GetCompletedValue() < fenceValue) {
        fence->SetEventOnCompletion(fenceValue, fenceEvent);
        WaitForSingleObject(fenceEvent, INFINITE);
    }

    // allocatorとcommandListをリセット
    commandAllocator->Reset();
    commandList->Reset(commandAllocator.Get(), nullptr);

    // intermediateResourceを解放（ComPtrなのでResetするだけでよい）
    intermediateResource.Reset();
    intermediateResource2.Reset();

    // SRV作成(uvChecker)
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = metadata.format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);
    D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU = GetCPUDescriptorHandle(srvDescriptorHeap, desriptorSizeSRV, 1);
    D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU = GetGPUDescriptorHandle(srvDescriptorHeap, desriptorSizeSRV, 1);
    device->CreateShaderResourceView(textureResource.Get(), &srvDesc, textureSrvHandleCPU);

    // SRV作成(monsterBall) 
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc2{};
    srvDesc2.Format = metadata2.format;
    srvDesc2.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc2.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc2.Texture2D.MipLevels = UINT(metadata2.mipLevels);
    D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU2 = GetCPUDescriptorHandle(srvDescriptorHeap, desriptorSizeSRV, 2);
    D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU2 = GetGPUDescriptorHandle(srvDescriptorHeap, desriptorSizeSRV, 2);
    device->CreateShaderResourceView(textureResource2.Get(), &srvDesc2, textureSrvHandleCPU2);

    // DepthStencilTextureを作成
    ComPtr<ID3D12Resource> depthStencilResource = CreateDepthStencilTextureResource(device, kClientWidth, kClientHeight);

    // DSV用DescriptorHeap
    ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);

    // DSVの設定
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;          // Format。基本的にはResourceに合わせる
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;   // 2dTexture
    // DSVを作成
    device->CreateDepthStencilView(depthStencilResource.Get(), &dsvDesc, dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

    bool fanfarePlayedLastFrame = false;

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

            // UVTransform行列を作ってSprite用マテリアルに反映
            Matrix4x4 uvTransformMatrix = MakeAffineMatrix(
                uvTransformSprite.scale, uvTransformSprite.rotate, uvTransformSprite.translate);
            materialDataSprite->uvTransform = uvTransformMatrix;

            // Pキーでファンファーレを再生（連打で多重再生しないようキー押下エッジで判定）
            bool fanfareKeyDown = (GetAsyncKeyState('P') & 0x8000) != 0;
            if (fanfareKeyDown && !fanfarePlayedLastFrame) {
                fanfareSound.Play();
            }
            fanfarePlayedLastFrame = fanfareKeyDown;

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
            ImGui::DragFloat2("UVTranslate", &uvTransformSprite.translate.x, 0.01f);
            ImGui::DragFloat2("UVScale", &uvTransformSprite.scale.x, 0.01f);
            ImGui::SliderAngle("UVRotate", &uvTransformSprite.rotate.z);
            if (ImGui::Button("Play Fanfare")) {
                fanfareSound.Play();
            }
            ImGui::End();
#endif

#ifdef USE_IMGUI
            ImGui::Render();
#endif

            UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();

            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = swapChainResources[backBufferIndex].Get();
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
            ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap.Get() };
            commandList->SetDescriptorHeaps(1, descriptorHeaps);
#endif

            commandList->RSSetViewports(1, &viewport);
            commandList->RSSetScissorRects(1, &scissorRect);
            commandList->SetGraphicsRootSignature(rootSignature.Get());
            commandList->SetPipelineState(graphicsPipelineState.Get());
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
            ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList.Get());
#endif

            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
            commandList->ResourceBarrier(1, &barrier);

            commandList->Close();
            ID3D12CommandList* frameCommandLists[] = { commandList.Get() };
            commandQueue->ExecuteCommandLists(1, frameCommandLists);
            swapChain->Present(1, 0);

            fenceValue++;
            commandQueue->Signal(fence.Get(), fenceValue);
            if (fence->GetCompletedValue() < fenceValue) {
                fence->SetEventOnCompletion(fenceValue, fenceEvent);
                WaitForSingleObject(fenceEvent, INFINITE);
            }

            commandAllocator->Reset();
            commandList->Reset(commandAllocator.Get(), nullptr);
        }
    }

#ifdef USE_IMGUI
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
#endif

    // サウンド関連の終了処理（XAudio2のVoice/Engineを解放）
    fanfareSound.Finalize();

    CloseHandle(fenceEvent);

    // COMの終了処理 
    CoUninitialize();

    // ここから先はスコープを抜ける際に全てのComPtrが自動でReleaseされる。
    // その一番最後に、先頭で宣言したleakCheckのDestructorが呼ばれてリークチェックが走る。
    return 0;
}