#include <Windows.h>
#include <cstdint>
#include <string>
#include <format>
#include <vector>
#include <algorithm>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cassert>
#include <cmath>
#include <chrono>

#include <dxgidebug.h>
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

#include <dxcapi.h>
#pragma comment(lib, "dxcompiler.lib")

#include "externals/DirectXTex/DirectXTex.h"
#include <wrl.h>
#include "externals/DirectXTex/d3dx12.h"

#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_win32.h"
#include "externals/imgui/imgui_impl_dx12.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

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
    if (ImGui_ImplWin32_WndProcHandler(hwnd, uMsg, wParam, lParam)) return true;
    if (uMsg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

// ★ 3次元ベクトル
struct Vector3 { float x, y, z; };

// ★ 16バイトアライメントの行列構造体
struct Matrix4x4 {
    float m[4][4];
};

// ★ 行列の掛け算
Matrix4x4 Multiply(const Matrix4x4& a, const Matrix4x4& b) {
    Matrix4x4 result{};
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a.m[i][k] * b.m[k][j];
            }
            result.m[i][j] = sum;
        }
    }
    return result;
}

// ★ 4x4逆行列（Gauss-Jordan消去法）
Matrix4x4 Inverse(const Matrix4x4& matrix) {
    Matrix4x4 a = matrix;
    Matrix4x4 result{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            result.m[i][j] = (i == j) ? 1.0f : 0.0f;

    for (int col = 0; col < 4; ++col) {
        int pivotRow = col;
        float maxVal = fabsf(a.m[col][col]);
        for (int row = col + 1; row < 4; ++row) {
            if (fabsf(a.m[row][col]) > maxVal) {
                maxVal = fabsf(a.m[row][col]);
                pivotRow = row;
            }
        }
        if (pivotRow != col) {
            for (int k = 0; k < 4; ++k) {
                std::swap(a.m[col][k], a.m[pivotRow][k]);
                std::swap(result.m[col][k], result.m[pivotRow][k]);
            }
        }

        float pivot = a.m[col][col];
        assert(pivot != 0.0f);
        for (int k = 0; k < 4; ++k) {
            a.m[col][k] /= pivot;
            result.m[col][k] /= pivot;
        }

        for (int row = 0; row < 4; ++row) {
            if (row != col) {
                float factor = a.m[row][col];
                for (int k = 0; k < 4; ++k) {
                    a.m[row][k] -= factor * a.m[col][k];
                    result.m[row][k] -= factor * result.m[col][k];
                }
            }
        }
    }
    return result;
}

Matrix4x4 MakeScaleMatrix(const Vector3& scale) {
    Matrix4x4 m{};
    m.m[0][0] = scale.x;
    m.m[1][1] = scale.y;
    m.m[2][2] = scale.z;
    m.m[3][3] = 1.0f;
    return m;
}

Matrix4x4 MakeTranslateMatrix(const Vector3& translate) {
    Matrix4x4 m{};
    m.m[0][0] = 1.0f; m.m[1][1] = 1.0f; m.m[2][2] = 1.0f; m.m[3][3] = 1.0f;
    m.m[3][0] = translate.x;
    m.m[3][1] = translate.y;
    m.m[3][2] = translate.z;
    return m;
}

Matrix4x4 MakeRotateXMatrix(float angle) {
    float c = cosf(angle), s = sinf(angle);
    Matrix4x4 m{};
    m.m[0][0] = 1.0f;
    m.m[1][1] = c;  m.m[1][2] = s;
    m.m[2][1] = -s; m.m[2][2] = c;
    m.m[3][3] = 1.0f;
    return m;
}

Matrix4x4 MakeRotateYMatrix(float angle) {
    float c = cosf(angle), s = sinf(angle);
    Matrix4x4 m{};
    m.m[0][0] = c;  m.m[0][2] = s;
    m.m[1][1] = 1.0f;
    m.m[2][0] = -s; m.m[2][2] = c;
    m.m[3][3] = 1.0f;
    return m;
}

Matrix4x4 MakeRotateZMatrix(float angle) {
    float c = cosf(angle), s = sinf(angle);
    Matrix4x4 m{};
    m.m[0][0] = c;  m.m[0][1] = s;
    m.m[1][0] = -s; m.m[1][1] = c;
    m.m[2][2] = 1.0f;
    m.m[3][3] = 1.0f;
    return m;
}

// ★ Scale * RotateX * RotateY * RotateZ * Translate のアフィン変換行列
Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Vector3& rotate, const Vector3& translate) {
    Matrix4x4 scaleMatrix = MakeScaleMatrix(scale);
    Matrix4x4 rotateXMatrix = MakeRotateXMatrix(rotate.x);
    Matrix4x4 rotateYMatrix = MakeRotateYMatrix(rotate.y);
    Matrix4x4 rotateZMatrix = MakeRotateZMatrix(rotate.z);
    Matrix4x4 rotateMatrix = Multiply(Multiply(rotateXMatrix, rotateYMatrix), rotateZMatrix);
    Matrix4x4 translateMatrix = MakeTranslateMatrix(translate);
    return Multiply(Multiply(scaleMatrix, rotateMatrix), translateMatrix);
}

