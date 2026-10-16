#pragma once

#include "model/ParametricFeature.h"

#include <TopoDS_Shape.hxx>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace cad::parametric {

class Body final
{
public:
    using FeaturePtr = ParametricFeature::Ptr;

    void addFeature(const FeaturePtr& feature);
    void appendFeatures(const std::vector<FeaturePtr>& features);
    void insertFeature(std::size_t position, const FeaturePtr& feature);
    bool canRemoveFeature(const std::string& featureId) const;
    bool removeFeature(const std::string& featureId);

    FeaturePtr findFeature(const std::string& featureId) const;

    const std::vector<FeaturePtr>& features() const noexcept;
    const TopoDS_Shape& shape() const noexcept;

    bool recompute();
    void setRecomputeDeferred(bool deferred) noexcept { recomputeDeferred_ = deferred; }
    bool recomputeDeferred() const noexcept { return recomputeDeferred_; }
    void beginIncrementalRecompute();
    bool recomputeFeature(std::size_t index);
    bool finishIncrementalRecompute();
    void markDirtyFrom(const std::string& featureId);

    const std::string& lastError() const noexcept;
    std::uint64_t geometryRevision() const noexcept { return geometryRevision_; }
    std::uint64_t featureGeometryRevision(const std::string& featureId) const noexcept;

private:
    std::vector<FeaturePtr> features_;
    TopoDS_Shape resultShape_;
    std::string lastError_;
    std::uint64_t geometryRevision_{0};
    bool recomputeDeferred_{false};
};

} // namespace cad::parametric
