#pragma once

// Board-local, allocation-free math. Units: acceleration in g, angular rate in
// rad/s, time in seconds. q rotates the sensor frame into the gravity frame.
#include <algorithm>
#include <array>
#include <cmath>

namespace dsh {
constexpr float kPi = 3.14159265358979323846f;
struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3 operator+(Vec3 b) const { return {x + b.x, y + b.y, z + b.z}; }
    Vec3 operator-(Vec3 b) const { return {x - b.x, y - b.y, z - b.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    float Norm() const { return std::sqrt(x * x + y * y + z * z); }
    bool Finite() const { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z); }
};
struct Quaternion {
    float w = 1, x = 0, y = 0, z = 0;
    Quaternion operator*(Quaternion b) const {
        return {w * b.w - x * b.x - y * b.y - z * b.z, w * b.x + x * b.w + y * b.z - z * b.y,
                w * b.y - x * b.z + y * b.w + z * b.x, w * b.z + x * b.y - y * b.x + z * b.w};
    }
    Quaternion Conjugate() const { return {w, -x, -y, -z}; }
    void Normalize() {
        float n = std::sqrt(w * w + x * x + y * y + z * z);
        if (n > 1e-8f) {
            w /= n;
            x /= n;
            y /= n;
            z /= n;
        } else {
            *this = {};
        }
    }
    static Quaternion Rotation(Vec3 v) {
        float angle = v.Norm();
        float s = angle < 1e-6f ? 0.5f : std::sin(angle * 0.5f) / angle;
        return {std::cos(angle * 0.5f), v.x * s, v.y * s, v.z * s};
    }
    Vec3 Gravity() const {
        return {2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)};
    }
    Vec3 EulerDegrees() const {
        constexpr float d = 180 / kPi;
        return {std::atan2(2 * (w * x + y * z), 1 - 2 * (x * x + y * y)) * d,
                std::asin(std::clamp(2 * (w * y - z * x), -1.0f, 1.0f)) * d,
                std::atan2(2 * (w * z + x * y), 1 - 2 * (y * y + z * z)) * d};
    }
};

