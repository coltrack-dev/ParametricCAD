#pragma once
#include <cmath>

struct Vector3D
{
    double x{};
    double y{};
    double z{};

    [[nodiscard]] double length() const noexcept
    {
        return std::sqrt(x * x + y * y + z * z);
    }

    [[nodiscard]] double dot(const Vector3D& other) const noexcept
    {
        return x * other.x + y * other.y + z * other.z;
    }

    [[nodiscard]] Vector3D cross(const Vector3D& other) const noexcept
    {
        return {
            y * other.z - z * other.y,
            z * other.x - x * other.z,
            x * other.y - y * other.x
        };
    }
};
