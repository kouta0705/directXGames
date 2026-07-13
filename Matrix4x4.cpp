#include "Matrix4x4.h"
#include <cmath>

Matrix4x4 MakeIdentity4x4() {
    Matrix4x4 result = {};
    result.m[0][0] = 1.0f;
    result.m[1][1] = 1.0f;
    result.m[2][2] = 1.0f;
    result.m[3][3] = 1.0f;
    return result;
}

Matrix4x4 MakeScaleMatrix(const Vector3& scale) {
    Matrix4x4 result = {};
    result.m[0][0] = scale.x;
    result.m[1][1] = scale.y;
    result.m[2][2] = scale.z;
    result.m[3][3] = 1.0f;
    return result;
}

Matrix4x4 MakeRotateXMatrix(float radian) {
    Matrix4x4 result = {};
    result.m[0][0] = 1.0f;
    result.m[1][1] = std::cosf(radian);
    result.m[1][2] = std::sinf(radian);
    result.m[2][1] = -std::sinf(radian);
    result.m[2][2] = std::cosf(radian);
    result.m[3][3] = 1.0f;
    return result;
}

Matrix4x4 MakeRotateYMatrix(float radian) {
    Matrix4x4 result = {};
    result.m[0][0] = std::cosf(radian);
    result.m[0][2] = -std::sinf(radian);
    result.m[1][1] = 1.0f;
    result.m[2][0] = std::sinf(radian);
    result.m[2][2] = std::cosf(radian);
    result.m[3][3] = 1.0f;
    return result;
}

Matrix4x4 MakeRotateZMatrix(float radian) {
    Matrix4x4 result = {};
    result.m[0][0] = std::cosf(radian);
    result.m[0][1] = std::sinf(radian);
    result.m[1][0] = -std::sinf(radian);
    result.m[1][1] = std::cosf(radian);
    result.m[2][2] = 1.0f;
    result.m[3][3] = 1.0f;
    return result;
}

Matrix4x4 MakeTranslateMatrix(const Vector3& translate) {
    Matrix4x4 result = MakeIdentity4x4();
    result.m[3][0] = translate.x;
    result.m[3][1] = translate.y;
    result.m[3][2] = translate.z;
    return result;
}

Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Vector3& rotate, const Vector3& translate) {
    Matrix4x4 S = MakeScaleMatrix(scale);
    Matrix4x4 Rx = MakeRotateXMatrix(rotate.x);
    Matrix4x4 Ry = MakeRotateYMatrix(rotate.y);
    Matrix4x4 Rz = MakeRotateZMatrix(rotate.z);
    Matrix4x4 T = MakeTranslateMatrix(translate);
    // S * Rx * Ry * Rz * T
    return Multiply(Multiply(Multiply(Multiply(S, Rx), Ry), Rz), T);
}

Matrix4x4 Multiply(const Matrix4x4& m1, const Matrix4x4& m2) {
    Matrix4x4 result = {};
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            for (int k = 0; k < 4; ++k) {
                result.m[row][col] += m1.m[row][k] * m2.m[k][col];
            }
        }
    }
    return result;
}

Matrix4x4 Inverse(const Matrix4x4& m) {
    // 余因子展開による逆行列（4x4）
    Matrix4x4 result = {};
    float b[4][8] = {};

    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            b[i][j] = m.m[i][j];
        }
        b[i][i + 4] = 1.0f;
    }

    for (int i = 0; i < 4; ++i) {
        float pivot = b[i][i];
        for (int j = 0; j < 8; ++j) {
            b[i][j] /= pivot;
        }
        for (int k = 0; k < 4; ++k) {
            if (k == i) continue;
            float factor = b[k][i];
            for (int j = 0; j < 8; ++j) {
                b[k][j] -= factor * b[i][j];
            }
        }
    }

    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            result.m[i][j] = b[i][j + 4];
        }
    }
    return result;
}

Matrix4x4 MakePerspectiveFovMatrix(float fovY, float aspectRatio, float nearClip, float farClip) {
    Matrix4x4 result = {};
    float tanHalfFov = std::tanf(fovY / 2.0f);
    result.m[0][0] = 1.0f / (aspectRatio * tanHalfFov);
    result.m[1][1] = 1.0f / tanHalfFov;
    result.m[2][2] = farClip / (farClip - nearClip);
    result.m[2][3] = 1.0f;
    result.m[3][2] = -(nearClip * farClip) / (farClip - nearClip);
    return result;
}

Matrix4x4 MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip) {
    Matrix4x4 result = {};
    result.m[0][0] = 2.0f / (right - left);
    result.m[1][1] = 2.0f / (top - bottom);
    result.m[2][2] = 1.0f / (farClip - nearClip);
    result.m[3][0] = (left + right) / (left - right);
    result.m[3][1] = (top + bottom) / (bottom - top);
    result.m[3][2] = nearClip / (nearClip - farClip);
    result.m[3][3] = 1.0f;
    return result;
}