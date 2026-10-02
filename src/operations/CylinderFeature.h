#pragma once

#include "model/Feature.h"

class CylinderFeature final : public Feature
{
public:
    CylinderFeature(double radius, double height);

    void recompute() override;
    std::shared_ptr<cad::parametric::ParametricFeature>
    toParametricFeature(const std::string& id) const override;
    const char* legacyIdPrefix() const noexcept override { return "legacy-cylinder-"; }
    [[nodiscard]] double radius() const noexcept { return radius_; }
    [[nodiscard]] double height() const noexcept { return height_; }

private:
    double radius_;
    double height_;
};
