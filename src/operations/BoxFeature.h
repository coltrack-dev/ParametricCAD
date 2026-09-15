#pragma once

#include "model/Feature.h"

class BoxFeature final : public Feature
{
public:
    BoxFeature(double width, double depth, double height);

    void recompute() override;

    [[nodiscard]] double width() const noexcept;
    [[nodiscard]] double depth() const noexcept;
    [[nodiscard]] double height() const noexcept;

private:
    double width_;
    double depth_;
    double height_;
};
