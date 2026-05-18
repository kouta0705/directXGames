#include <d3d12.h>
#include<Windows.h>
#include<cstdint>
#include<dxgi1_6.h>
#include <cassert>
#include <string>
#include <format>


#pragma comment(lib,"d3d12.lib")
#pragma comment(lib,"dxgi.lib")


std::wstring ConvertString(const std::string& str) {
	if (str.empty()) {
		return std::wstring();
	}

	auto sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), NULL, 0);
	if (sizeNeeded == 0) {
		return std::wstring();
	}
	std::wstring result(sizeNeeded, 0);
	MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), &result[0], sizeNeeded);
	return result;
}

std::string ConvertString(const std::wstring& str) {
	if (str.empty()) {
		return std::string();
	}

	auto sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), NULL, 0, NULL, NULL);
	if (sizeNeeded == 0) {
		return std::string();
	}
	std::string result(sizeNeeded, 0);
	WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), sizeNeeded, NULL, NULL);
	return result;
}




LRESULT CALLBACK WndowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	//windowsアプリでのエントリーポイント(main関数)

	

	switch (msg)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

	}




	return DefWindowProc(hwnd, msg, wParam, lParam);
}




std::string str0{ "STRING!!!" };

std::string str1{ std::to_string(10) };

void Log(const std::string& Message)
{
	OutputDebugStringA(Message.c_str());
}


int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow) {


	WNDCLASS wc{};

	wc.lpfnWndProc = WndowProc;
	wc.lpszClassName = L"CG2WindowClass";
	wc.hInstance = GetModuleHandle(nullptr);
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	RegisterClass(&wc);

	const int32_t KClientWidth = 1280;
	const int32_t KClientHeight = 720;

	RECT wrc = { 0,0,KClientWidth,KClientHeight};

	AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);

	HWND hwnd = CreateWindow(

		wc.lpszClassName,
		L"CG2",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		wrc.right - wrc.left,
		wrc.bottom - wrc.top,
		nullptr,
		nullptr,
		wc.hInstance,
		nullptr);

	ShowWindow(hwnd, SW_SHOW);

	MSG msg{};
	
	int enemyHp;
	int texturePath;

	Log(std::format("enemyHp:{},texturePath:{}\n", enemyHp, texturePath));

	IDXGIFactory7* dxgiFactory = nullptr;

	HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory));

	assert(SUCCEEDED(hr));

	IDXGIAdapter4* useAdapter = nullptr;

	for (UINT i = 0;dxgiFactory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAdapter)) != DXGI_ERROR_NOT_FOUND; ++i)
	{
		DXGI_ADAPTER_DESC3 adapterDesc{};

		hr = useAdapter->GetDesc3(&adapterDesc);
		assert(SUCCEEDED(hr));

		if (!(adapterDesc.Flags&DXGI_ADAPTER_FLAG3_SOFTWARE))
		{
			Log(ConvertString(std::format(L"Use Adapter: {}\n", adapterDesc.Description)));
			break;
		}

		useAdapter = nullptr;
		
	}
	
	assert(useAdapter != nullptr);

	ID3D12Device* device = nullptr;
	D3D_FEATURE_LEVEL featureLevels[] =
	{ D3D_FEATURE_LEVEL_12_2,D3D_FEATURE_LEVEL_12_1,D3D_FEATURE_LEVEL_12_0 };

	const char* fetureLevelString[] = { "12.2","12.1","12.0" };

	for (size_t i = 0;i < std::size(featureLevels);++i)
	{
		hr = D3D12CreateDevice(useAdapter, featureLevels[i], IID_PPV_ARGS(&device));
		if (SUCCEEDED(hr))
		{
			Log(std::format("Feature Level: {}\n", fetureLevelString[i]));
			break;
		}
	}

	Log("conmplete Create D3D12Dvice\n");
	ID3D12CommandQueue* commandQueue = nullptr;
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	hr = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue));
	IID_PPV_ARGS(&commandQueue);
	assert(hr);

	ID3D12CommandAllocator* commandAllocator = nullptr;
	hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));

	assert(SUCCEEDED(hr));

	ID3D12GraphicsCommandList* commandList = nullptr;

	hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator, nullptr, IID_PPV_ARGS(&commandList));

	assert(SUCCEEDED(hr));

	IDXGISwapChain* swapChain = nullptr;
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
	swapChainDesc.Width = KClientWidth;
	swapChainDesc.Height = KClientHeight;
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



	while (msg.message != WM_QUIT)
	{
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{

		}
	}



	//出力ウィンドウへの文字出力
	OutputDebugStringA("Hello, DirectX!\n");
	return 0;

}
