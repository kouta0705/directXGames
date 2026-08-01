#pragma once
#include <Windows.h> 
#include "Matrix4x4.h"
#include "Vector3.h"

/// <summary>
/// デバッグカメラ
/// </summary>
class DebugCamera {
public:
    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize();

    /// <summary>
    /// 更新
    /// </summary>
    /// <param name="hwnd">ウィンドウハンドル（マウス入力取得用）</param>
    void Update(HWND hwnd);

    // ビュー行列を取得
    const Matrix4x4& GetViewMatrix() const { return viewMatrix_; }

private:
    // ローカル座標
    Vector3 translation_ = { 0.0f, 0.0f, -10.0f };

    // 累積回転行列
    Matrix4x4 matRot_;

    // ビュー行列
    Matrix4x4 viewMatrix_;

    // 射影行列（通常カメラと兼用するので持たない）

    // マウス前フレーム座標
    POINT prevMousePos_ = {};

    // マウスボタンの前フレーム状態
    bool prevLButton_ = false;
    bool prevRButton_ = false;
    bool prevMButton_ = false;
};