#include <Windows.h>
#include <cstdint>
#include <string>
#include <format>
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

struct Matrix4x4 {
    float m[4][4];
};

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

// ★★ 変更: worldMatrix に加えて color (float4) を保持するよう拡張
//    256バイトアライメントは alignas(256) で保証
struct alignas(256) ConstantBufferData {
    Matrix4x4 worldMatrix; // b0 / 頂点シェーダー用（変換行列）
    float     color[4];    // b0 / ピクセルシェーダー用（色情報）
    // ※ 今回は1つのバッファを b0 に束ね、VS・PS 両方から参照する設計にします。
    //   ルートシグネチャの ShaderVisibility を ALL に変更することで両シェーダーが参照できます。
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

    // ★★ 変更: 頂点シェーダー（b0: worldMatrix）
    //    頂点カラーは頂点バッファから削除し、ピクセルシェーダーが定数バッファから色を受け取る
    const char* vsCode = R"(
cbuffer TransformCB : register(b0) {
    float4x4 worldMatrix;
    float4   color;       // 構造体に合わせて宣言するが VS では使わない
};
struct VSInput {
    float4 pos : POSITION;
};
struct VSOutput {
    float4 pos : SV_POSITION;
};
VSOutput main(VSInput input) {
    VSOutput output;
    output.pos = mul(input.pos, worldMatrix);
    return output;
}
)";

    // ★★ 変更: ピクセルシェーダーが同じ b0 から color を受け取る
    const char* psCode = R"(
cbuffer TransformCB : register(b0) {
    float4x4 worldMatrix; // PS では使わないが、オフセットを合わせるために宣言する
    float4   color;
};
struct PSInput {
    float4 pos : SV_POSITION;
};
float4 main(PSInput input) : SV_TARGET {
    return color;
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

    // ★★ 変更: ShaderVisibility を ALL に変更
    //    → VS と PS の両方が同じ b0 バッファを参照できる
    ID3D12RootSignature* rootSignature = nullptr;
    D3D12_ROOT_PARAMETER rootParam{};
    rootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParam.Descriptor.ShaderRegister = 0; // b0
    rootParam.Descriptor.RegisterSpace = 0;
    rootParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL; // ★★ VERTEX → ALL

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc{};
    rootSigDesc.NumParameters = 1;
    rootSigDesc.pParameters = &rootParam;
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ID3DBlob* sigBlob = nullptr;
    hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errorBlob);
    assert(SUCCEEDED(hr));
    hr = device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
    assert(SUCCEEDED(hr));
    sigBlob->Release();

    // ★★ 変更: 頂点バッファから COLOR セマンティクスを削除
    //    色は ConstantBuffer 経由で渡すので頂点ごとの色情報は不要
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
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

    // ★★ 変更: 頂点バッファから COLOR を削除（位置情報のみ）
    struct Vertex { float pos[4]; };
    Vertex vertices[] = {
        { {  0.0f,  0.5f, 0.0f, 1.0f } },
        { {  0.5f, -0.5f, 0.0f, 1.0f } },
        { { -0.5f, -0.5f, 0.0f, 1.0f } },
    };
    UINT vertexBufferSize = sizeof(vertices);

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
    memcpy(mappedVertices, vertices, vertexBufferSize);
    // ★★ 変更: 頂点カラーを毎フレーム書き換えるコードは削除

    D3D12_VERTEX_BUFFER_VIEW vbView{};
    vbView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
    vbView.SizeInBytes = vertexBufferSize;
    vbView.StrideInBytes = sizeof(Vertex);

    // 定数バッファの作成
    ID3D12Resource* constantBuffer = nullptr;
    D3D12_HEAP_PROPERTIES cbHeapProps{};
    cbHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC cbResDesc{};
    cbResDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    cbResDesc.Width = sizeof(ConstantBufferData); // alignas(256) により 256 の倍数が保証される
    cbResDesc.Height = 1;
    cbResDesc.DepthOrArraySize = 1;
    cbResDesc.MipLevels = 1;
    cbResDesc.SampleDesc.Count = 1;
    cbResDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hr = device->CreateCommittedResource(&cbHeapProps, D3D12_HEAP_FLAG_NONE, &cbResDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&constantBuffer));
    assert(SUCCEEDED(hr));

    ConstantBufferData* mappedCB = nullptr;
    constantBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mappedCB));

    D3D12_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(kClientWidth);
    viewport.Height = static_cast<float>(kClientHeight);
    viewport.MaxDepth = 1.0f;
    D3D12_RECT scissorRect{};
    scissorRect.right = kClientWidth;
    scissorRect.bottom = kClientHeight;

    ID3D12DescriptorHeap* srvDescriptorHeap = nullptr;
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc{};
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.NumDescriptors = 1;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&srvDescriptorHeap));
    assert(SUCCEEDED(hr));

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX12_Init(
        device, 2,
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
        srvDescriptorHeap,
        srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
        srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart()
    );

    float triangleColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float rotationSpeed = 1.0f;
    float rotationAngle = 0.0f;

    bool showDemoWindow = true;

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

            ImGui::Begin("Triangle Settings");
            ImGui::ColorEdit4("Color", triangleColor);
            ImGui::SliderFloat("Rotation Speed", &rotationSpeed, -5.0f, 5.0f);
            ImGui::Text("Angle: %.2f rad", rotationAngle);
            ImGui::End();

            ImGui::Render();

            // ★★ 変更: worldMatrix と color の両方を定数バッファ経由で GPU に渡す
            mappedCB->worldMatrix = MakeRotationY(rotationAngle);
            mappedCB->color[0] = triangleColor[0]; // R
            mappedCB->color[1] = triangleColor[1]; // G
            mappedCB->color[2] = triangleColor[2]; // B
            mappedCB->color[3] = triangleColor[3]; // A
            // ★★ 変更: 頂点バッファの色書き換えは不要になったため削除

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
            commandList->SetGraphicsRootConstantBufferView(0, constantBuffer->GetGPUVirtualAddress());
            commandList->SetPipelineState(pipelineState);
            commandList->RSSetViewports(1, &viewport);
            commandList->RSSetScissorRects(1, &scissorRect);
            commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            commandList->IASetVertexBuffers(0, 1, &vbView);
            commandList->DrawInstanced(3, 1, 0, 0);

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

    constantBuffer->Unmap(0, nullptr);
    if (constantBuffer) constantBuffer->Release();
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