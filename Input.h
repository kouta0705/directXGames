#pragma once
#include <Windows.h>
#include <wrl.h>

#define DIRECTINPUT_VERSION 0x0800 // DirectInputのバージョン指定
#include <dinput.h>

// 入力
class Input {
public:
    // namespace省略
    template <class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

public:
    // 初期化
    void Initialize(HINSTANCE hInstance, HWND hwnd);
    // 更新
    void Update();

    
    bool PushKey(BYTE keyNumber);

 
    bool TriggerKey(BYTE keyNumber);

private:
  
    ComPtr<IDirectInputDevice8> keyboard;
   
    ComPtr<IDirectInput8> directInput;
    // 全キーの状態
    BYTE key[256] = {};
    // 前回の全キーの状態
    BYTE keyPre[256] = {};
};