// ★ 透視投影行列
Matrix4x4 MakePerspectiveFovMatrix(float fovY, float aspectRatio, float nearClip, float farClip) {
    Matrix4x4 result{};
    float cot = 1.0f / tanf(fovY / 2.0f);
    result.m[0][0] = cot / aspectRatio;
    result.m[1][1] = cot;
    result.m[2][2] = farClip / (farClip - nearClip);
    result.m[2][3] = 1.0f;
    result.m[3][2] = -nearClip * farClip / (farClip - nearClip);
    return result;
}

// ★ Transform用定数バッファ（WVP行列とWorld行列。合計256バイトになるので追加パディング不要）
struct alignas(256) TransformationMatrix {
    Matrix4x4 WVP;
    Matrix4x4 World;
};

// ★ マテリアル用の定数バッファ（色 + Lightingを使うかどうか）
struct alignas(256) MaterialCBData {
    float color[4];
    int32_t enableLighting;
    float padding[3];
};

// ★ 平行光源用の定数バッファ
struct alignas(256) DirectionalLightData {
    float color[4];
    float direction[3];
    float intensity;
};

// ★ 頂点データ構造体（POSITION / COLOR / TEXCOORD / NORMAL）
struct Vertex { float pos[4]; float color[4]; float uv[2]; float normal[3]; };

// ★ 球の分割数（緯度・経度とも同じ数で分割）
const uint32_t kSubdivision = 16;

