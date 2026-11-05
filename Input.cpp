#include "Input.h"
#include <cassert>
#include <cstring>

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

void Input::Initialize(WinApp* winApp)
{
    HRESULT result;

    // 借りてきたWinAppのインスタンスを記録
    this->winApp = winApp;

    // DirectInputのインスタンス生成
    result = DirectInput8Create(
        winApp->GetHInstance(), DIRECTINPUT_VERSION, IID_IDirectInput8,
        reinterpret_cast<void**>(directInput.GetAddressOf()), nullptr);
    assert(SUCCEEDED(result));

    // キーボードデバイス生成
    result = directInput->CreateDevice(GUID_SysKeyboard, keyboard.GetAddressOf(), nullptr);
    assert(SUCCEEDED(result));

    // 入力データ形式のセット
    result = keyboard->SetDataFormat(&c_dfDIKeyboard);
    assert(SUCCEEDED(result));

    // 排他制御レベルのセット
    result = keyboard->SetCooperativeLevel(
        winApp->GetHwnd(), DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY);
    assert(SUCCEEDED(result));
}

void Input::Update()
{
    // 前回のキー入力を保存
    std::memcpy(keyPre, key, sizeof(key));

    // キーボード情報の取得開始
    keyboard->Acquire();

    // 全キーの入力状態を取得する
    keyboard->GetDeviceState(sizeof(key), key);
}

bool Input::PushKey(BYTE keyNumber)
{
    // 指定キーを押していればtrueを返す
    if (key[keyNumber]) {
        return true;
    }
    // そうでなければfalseを返す
    return false;
}

bool Input::TriggerKey(BYTE keyNumber)
{
    // 前回は押されておらず、今回は押されていればtrueを返す
    if (!keyPre[keyNumber] && key[keyNumber]) {
        return true;
    }
    // そうでなければfalseを返す
    return false;
}