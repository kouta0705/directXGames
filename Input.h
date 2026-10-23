#pragma once
#include <Windows.h>
#include <wrl.h>
#include <cstdint>

#define DIRECTINPUT_VERSION 0x0800
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

    /// <summary>
    /// キーの押下をチェック
    /// </summary>
    /// <param name="keyNumber">キー番号( DIK_0 等)</param>
    /// <returns>押されているか</returns>
    bool PushKey(BYTE keyNumber) const;

    /// <summary>
    /// キーのトリガーをチェック（押した瞬間）
    /// </summary>
    bool TriggerKey(BYTE keyNumber) const;

    /// <summary>
    /// キーを離した瞬間をチェック
    /// </summary>
    bool ReleaseKey(BYTE keyNumber) const;

private:
    // DirectInputのインスタンス
    ComPtr<IDirectInput8> directInput;
    // キーボードデバイス
    ComPtr<IDirectInputDevice8> keyboard;
    // 全キーの状態
    BYTE key[256] = {};
    // 前回の全キーの状態
    BYTE keyPre[256] = {};
};