// ★ UV球のVertexデータを生成する（三角形リスト、インデックスなし）
//   ★ Lighting用に法線ベクトルを追加：原点中心・半径1の球なので法線=座標そのもの
std::vector<Vertex> CreateSphereVertexData(uint32_t subdivision) {
    std::vector<Vertex> vertices;
    vertices.resize(subdivision * subdivision * 6);

    const float pi = 3.14159265358979f;
    const float kLatEvery = pi / static_cast<float>(subdivision);
    const float kLonEvery = 2.0f * pi / static_cast<float>(subdivision);

    auto setNormal = [](Vertex& v) {
        v.normal[0] = v.pos[0];
        v.normal[1] = v.pos[1];
        v.normal[2] = v.pos[2];
        };

    for (uint32_t latIndex = 0; latIndex < subdivision; ++latIndex) {
        float lat = -pi / 2.0f + kLatEvery * latIndex;

        for (uint32_t lonIndex = 0; lonIndex < subdivision; ++lonIndex) {
            uint32_t start = (latIndex * subdivision + lonIndex) * 6;
            float lon = lonIndex * kLonEvery;

            // a: 左下
            vertices[start].pos[0] = cosf(lat) * cosf(lon);
            vertices[start].pos[1] = sinf(lat);
            vertices[start].pos[2] = cosf(lat) * sinf(lon);
            vertices[start].pos[3] = 1.0f;
            vertices[start].uv[0] = float(lonIndex) / subdivision;
            vertices[start].uv[1] = 1.0f - float(latIndex) / subdivision;
            setNormal(vertices[start]);

            // b: 左上
            vertices[start + 1].pos[0] = cosf(lat + kLatEvery) * cosf(lon);
            vertices[start + 1].pos[1] = sinf(lat + kLatEvery);
            vertices[start + 1].pos[2] = cosf(lat + kLatEvery) * sinf(lon);
            vertices[start + 1].pos[3] = 1.0f;
            vertices[start + 1].uv[0] = float(lonIndex) / subdivision;
            vertices[start + 1].uv[1] = 1.0f - float(latIndex + 1) / subdivision;
            setNormal(vertices[start + 1]);

            // c: 右下
            vertices[start + 2].pos[0] = cosf(lat) * cosf(lon + kLonEvery);
            vertices[start + 2].pos[1] = sinf(lat);
            vertices[start + 2].pos[2] = cosf(lat) * sinf(lon + kLonEvery);
            vertices[start + 2].pos[3] = 1.0f;
            vertices[start + 2].uv[0] = float(lonIndex + 1) / subdivision;
            vertices[start + 2].uv[1] = 1.0f - float(latIndex) / subdivision;
            setNormal(vertices[start + 2]);

            // 2枚目の三角形: b, d, c
            vertices[start + 3] = vertices[start + 1]; // b

            // d: 右上
            vertices[start + 4].pos[0] = cosf(lat + kLatEvery) * cosf(lon + kLonEvery);
            vertices[start + 4].pos[1] = sinf(lat + kLatEvery);
            vertices[start + 4].pos[2] = cosf(lat + kLatEvery) * sinf(lon + kLonEvery);
            vertices[start + 4].pos[3] = 1.0f;
            vertices[start + 4].uv[0] = float(lonIndex + 1) / subdivision;
            vertices[start + 4].uv[1] = 1.0f - float(latIndex + 1) / subdivision;
            setNormal(vertices[start + 4]);

            vertices[start + 5] = vertices[start + 2]; // c

            for (uint32_t i = 0; i < 6; ++i) {
                vertices[start + i].color[0] = 1.0f;
                vertices[start + i].color[1] = 1.0f;
                vertices[start + i].color[2] = 1.0f;
                vertices[start + i].color[3] = 1.0f;
            }
        }
    }
    return vertices;
}

// ★ DXCを使ってHLSLファイルをコンパイルする
IDxcBlob* CompileShader(
    const std::wstring& filePath,
    const wchar_t* profile,
    IDxcUtils* dxcUtils,
    IDxcCompiler3* dxcCompiler,
    IDxcIncludeHandler* includeHandler) {

    Log(ConvertString(std::format(L"Begin CompileShader, path:{}, profile:{}\n", filePath, profile)));

    IDxcBlobEncoding* shaderSource = nullptr;
    HRESULT hr = dxcUtils->LoadFile(filePath.c_str(), nullptr, &shaderSource);
    assert(SUCCEEDED(hr));

    DxcBuffer shaderSourceBuffer{};
    shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
    shaderSourceBuffer.Size = shaderSource->GetBufferSize();
    shaderSourceBuffer.Encoding = DXC_CP_UTF8;

    LPCWSTR arguments[] = {
        filePath.c_str(),
        L"-E", L"main",
        L"-T", profile,
        L"-Zi", L"-Qembed_debug",
        L"-Od",
        L"-Zpr",
    };

    IDxcResult* shaderResult = nullptr;
    hr = dxcCompiler->Compile(
        &shaderSourceBuffer,
        arguments, _countof(arguments),
        includeHandler,
        IID_PPV_ARGS(&shaderResult));
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

    if (shaderError) shaderError->Release();
    shaderSource->Release();
    shaderResult->Release();
    return shaderBlob;
}

