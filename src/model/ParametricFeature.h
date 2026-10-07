#pragma once

#include <TopoDS_Shape.hxx>
#include <gp_Trsf.hxx>
#include <QJsonObject>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace cad::parametric {

enum class FeatureState
{
    Dirty,
    UpToDate,
    Failed
};

enum class FeatureRole
{
    Generic,
    Sketch,
    Face
};

using PropertyValue = std::variant<bool, int, double, std::string>;

struct FeatureProperty
{
    using Value = PropertyValue;

    std::string key;
    std::string label;
    Value value;
    std::optional<double> minimum;
    std::optional<double> maximum;
    bool editable{false};
};

/**
 * Base class for the new parametric modeling layer.
 *
 * It remains next to the legacy Feature class for source and file compatibility;
 * the application runtime uses ParametricFeature for canonical model features.
 */
class ParametricFeature
{
public:
    using Ptr = std::shared_ptr<ParametricFeature>;

    ParametricFeature(std::string id, std::string name);
    virtual ~ParametricFeature() = default;

    const std::string& id() const noexcept;
    const std::string& name() const noexcept;
    void setName(std::string name);

    FeatureState state() const noexcept;
    bool isDirty() const noexcept;
    const std::string& error() const noexcept;

    virtual const char* typeId() const noexcept;
    virtual FeatureRole role() const noexcept;
    virtual std::string creationLabel() const;
    virtual std::vector<FeatureProperty> properties() const;
    virtual std::vector<std::string> hiddenDependencyIds() const;
    virtual Ptr clone(std::string newId) const;
    virtual bool setProperty(const std::string& key, const PropertyValue& value);
    virtual bool setNumericProperty(const std::string& key, double value);
    QJsonObject serialize() const;

    const TopoDS_Shape& shape() const noexcept;
    const gp_Trsf& placement() const noexcept;
    void setPlacement(const gp_Trsf& placement);
    bool userVisible() const noexcept;
    void setUserVisible(bool visible) noexcept;

    const std::vector<std::weak_ptr<ParametricFeature>>& dependencies() const noexcept;
    void addDependency(const Ptr& dependency);

    void markDirty() noexcept;
    bool recompute();

protected:
    virtual TopoDS_Shape build() const = 0;
    virtual void writeParameters(QJsonObject& object) const;
    void copyPlacementTo(const Ptr& feature) const;
    void clearDependencies() noexcept { dependencies_.clear(); }

private:
    std::string id_;
    std::string name_;
    TopoDS_Shape shape_;
    gp_Trsf placement_;
    bool userVisible_{true};
    FeatureState state_{FeatureState::Dirty};
    std::string error_;
    std::vector<std::weak_ptr<ParametricFeature>> dependencies_;
};

} // namespace cad::parametric
