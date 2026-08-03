#include "DebugCamera.h"
#include "Matrix4x4.h"
#include <Windows.h>
#include <cmath>

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

// カメラ移動・回転の速度定数
static const float kPanSpeed = 0.01f;   // 左クリック：左右上下移動の速度
static const float kZoomSpeed = 0.5f;   // ホイール中ボタン：拡大縮小の速度

void DebugCamera::Initialize() {
    translation_ = { 0.0f, 0.0f, -10.0f };
    matRot_ = MakeIdentity4x4();
    viewMatrix_ = MakeIdentity4x4();
}

void DebugCamera::Update(HWND hwnd) {
#ifdef USE_IMGUI
    // ImGui側がマウスを使用中(スライダー操作等)はカメラ入力として扱わない
    if (ImGui::GetIO().WantCaptureMouse) {
        GetCursorPos(&prevMousePos_);
        prevLButton_ = false;
        prevMButton_ = false;
        return;
    }
#endif

    // マウス状態を取得（右クリックは使用しない）
    POINT currentMousePos = {};
    GetCursorPos(&currentMousePos);

    bool lButton = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    bool mButton = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
 


    float dx = 0.0f;
    float dy = 0.0f;
    if (lButton || mButton) {
        dx = static_cast<float>(currentMousePos.x - prevMousePos_.x);
        dy = static_cast<float>(currentMousePos.y - prevMousePos_.y);
    }

    // 左クリック押しながら：左右上下移動
    if (lButton) {
        Vector3 right = { matRot_.m[0][0], matRot_.m[0][1], matRot_.m[0][2] };
        Vector3 up = { matRot_.m[1][0], matRot_.m[1][1], matRot_.m[1][2] };

        translation_.x -= right.x * dx * kPanSpeed;
        translation_.y -= right.y * dx * kPanSpeed;
        translation_.z -= right.z * dx * kPanSpeed;

        translation_.x += up.x * dy * kPanSpeed;
        translation_.y += up.y * dy * kPanSpeed;
        translation_.z += up.z * dy * kPanSpeed;
    }

    // ホイール中ボタン押しながら：前後移動（拡大縮小）
    if (mButton) {
        Vector3 forward = { matRot_.m[2][0], matRot_.m[2][1], matRot_.m[2][2] };

        translation_.x -= forward.x * dy * kZoomSpeed;
        translation_.y -= forward.y * dy * kZoomSpeed;
        translation_.z -= forward.z * dy * kZoomSpeed;
    }

    // ビュー行列の更新
    Matrix4x4 matTrans = MakeTranslateMatrix(translation_);
    Matrix4x4 matWorld = Multiply(matRot_, matTrans);
    viewMatrix_ = Inverse(matWorld);

    // 前フレームの状態を保存
    prevMousePos_ = currentMousePos;
    prevLButton_ = lButton;
    prevMButton_ = mButton;
}