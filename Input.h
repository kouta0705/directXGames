#pragma once
#include <Windows.h>
#include <wrl.h>

#define DIRECTINPUT_VERSION 0x0800 // DirectInputのバージョン指定
#include <dinput.h>

#include "WinApp.h" // ★追加: WinAppのヘッダをインクルード(依存関係)

// 入力
class Input {
public:
    // namespace省略
    template <class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

public:
    // 初期化
    void Initialize(WinApp* winApp); // ★変更: 引数をWinAppのポインタに差し替え
    // 更新
    void Update();

    /// <summary>
    /// キーの押下をチェック
    /// </summary>
    /// <param name="keyNumber">キー番号( 例 DIK_0 )</param>
    /// <returns>押されているか</returns>
    bool PushKey(BYTE keyNumber);

    /// <summary>
    /// キーのトリガーをチェック
    /// </summary>
    /// <param name="keyNumber">キー番号( 例 DIK_0 )</param>
    /// <returns>トリガーか</returns>
    bool TriggerKey(BYTE keyNumber);

private:
    // キーボードのデバイス
    ComPtr<IDirectInputDevice8> keyboard;
    // DirectInputのインスタンス
    ComPtr<IDirectInput8> directInput;
    // 全キーの状態
    BYTE key[256] = {};
    // 前回の全キーの状態
    BYTE keyPre[256] = {};

    // ★追加: WindowsAPI(借りてきたインスタンス。持ち主は main.cpp)
    WinApp* winApp = nullptr;
};