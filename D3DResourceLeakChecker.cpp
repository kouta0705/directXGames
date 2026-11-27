#include "D3DResourceLeakChecker.h"
#include <dxgi1_6.h>
#include <dxgidebug.h>
#include <wrl.h>

#pragma comment(lib, "dxguid.lib")

D3DResourceLeakChecker::~D3DResourceLeakChecker()
{
    // リソースリークチェック
    Microsoft::WRL::ComPtr<IDXGIDebug1> debug;
    if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(debug.GetAddressOf())))) {
        debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
        debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
        debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
    }
}