// Motusy Moto Box — podstawowa algebra wektorowa.
//
// Czyste C++ bez zaleznosci od sprzetu ani Arduino: ten naglowek kompiluje sie
// zarowno w firmware, jak i w srodowisku native (testy, replay).

#pragma once

#include <cmath>

namespace motion {

/// Przyspieszenie ziemskie [m/s^2]. Uzywane do przeliczen na jednostki g.
constexpr float kGravity = 9.80665f;

constexpr float kPi = 3.14159265358979323846f;

constexpr float radToDeg(float rad) { return rad * (180.0f / kPi); }
constexpr float degToRad(float deg) { return deg * (kPi / 180.0f); }

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    constexpr Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }

    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }

    constexpr float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }

    constexpr Vec3 cross(const Vec3& o) const {
        return {y * o.z - z * o.y,
                z * o.x - x * o.z,
                x * o.y - y * o.x};
    }

    float norm() const { return std::sqrt(dot(*this)); }

    /// Wektor jednostkowy. Dla wektora zerowego zwraca (0,0,0) zamiast NaN —
    /// wywolujacy powinien sprawdzic norm() zanim uzna wynik za sensowny.
    Vec3 normalized() const {
        const float n = norm();
        if (n < 1e-9f) return {};
        return {x / n, y / n, z / n};
    }
};

/// Macierz 3x3 przechowywana wierszami. Sluzy do transformacji miedzy ukladem
/// urzadzenia a ukladem motocykla (patrz MountCalibration).
struct Mat3 {
    Vec3 row0{1.0f, 0.0f, 0.0f};
    Vec3 row1{0.0f, 1.0f, 0.0f};
    Vec3 row2{0.0f, 0.0f, 1.0f};

    /// Mnozenie macierz * wektor.
    Vec3 operator*(const Vec3& v) const {
        return {row0.dot(v), row1.dot(v), row2.dot(v)};
    }
};

/// Pojedyncza probka z IMU w ukladzie **urzadzenia**, przed kalibracja montazu.
struct ImuSample {
    /// Sila wlasciwa mierzona przez akcelerometr [g]. W spoczynku ma modul 1.0
    /// i jest skierowana w gore (przeciwnie do grawitacji).
    Vec3 accelG;

    /// Predkosc katowa [rad/s].
    Vec3 gyroRadS;

    /// Znacznik czasu probki [ms od startu].
    unsigned long timestampMs = 0;
};

}  // namespace motion
