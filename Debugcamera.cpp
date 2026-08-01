#include "DebugCamera.h"
#include "Matrix4x4.h"
#include <Windows.h>
#include <cmath>

// カメラ移動・回転の速度定数
static const float kPanSpeed    = 0.01f;   // 左クリック：左右上下移動の速度
static const float kRotSpeed    = 0.005f;  // 右クリック：回転の速度
static const float kZoomSpeed   = 0.5f;    // ホイール中ボタン：拡大縮小の速度

void DebugCamera::Initialize() {
    translation_ = { 0.0f, 0.0f, -10.0f };
    matRot_      = MakeIdentity4x4();
    viewMatrix_  = MakeIdentity4x4();
}

void DebugCamera::Update(HWND hwnd) {
    // -----------------------------------
    // マウス状態を取得
    // -----------------------------------
    POINT currentMousePos = {};
    GetCursorPos(&currentMousePos);

    bool lButton = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    bool rButton = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    bool mButton = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;

    // マウス移動量
    float dx = 0.0f;
    float dy = 0.0f;

    // いずれかのボタンが押されているときだけ移動量を計算
    if (lButton || rButton || mButton) {
        dx = static_cast<float>(currentMousePos.x - prevMousePos_.x);
        dy = static_cast<float>(currentMousePos.y - prevMousePos_.y);
    }

    // -----------------------------------
    // 左クリック押しながら：左右上下移動
    // -----------------------------------
    if (lButton) {
        // カメラのローカル軸方向に移動する
        // matRot_の列ベクトルがローカル軸
        // 右方向 = matRot_[0][0..2]
        // 上方向 = matRot_[1][0..2]
        Vector3 right = { matRot_.m[0][0], matRot_.m[0][1], matRot_.m[0][2] };
        Vector3 up    = { matRot_.m[1][0], matRot_.m[1][1], matRot_.m[1][2] };

        translation_.x -= right.x * dx * kPanSpeed;
        translation_.y -= right.y * dx * kPanSpeed;
        translation_.z -= right.z * dx * kPanSpeed;

        translation_.x += up.x * dy * kPanSpeed;
        translation_.y += up.y * dy * kPanSpeed;
        translation_.z += up.z * dy * kPanSpeed;
    }

    // -----------------------------------
    // 右クリック押しながら：3D回転（X軸・Y軸）
    // -----------------------------------
    if (rButton) {
        // 今回フレームの追加回転行列を生成
        Matrix4x4 matRotDelta = MakeIdentity4x4();
        matRotDelta = Multiply(matRotDelta, MakeRotateXMatrix(dy * kRotSpeed));
        matRotDelta = Multiply(matRotDelta, MakeRotateYMatrix(dx * kRotSpeed));

        // 累積回転行列に合成（資料の式: 累積 = 今回 x 前回の累積）
        matRot_ = Multiply(matRotDelta, matRot_);
    }

    // -----------------------------------
    // ホイール中ボタン押しながら：前後移動（拡大縮小）
    // -----------------------------------
    if (mButton) {
        // 前方向 = matRot_[2][0..2]
        Vector3 forward = { matRot_.m[2][0], matRot_.m[2][1], matRot_.m[2][2] };

        translation_.x -= forward.x * dy * kZoomSpeed;
        translation_.y -= forward.y * dy * kZoomSpeed;
        translation_.z -= forward.z * dy * kZoomSpeed;
    }

    // -----------------------------------
    // ビュー行列の更新
    // -----------------------------------
    // 座標から平行移動行列を計算
    Matrix4x4 matTrans = MakeTranslateMatrix(translation_);

    // 累積回転行列と平行移動行列からワールド行列を計算
    Matrix4x4 matWorld = Multiply(matRot_, matTrans);

    // ワールド行列の逆行列をビュー行列に代入
    viewMatrix_ = Inverse(matWorld);

    // -----------------------------------
    // 前フレームの状態を保存
    // -----------------------------------
    prevMousePos_  = currentMousePos;
    prevLButton_   = lButton;
    prevRButton_   = rButton;
    prevMButton_   = mButton;
}