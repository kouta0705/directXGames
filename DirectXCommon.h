#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#include <dxcapi.h>
#include <cstdint>
#include <array>
#include <vector>
#include <chrono>
#include <string>

#include "externals/DirectXTex/DirectXTex.h"

#include "WinApp.h"

// DirectX基盤クラス
class DirectXCommon
{
public:
    // 終了処理 (ImGui終了・フェンスイベントのクローズ)
    ~DirectXCommon();

    // 初期化
    void Initialize(WinApp* winApp);

    // 描画前処理
    void PreDraw();
    // 描画後処理
    void PostDraw();

    /// <summary>
    /// テクスチャデータの転送
    /// </summary>
    /// <returns>中間リソース (GPUの転送完了まで解放しないこと)</returns>
    [[nodiscard]]
    Microsoft::WRL::ComPtr<ID3D12Resource> UploadTextureData(
        const Microsoft::WRL::ComPtr<ID3D12Resource>& texture,
        const DirectX::ScratchImage& mipImages);

    /// <summary>
    /// バッファリソースの生成 (アップロードヒープ)
    /// </summary>
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateBufferResource(size_t sizeInBytes);

    /// <summary>
    /// テクスチャリソースの生成
    /// </summary>
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateTextureResource(const DirectX::TexMetadata& metadata);

    /// <summary>
    /// テクスチャファイルの読み込み (ミップマップ生成まで行う)
    /// </summary>
    static DirectX::ScratchImage LoadTexture(const std::string& filePath);

    /// <summary>
    /// シェーダーのコンパイル
    /// </summary>
    Microsoft::WRL::ComPtr<IDxcBlob> CompileShader(const std::wstring& filePath, const wchar_t* profile);

    /// <summary>
    /// コマンドを実行してGPUの完了を待ち、コマンドリストを再び使える状態に戻す
    /// (初期化中のテクスチャ転送などで使う。Presentは行わない)
    /// </summary>
    void ExecuteCommandAndWait();

    // --- 他のクラスから使うためのゲッター ---
    ID3D12Device* GetDevice() const { return device_.Get(); }
    ID3D12GraphicsCommandList* GetCommandList() const { return commandList_.Get(); }
    ID3D12DescriptorHeap* GetSrvDescriptorHeap() const { return srvDescriptorHeap_.Get(); }
    ID3D12DescriptorHeap* GetRtvDescriptorHeap() const { return rtvDescriptorHeap_.Get(); }
    ID3D12DescriptorHeap* GetDsvDescriptorHeap() const { return dsvDescriptorHeap_.Get(); }
    IDxcUtils* GetDxcUtils() const { return dxcUtils_.Get(); }
    IDxcCompiler3* GetDxcCompiler() const { return dxcCompiler_.Get(); }
    IDxcIncludeHandler* GetIncludeHandler() const { return includeHandler_.Get(); }

    uint32_t GetDescriptorSizeSRV() const { return descriptorSizeSRV_; }
    uint32_t GetDescriptorSizeRTV() const { return descriptorSizeRTV_; }
    uint32_t GetDescriptorSizeDSV() const { return descriptorSizeDSV_; }

    // デスクリプタハンドル取得
    static D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(
        ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index);
    static D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(
        ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index);

private:
    // ==== 初期化の小分け関数 (Initializeからのみ呼ぶ) ====
    void InitializeDevice();             // デバイスの生成
    void InitializeCommand();            // コマンド関連の初期化
    void CreateSwapChain();              // スワップチェーンの生成
    void CreateDepthBuffer();            // 深度バッファの生成
    void CreateDescriptorHeaps();        // 各種デスクリプタヒープの生成
    void InitializeRenderTargetView();   // レンダーターゲットビューの初期化
    void InitializeDepthStencilView();   // 深度ステンシルビューの初期化
    void InitializeFence();              // フェンスの初期化
    void InitializeViewport();           // ビューポート矩形の初期化
    void InitializeScissorRect();        // シザリング矩形の初期化
    void CreateDXCCompiler();            // DXCコンパイラの生成
    void InitializeImGui();              // ImGuiの初期化

    // FPS固定初期化
    void InitializeFixFPS();
    // FPS固定更新
    void UpdateFixFPS();

    // ==== 内部ヘルパー ====
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible);
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateDepthStencilTextureResource(
        int32_t width, int32_t height);

private:
    // WindowsAPI (借り物なのでdeleteしない)
    WinApp* winApp_ = nullptr;

    // DXGI / デバイス
    Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory_;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;

    // コマンド関連
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue_;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;

    // スワップチェーン
    Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain_;
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc_{};
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 2> swapChainResources_;

    // 深度バッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilResource_;

    // デスクリプタヒープ
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap_;
    uint32_t descriptorSizeRTV_ = 0;
    uint32_t descriptorSizeSRV_ = 0;
    uint32_t descriptorSizeDSV_ = 0;

    // RTVハンドル
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 2> rtvHandles_{};

    // フェンス
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    uint64_t fenceValue_ = 0;
    HANDLE fenceEvent_ = nullptr;

    // ビューポート / シザリング矩形
    D3D12_VIEWPORT viewport_{};
    D3D12_RECT scissorRect_{};

    // 記録時間(FPS固定用)
    std::chrono::steady_clock::time_point reference_;

    // DXCコンパイラ
    Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils_;
    Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler_;
    Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler_;
};