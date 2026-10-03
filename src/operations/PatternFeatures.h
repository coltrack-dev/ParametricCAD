#pragma once

#include "model/ParametricFeature.h"

#include <gp_Vec.hxx>

#include <string>
#include <vector>

namespace cad::parametric {

enum class PathPatternDistribution
{
    FixedSpacing,
    FitCount
};

enum class PathPatternOrientation
{
    Fixed,
    Tangent
};

class LinearPatternFeature final : public ParametricFeature
{
public:
    LinearPatternFeature(
        std::string id,
        std::vector<Ptr> sources,
        gp_Vec direction,
        double spacing,
        int count,
        bool includeSource
    );

    const char* typeId() const noexcept override { return "LinearPattern"; }
    std::string creationLabel() const override { return "Linear Pattern"; }
    std::vector<FeatureProperty> properties() const override;
    std::vector<std::string> hiddenDependencyIds() const override;
    bool setProperty(const std::string& key, const PropertyValue& value) override;
    bool setNumericProperty(const std::string& key, double value) override;
    Ptr clone(std::string newId) const override;

    const std::vector<std::string>& sourceIds() const noexcept { return sourceIds_; }
    const gp_Vec& direction() const noexcept { return direction_; }
    double spacing() const noexcept { return spacing_; }
    int count() const noexcept { return count_; }
    bool includeSource() const noexcept { return includeSource_; }

protected:
    TopoDS_Shape build() const override;
    void writeParameters(QJsonObject& object) const override;

private:
    std::vector<std::string> sourceIds_;
    std::vector<std::weak_ptr<ParametricFeature>> sources_;
    gp_Vec direction_;
    double spacing_;
    int count_;
    bool includeSource_;
};

class PathPatternFeature final : public ParametricFeature
{
public:
    PathPatternFeature(
        std::string id,
        std::vector<Ptr> sources,
        const Ptr& path,
        double spacing,
        int count,
        PathPatternDistribution distribution,
        PathPatternOrientation orientation,
        double startOffset,
        double endOffset,
        bool includeSource
    );

    const char* typeId() const noexcept override { return "PathPattern"; }
    std::string creationLabel() const override { return "Path Pattern"; }
    std::vector<FeatureProperty> properties() const override;
    std::vector<std::string> hiddenDependencyIds() const override;
    bool setProperty(const std::string& key, const PropertyValue& value) override;
    bool setNumericProperty(const std::string& key, double value) override;
    Ptr clone(std::string newId) const override;

    const std::vector<std::string>& sourceIds() const noexcept { return sourceIds_; }
    const std::string& pathId() const noexcept { return pathId_; }
    double spacing() const noexcept { return spacing_; }
    int count() const noexcept { return count_; }
    PathPatternDistribution distribution() const noexcept { return distribution_; }
    PathPatternOrientation orientation() const noexcept { return orientation_; }
    double startOffset() const noexcept { return startOffset_; }
    double endOffset() const noexcept { return endOffset_; }
    bool includeSource() const noexcept { return includeSource_; }

protected:
    TopoDS_Shape build() const override;
    void writeParameters(QJsonObject& object) const override;

private:
    std::vector<std::string> sourceIds_;
    std::vector<std::weak_ptr<ParametricFeature>> sources_;
    std::string pathId_;
    std::weak_ptr<ParametricFeature> path_;
    double spacing_;
    int count_;
    PathPatternDistribution distribution_;
    PathPatternOrientation orientation_;
    double startOffset_;
    double endOffset_;
    bool includeSource_;
};

} // namespace cad::parametric
