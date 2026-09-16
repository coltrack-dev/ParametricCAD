#pragma once

#include "model/ParametricFeature.h"

#include <TopoDS_Shape.hxx>

#include <memory>
#include <string>
#include <vector>

namespace cad::parametric {

class Body final
{
public:
    using FeaturePtr = ParametricFeature::Ptr;

    void addFeature(const FeaturePtr& feature);
    void insertFeature(std::size_t position, const FeaturePtr& feature);
    bool canRemoveFeature(const std::string& featureId) const;
    bool removeFeature(const std::string& featureId);

    FeaturePtr findFeature(const std::string& featureId) const;

    const std::vector<FeaturePtr>& features() const noexcept;
    const TopoDS_Shape& shape() const noexcept;

    bool recompute();
    void markDirtyFrom(const std::string& featureId);

    const std::string& lastError() const noexcept;

private:
    std::vector<FeaturePtr> features_;
    TopoDS_Shape resultShape_;
    std::string lastError_;
};

} // namespace cad::parametric
