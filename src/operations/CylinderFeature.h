#pragma once

#include "model/Feature.h"

class CylinderFeature final : public Feature
{
public:
    CylinderFeature(double radius, double height);

    void recompute() override;
    [[nodiscard]] double radius() const noexcept { return radius_; }
    [[nodiscard]] double height() const noexcept { return height_; }

private:
    double radius_;
    double height_;
};