// ★ DirectXTexを使ったテクスチャ読み込み関数
DirectX::ScratchImage LoadTexture(const std::string& filePath) {
    DirectX::ScratchImage image{};
    std::wstring filePathW = ConvertString(filePath);
    HRESULT hr = DirectX::LoadFromWICFile(filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
    assert(SUCCEEDED(hr));

    DirectX::ScratchImage mipImages{};
    hr = DirectX::GenerateMipMaps(
        image.GetImages(), image.GetImageCount(), image.GetMetadata(),
        DirectX::TEX_FILTER_SRGB, 0, mipImages);
    assert(SUCCEEDED(hr));

    return mipImages;
}

// ★ DirectXTexのMetadataを基にテクスチャリソース(DEFAULTヒープ)を生成する
ID3D12Resource* CreateTextureResource(ID3D12Device* device, const DirectX::TexMetadata& metadata) {
    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Width = UINT(metadata.width);
    resourceDesc.Height = UINT(metadata.height);
    resourceDesc.MipLevels = UINT16(metadata.mipLevels);
    resourceDesc.DepthOrArraySize = UINT16(metadata.arraySize);
    resourceDesc.Format = metadata.format;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION(metadata.dimension);

    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

    ID3D12Resource* resource = nullptr;
    HRESULT hr = device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&resource));
    assert(SUCCEEDED(hr));
    return resource;
}

// ★ DepthStencilTextureリソースを生成する
ID3D12Resource* CreateDepthStencilTextureResource(ID3D12Device* device, int32_t width, int32_t height) {
    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Width = width;
    resourceDesc.Height = height;
    resourceDesc.MipLevels = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_CLEAR_VALUE depthClearValue{};
    depthClearValue.DepthStencil.Depth = 1.0f;
    depthClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;

    ID3D12Resource* resource = nullptr;
    HRESULT hr = device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &depthClearValue,
        IID_PPV_ARGS(&resource));
    assert(SUCCEEDED(hr));
    return resource;
}

// ★ DirectXTexのPrepareUpload + UpdateSubresourcesでテクスチャデータをアップロードする
[[nodiscard]]
ID3D12Resource* UploadTextureData(
    ID3D12Resource* texture,
    const DirectX::ScratchImage& mipImages,
    ID3D12Device* device,
    ID3D12GraphicsCommandList* commandList) {

    std::vector<D3D12_SUBRESOURCE_DATA> subresources;
    DirectX::PrepareUpload(device, mipImages.GetImages(), mipImages.GetImageCount(), mipImages.GetMetadata(), subresources);
    uint64_t intermediateSize = GetRequiredIntermediateSize(texture, 0, UINT(subresources.size()));

    D3D12_HEAP_PROPERTIES uploadHeapProperties{};
    uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC uploadResourceDesc{};
    uploadResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadResourceDesc.Width = intermediateSize;
    uploadResourceDesc.Height = 1;
    uploadResourceDesc.DepthOrArraySize = 1;
    uploadResourceDesc.MipLevels = 1;
    uploadResourceDesc.SampleDesc.Count = 1;
    uploadResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ID3D12Resource* intermediateResource = nullptr;
    HRESULT hr = device->CreateCommittedResource(
        &uploadHeapProperties,
        D3D12_HEAP_FLAG_NONE,
        &uploadResourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&intermediateResource));
    assert(SUCCEEDED(hr));

    UpdateSubresources(commandList, texture, intermediateResource, 0, 0, UINT(subresources.size()), subresources.data());

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = texture;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);

    return intermediateResource;
}

D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index) {
    D3D12_CPU_DESCRIPTOR_HANDLE handleCPU = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
    handleCPU.ptr += (descriptorSize * index);
    return handleCPU;
}

D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index) {
    D3D12_GPU_DESCRIPTOR_HANDLE handleGPU = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
    handleGPU.ptr += (descriptorSize * index);
    return handleGPU;
}

