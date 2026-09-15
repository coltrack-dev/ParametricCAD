#include "geometry/Vector3D.h"

#include <cassert>
#include <cmath>

namespace
{
bool almostEqual(double left, double right, double epsilon = 1e-9)
{
    return std::abs(left - right) < epsilon;
}
}

int main()
{
    const Vector3D xAxis{1.0, 0.0, 0.0};
    const Vector3D yAxis{0.0, 1.0, 0.0};

    assert(almostEqual(xAxis.length(), 1.0));
    assert(almostEqual(xAxis.dot(yAxis), 0.0));

    const Vector3D zAxis = xAxis.cross(yAxis);
    assert(almostEqual(zAxis.x, 0.0));
    assert(almostEqual(zAxis.y, 0.0));
    assert(almostEqual(zAxis.z, 1.0));

    return 0;
}
