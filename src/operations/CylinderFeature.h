#pragma once

#include "model/Feature.h"

class CylinderFeature final : public Feature
{
public:
    CylinderFeature(double radius, double height);

    void recompute() override;

private:
    double radius_;
    double height_;
};