int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {
    HRESULT hr = S_OK;

    hr = CoInitializeEx(0, COINIT_MULTITHREADED);
    assert(SUCCEEDED(hr));

    IDxcUtils* dxcUtils = nullptr;
    IDxcCompiler3* dxcCompiler = nullptr;
    IDxcIncludeHandler* includeHandler = nullptr;
    hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
    assert(SUCCEEDED(hr));
    hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
    assert(SUCCEEDED(hr));
    hr = dxcUtils->CreateDefaultIncludeHandler(&includeHandler);
    assert(SUCCEEDED(hr));

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

    ID3D12Resource* depthStencilResource = CreateDepthStencilTextureResource(device, kClientWidth, kClientHeight);

    ID3D12DescriptorHeap* dsvDescriptorHeap = nullptr;
    D3D12_DESCRIPTOR_HEAP_DESC dsvDescriptorHeapDesc{};
    dsvDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvDescriptorHeapDesc.NumDescriptors = 1;
    hr = device->CreateDescriptorHeap(&dsvDescriptorHeapDesc, IID_PPV_ARGS(&dsvDescriptorHeap));
    assert(SUCCEEDED(hr));

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    device->CreateDepthStencilView(depthStencilResource, &dsvDesc, dsvHandle);

    ID3D12Fence* fence = nullptr;
    uint64_t fenceValue = 0;
    hr = device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    assert(SUCCEEDED(hr));
    HANDLE fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    assert(fenceEvent != nullptr);

    IDxcBlob* vsBlob = CompileShader(L"Object3d.VS.hlsl", L"vs_6_0", dxcUtils, dxcCompiler, includeHandler);
    assert(vsBlob != nullptr);
    IDxcBlob* psBlob = CompileShader(L"Object3d.PS.hlsl", L"ps_6_0", dxcUtils, dxcCompiler, includeHandler);
    assert(psBlob != nullptr);

    // ★ ルートシグネチャ：b0(Transform/VS), b1(マテリアル/PS), t0(テクスチャ/PS), b2(ライト/PS) + 静的サンプラー
    ID3D12RootSignature* rootSignature = nullptr;
    ID3DBlob* errorBlob = nullptr;

    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 0;
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParams[4]{};
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[0].Descriptor.ShaderRegister = 0; // b0 (Transform: WVP/World)
    rootParams[0].Descriptor.RegisterSpace = 0;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[1].Descriptor.ShaderRegister = 1; // b1 (マテリアルカラー + enableLighting)
    rootParams[1].Descriptor.RegisterSpace = 0;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[2].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[2].DescriptorTable.pDescriptorRanges = &srvRange;
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[3].Descriptor.ShaderRegister = 2; // b2 (平行光源)
    rootParams[3].Descriptor.RegisterSpace = 0;
    rootParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

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

    // ★ NORMALを追加した入力レイアウト（uv:offset32,8バイト → normal:offset40）
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
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
    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

    ID3D12PipelineState* pipelineState = nullptr;
    hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState));
    assert(SUCCEEDED(hr));

    std::vector<Vertex> vertices = CreateSphereVertexData(kSubdivision);
    UINT vertexBufferSize = static_cast<UINT>(sizeof(Vertex) * vertices.size());

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC resDesc{};
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resDesc.Width = vertexBufferSize;
    resDesc.Height = 1;
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = 1;
    resDesc.SampleDesc.Count = 1;
    resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ID3D12Resource* vertexBuffer = nullptr;
    hr = device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertexBuffer));
    assert(SUCCEEDED(hr));

    Vertex* mappedVertices = nullptr;
    vertexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mappedVertices));
    memcpy(mappedVertices, vertices.data(), vertexBufferSize);

    D3D12_VERTEX_BUFFER_VIEW vbView{};
    vbView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
    vbView.SizeInBytes = vertexBufferSize;
    vbView.StrideInBytes = sizeof(Vertex);

    // ★ Transform用の定数バッファ（WVP/World）
    ID3D12Resource* transformBuffer = nullptr;
    D3D12_HEAP_PROPERTIES tbHeapProps{};
    tbHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC tbResDesc{};
    tbResDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    tbResDesc.Width = sizeof(TransformationMatrix);
    tbResDesc.Height = 1;
    tbResDesc.DepthOrArraySize = 1;
    tbResDesc.MipLevels = 1;
    tbResDesc.SampleDesc.Count = 1;
    tbResDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hr = device->CreateCommittedResource(&tbHeapProps, D3D12_HEAP_FLAG_NONE, &tbResDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&transformBuffer));
    assert(SUCCEEDED(hr));

    TransformationMatrix* mappedTransform = nullptr;
    transformBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mappedTransform));

    // ★ マテリアルカラー用の定数バッファ（デフォルトは白、Lighting有効）
    ID3D12Resource* materialBuffer = nullptr;
    D3D12_HEAP_PROPERTIES matHeapProps{};
    matHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC matResDesc{};
    matResDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    matResDesc.Width = sizeof(MaterialCBData);
    matResDesc.Height = 1;
    matResDesc.DepthOrArraySize = 1;
    matResDesc.MipLevels = 1;
    matResDesc.SampleDesc.Count = 1;
    matResDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hr = device->CreateCommittedResource(&matHeapProps, D3D12_HEAP_FLAG_NONE, &matResDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&materialBuffer));
    assert(SUCCEEDED(hr));

    MaterialCBData* mappedMaterial = nullptr;
    materialBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mappedMaterial));
    mappedMaterial->color[0] = 1.0f;
    mappedMaterial->color[1] = 1.0f;
    mappedMaterial->color[2] = 1.0f;
    mappedMaterial->color[3] = 1.0f;
    mappedMaterial->enableLighting = 1;

    // ★ 平行光源用の定数バッファ
    ID3D12Resource* lightBuffer = nullptr;
    D3D12_HEAP_PROPERTIES lightHeapProps{};
    lightHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC lightResDesc{};
    lightResDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    lightResDesc.Width = sizeof(DirectionalLightData);
    lightResDesc.Height = 1;
    lightResDesc.DepthOrArraySize = 1;
    lightResDesc.MipLevels = 1;
    lightResDesc.SampleDesc.Count = 1;
    lightResDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hr = device->CreateCommittedResource(&lightHeapProps, D3D12_HEAP_FLAG_NONE, &lightResDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&lightBuffer));
    assert(SUCCEEDED(hr));

    DirectionalLightData* mappedLight = nullptr;
    lightBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mappedLight));
    mappedLight->color[0] = 1.0f;
    mappedLight->color[1] = 1.0f;
    mappedLight->color[2] = 1.0f;
    mappedLight->color[3] = 1.0f;
    mappedLight->direction[0] = 0.0f;
    mappedLight->direction[1] = -1.0f;
    mappedLight->direction[2] = 0.0f;
    mappedLight->intensity = 1.0f;

    D3D12_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(kClientWidth);
    viewport.Height = static_cast<float>(kClientHeight);
    viewport.MaxDepth = 1.0f;
    D3D12_RECT scissorRect{};
    scissorRect.right = kClientWidth;
    scissorRect.bottom = kClientHeight;

    DirectX::ScratchImage mipImagesUvChecker = LoadTexture("resources/uvChecker.png");
    const DirectX::TexMetadata& metadataUvChecker = mipImagesUvChecker.GetMetadata();
    ID3D12Resource* textureResourceUvChecker = CreateTextureResource(device, metadataUvChecker);

    DirectX::ScratchImage mipImagesMonsterBall = LoadTexture("resources/monsterBall.png");
    const DirectX::TexMetadata& metadataMonsterBall = mipImagesMonsterBall.GetMetadata();
    ID3D12Resource* textureResourceMonsterBall = CreateTextureResource(device, metadataMonsterBall);

    commandList->Reset(commandAllocator, nullptr);
    ID3D12Resource* intermediateResourceUvChecker = UploadTextureData(textureResourceUvChecker, mipImagesUvChecker, device, commandList);
    ID3D12Resource* intermediateResourceMonsterBall = UploadTextureData(textureResourceMonsterBall, mipImagesMonsterBall, device, commandList);
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
    intermediateResourceUvChecker->Release();
    intermediateResourceUvChecker = nullptr;
    intermediateResourceMonsterBall->Release();
    intermediateResourceMonsterBall = nullptr;

    commandAllocator->Reset();
    commandList->Reset(commandAllocator, nullptr);

    const UINT kSrvHeapDescriptorCount = 4;
    ID3D12DescriptorHeap* srvDescriptorHeap = nullptr;
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc{};
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.NumDescriptors = kSrvHeapDescriptorCount;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&srvDescriptorHeap));
    assert(SUCCEEDED(hr));

    UINT srvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_CPU_DESCRIPTOR_HANDLE imguiFontCpuHandle = GetCPUDescriptorHandle(srvDescriptorHeap, srvDescriptorSize, 0);
    D3D12_GPU_DESCRIPTOR_HANDLE imguiFontGpuHandle = GetGPUDescriptorHandle(srvDescriptorHeap, srvDescriptorSize, 0);

    D3D12_CPU_DESCRIPTOR_HANDLE uvCheckerCpuHandle = GetCPUDescriptorHandle(srvDescriptorHeap, srvDescriptorSize, 1);
    D3D12_GPU_DESCRIPTOR_HANDLE uvCheckerGpuHandle = GetGPUDescriptorHandle(srvDescriptorHeap, srvDescriptorSize, 1);

    D3D12_CPU_DESCRIPTOR_HANDLE monsterBallCpuHandle = GetCPUDescriptorHandle(srvDescriptorHeap, srvDescriptorSize, 2);
    D3D12_GPU_DESCRIPTOR_HANDLE monsterBallGpuHandle = GetGPUDescriptorHandle(srvDescriptorHeap, srvDescriptorSize, 2);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDescUvChecker{};
    srvDescUvChecker.Format = metadataUvChecker.format;
    srvDescUvChecker.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDescUvChecker.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDescUvChecker.Texture2D.MipLevels = UINT(metadataUvChecker.mipLevels);
    device->CreateShaderResourceView(textureResourceUvChecker, &srvDescUvChecker, uvCheckerCpuHandle);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDescMonsterBall{};
    srvDescMonsterBall.Format = metadataMonsterBall.format;
    srvDescMonsterBall.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDescMonsterBall.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDescMonsterBall.Texture2D.MipLevels = UINT(metadataMonsterBall.mipLevels);
    device->CreateShaderResourceView(textureResourceMonsterBall, &srvDescMonsterBall, monsterBallCpuHandle);

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

    // ★ マテリアルカラー・Lighting設定
    float materialColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    bool enableLighting = true;
    float rotationSpeed = -1.0f;
    float rotationAngle = 0.0f;
    bool useMonsterBall = true;

    // ★ カメラ設定
    Vector3 cameraTranslate = { 0.0f, 0.0f, -10.0f };
    Vector3 cameraRotate = { 0.0f, 0.0f, 0.0f };

    // ★ ライト設定
    float lightColor[3] = { 1.0f, 1.0f, 1.0f };
    float lightDirection[3] = { 0.0f, -1.0f, 0.0f };
    float lightIntensity = 1.0f;

    bool showDemoWindow = false;

    auto startTime = std::chrono::steady_clock::now();
    auto prevTime = startTime;

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

            ImGui_ImplDX12_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            if (showDemoWindow) {
                ImGui::ShowDemoWindow(&showDemoWindow);
            }

            ImGui::Begin("Settings");
            ImGui::DragFloat3("CameraTranslate", &cameraTranslate.x, 0.1f);
            ImGui::DragFloat("CameraRotateX", &cameraRotate.x, 0.01f);
            ImGui::DragFloat("CameraRotateY", &cameraRotate.y, 0.01f);
            ImGui::DragFloat("CameraRotateZ", &cameraRotate.z, 0.01f);
            ImGui::ColorEdit4("color", materialColor);
            ImGui::Checkbox("enableLighting", &enableLighting);
            ImGui::Checkbox("useMonsterBall", &useMonsterBall);
            ImGui::ColorEdit3("LightColor", lightColor);
            ImGui::DragFloat3("LightDirection", lightDirection, 0.01f, -1.0f, 1.0f);
            ImGui::DragFloat("Intensity", &lightIntensity, 0.01f, 0.0f, 10.0f);
            ImGui::SliderFloat("Rotation Speed", &rotationSpeed, -5.0f, 5.0f);
            ImGui::Text("Angle: %.2f rad", rotationAngle);
            ImGui::End();

            ImGui::Render();

            // ★ World行列（回転のみ）
            Matrix4x4 worldMatrix = MakeAffineMatrix({ 1.0f, 1.0f, 1.0f }, { 0.0f, rotationAngle, 0.0f }, { 0.0f, 0.0f, 0.0f });
            // ★ カメラのワールド行列 → View行列はその逆行列
            Matrix4x4 cameraMatrix = MakeAffineMatrix({ 1.0f, 1.0f, 1.0f }, cameraRotate, cameraTranslate);
            Matrix4x4 viewMatrix = Inverse(cameraMatrix);
            Matrix4x4 projectionMatrix = MakePerspectiveFovMatrix(0.45f * 3.14159265358979f, float(kClientWidth) / float(kClientHeight), 0.1f, 100.0f);
            Matrix4x4 worldViewProjectionMatrix = Multiply(Multiply(worldMatrix, viewMatrix), projectionMatrix);

            mappedTransform->WVP = worldViewProjectionMatrix;
            mappedTransform->World = worldMatrix;

            mappedMaterial->color[0] = materialColor[0];
            mappedMaterial->color[1] = materialColor[1];
            mappedMaterial->color[2] = materialColor[2];
            mappedMaterial->color[3] = materialColor[3];
            mappedMaterial->enableLighting = enableLighting ? 1 : 0;

            mappedLight->color[0] = lightColor[0];
            mappedLight->color[1] = lightColor[1];
            mappedLight->color[2] = lightColor[2];
            mappedLight->color[3] = 1.0f;
            mappedLight->direction[0] = lightDirection[0];
            mappedLight->direction[1] = lightDirection[1];
            mappedLight->direction[2] = lightDirection[2];
            mappedLight->intensity = lightIntensity;

            UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();

            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barrier.Transition.pResource = swapChainResources[backBufferIndex];
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            commandList->ResourceBarrier(1, &barrier);

            commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], false, &dsvHandle);
            commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

            float clearColor[] = { 0.1f, 0.25f, 0.5f, 1.0f };
            commandList->ClearRenderTargetView(rtvHandles[backBufferIndex], clearColor, 0, nullptr);

            ID3D12DescriptorHeap* heaps[] = { srvDescriptorHeap };
            commandList->SetDescriptorHeaps(1, heaps);

            commandList->SetGraphicsRootSignature(rootSignature);
            commandList->SetGraphicsRootConstantBufferView(0, transformBuffer->GetGPUVirtualAddress());
            commandList->SetGraphicsRootConstantBufferView(1, materialBuffer->GetGPUVirtualAddress());
            commandList->SetGraphicsRootDescriptorTable(2, useMonsterBall ? monsterBallGpuHandle : uvCheckerGpuHandle);
            commandList->SetGraphicsRootConstantBufferView(3, lightBuffer->GetGPUVirtualAddress());
            commandList->SetPipelineState(pipelineState);
            commandList->RSSetViewports(1, &viewport);
            commandList->RSSetScissorRects(1, &scissorRect);
            commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            commandList->IASetVertexBuffers(0, 1, &vbView);
            commandList->DrawInstanced(static_cast<UINT>(vertices.size()), 1, 0, 0);

            ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);

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

    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    lightBuffer->Unmap(0, nullptr);
    if (lightBuffer) lightBuffer->Release();
    materialBuffer->Unmap(0, nullptr);
    if (materialBuffer) materialBuffer->Release();
    if (textureResourceUvChecker) textureResourceUvChecker->Release();
    if (textureResourceMonsterBall) textureResourceMonsterBall->Release();
    transformBuffer->Unmap(0, nullptr);
    if (transformBuffer) transformBuffer->Release();
    vertexBuffer->Unmap(0, nullptr);
    if (vertexBuffer) vertexBuffer->Release();
    if (pipelineState) pipelineState->Release();
    if (rootSignature) rootSignature->Release();
    if (vsBlob) vsBlob->Release();
    if (psBlob) psBlob->Release();
    CloseHandle(fenceEvent);
    if (fence) fence->Release();
    if (depthStencilResource) depthStencilResource->Release();
    if (dsvDescriptorHeap) dsvDescriptorHeap->Release();
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
    if (includeHandler) includeHandler->Release();
    if (dxcCompiler) dxcCompiler->Release();
    if (dxcUtils) dxcUtils->Release();

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
    CoUninitialize();

    CloseWindow(hwnd);
    return 0;
}