// Multiplicative error-state EKF: nominal quaternion + gyro bias, with a 6x6
// covariance for [small body rotation, bias]. Gravity cannot observe yaw.
class AttitudeFilter {
public:
    void Reset() { *this = AttitudeFilter(); }
    const Quaternion& orientation() const { return q_; }
    Vec3 bias() const { return bias_; }
    void SetBias(Vec3 bias) { bias_ = bias; }
    bool ready() const { return ready_; }
    float Variance(int i) const { return p_[i][i]; }
    bool Update(Vec3 accel, Vec3 gyro, float dt) {
        if (!accel.Finite() || !gyro.Finite() || !std::isfinite(dt) || dt <= 0 || dt > 0.1f)
            return false;
        float norm = accel.Norm();
        if (!ready_) {
            if (norm < 0.85f || norm > 1.15f)
                return false;
            Vec3 a = accel * (1 / norm);
            // Shortest rotation taking measured gravity onto world +Z.
            q_ = a.z < -0.9999f ? Quaternion{0, 1, 0, 0} : Quaternion{1 + a.z, a.y, -a.x, 0};
            q_.Normalize();
            for (int i = 0; i < 6; ++i)
                p_[i][i] = i < 3 ? 0.03f : 0.0025f;
            ready_ = true;
        }
        Vec3 rate = gyro - bias_;
        q_ = q_ * Quaternion::Rotation(rate * dt);
        q_.Normalize();
        float f[6][6] = {};
        for (int i = 0; i < 6; ++i)
            f[i][i] = 1;
        float omega[3][3];
        Skew(rate, omega);
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j)
                f[i][j] -= omega[i][j] * dt;
            f[i][i + 3] = -dt;
        }
        float fp[6][6] = {}, predicted[6][6] = {};
        Multiply(f, p_, fp);
        for (int i = 0; i < 6; ++i)
            for (int j = 0; j < 6; ++j)
                for (int k = 0; k < 6; ++k)
                    predicted[i][j] += fp[i][k] * f[j][k];
        for (int i = 0; i < 6; ++i)
            predicted[i][i] += (i < 3 ? 0.0004f : 0.000001f) * dt;
        Copy(predicted, p_);
        // During translation/freefall/shaking, do not mistake acceleration for
        // gravity. Gyro prediction continues; accepted updates use adaptive R.
        if (norm < 0.85f || norm > 1.15f)
            return true;
        Vec3 gravity = q_.Gravity();
        Vec3 residual = accel * (1 / norm) - gravity;
        if (residual.Norm() > 0.6f)
            return true;
        float h[3][3];
        Skew(gravity, h);
        float ph[6][3] = {}, s[3][3] = {}, inv[3][3] = {};
        float r = 0.0025f + 0.5f * (norm - 1) * (norm - 1);
        for (int i = 0; i < 6; ++i)
            for (int j = 0; j < 3; ++j)
                for (int k = 0; k < 3; ++k)
                    ph[i][j] += p_[i][k] * h[j][k];
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) {
                for (int k = 0; k < 3; ++k)
                    s[i][j] += h[i][k] * ph[k][j];
                if (i == j)
                    s[i][j] += r;
            }
        if (!Invert(s, inv))
            return true;
        float gain[6][3] = {}, dx[6] = {};
        float innovation[3] = {residual.x, residual.y, residual.z};
        for (int i = 0; i < 6; ++i)
            for (int j = 0; j < 3; ++j) {
                for (int k = 0; k < 3; ++k)
                    gain[i][j] += ph[i][k] * inv[k][j];
                dx[i] += gain[i][j] * innovation[j];
            }
        q_ = q_ * Quaternion::Rotation({dx[0], dx[1], dx[2]});
        q_.Normalize();
        bias_ = bias_ + Vec3{dx[3], dx[4], dx[5]};
        // Joseph form maintains positive covariance under float roundoff.
        float a[6][6] = {}, ap[6][6] = {}, corrected[6][6] = {};
        for (int i = 0; i < 6; ++i)
            for (int j = 0; j < 6; ++j) {
                a[i][j] = i == j ? 1 : 0;
                if (j < 3)
                    for (int k = 0; k < 3; ++k)
                        a[i][j] -= gain[i][k] * h[k][j];
            }
        Multiply(a, p_, ap);
        for (int i = 0; i < 6; ++i)
            for (int j = 0; j < 6; ++j) {
                for (int k = 0; k < 6; ++k)
                    corrected[i][j] += ap[i][k] * a[j][k];
                for (int k = 0; k < 3; ++k)
                    corrected[i][j] += gain[i][k] * r * gain[j][k];
            }
        // Reset error coordinates after injecting the small rotation.
        float reset[6][6] = {}, skew[3][3], temp[6][6] = {};
        Skew({dx[0], dx[1], dx[2]}, skew);
        for (int i = 0; i < 6; ++i)
            reset[i][i] = 1;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                reset[i][j] -= 0.5f * skew[i][j];
        Multiply(reset, corrected, temp);
        float result[6][6] = {};
        for (int i = 0; i < 6; ++i)
            for (int j = 0; j < 6; ++j)
                for (int k = 0; k < 6; ++k)
                    result[i][j] += temp[i][k] * reset[j][k];
        for (int i = 0; i < 6; ++i)
            for (int j = 0; j < 6; ++j)
                p_[i][j] = 0.5f * (result[i][j] + result[j][i]);
        return true;
    }

private:
    Quaternion q_;
    Vec3 bias_;
    float p_[6][6] = {};
    bool ready_ = false;
    static void Skew(Vec3 v, float m[3][3]) {
        m[0][0] = m[1][1] = m[2][2] = 0;
        m[0][1] = -v.z;
        m[0][2] = v.y;
        m[1][0] = v.z;
        m[1][2] = -v.x;
        m[2][0] = -v.y;
        m[2][1] = v.x;
    }
    static void Copy(const float a[6][6], float b[6][6]) {
        for (int i = 0; i < 6; ++i)
            for (int j = 0; j < 6; ++j)
                b[i][j] = a[i][j];
    }
    static void Multiply(const float a[6][6], const float b[6][6], float c[6][6]) {
        for (int i = 0; i < 6; ++i)
            for (int j = 0; j < 6; ++j)
                for (int k = 0; k < 6; ++k)
                    c[i][j] += a[i][k] * b[k][j];
    }
    static bool Invert(const float a[3][3], float b[3][3]) {
        float det = a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) -
                    a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) +
                    a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
        if (!std::isfinite(det) || std::abs(det) < 1e-12f)
            return false;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                b[j][i] = (a[(i + 1) % 3][(j + 1) % 3] * a[(i + 2) % 3][(j + 2) % 3] -
                           a[(i + 1) % 3][(j + 2) % 3] * a[(i + 2) % 3][(j + 1) % 3]) /
                          det;
        return true;
    }
};

}  // namespace dsh
