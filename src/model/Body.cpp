#include "model/Body.h"

#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopAbs_ShapeEnum.hxx>

#include <algorithm>
#include <unordered_map>
#include <stdexcept>
#include <unordered_set>

namespace cad::parametric {

namespace {

void updateComponentGeometry(ModelComponent& component, const TopoDS_Shape& shape)
{
    Bnd_Box bounds;
    BRepBndLib::Add(shape, bounds);
    if (!bounds.IsVoid()) {
        bounds.Get(component.minX, component.minY, component.minZ,
                   component.maxX, component.maxY, component.maxZ);
    }
    GProp_GProps properties;
    BRepGProp::VolumeProperties(shape, properties);
    component.volume = properties.Mass();
}

std::vector<TopoDS_Shape> solidsOf(const TopoDS_Shape& shape)
{
    std::vector<TopoDS_Shape> solids;
    if (shape.IsNull()) return solids;
    if (shape.ShapeType() == TopAbs_SOLID) {
        solids.push_back(shape);
        return solids;
    }
    for (TopExp_Explorer explorer(shape, TopAbs_SOLID);
         explorer.More(); explorer.Next()) {
        solids.push_back(explorer.Current());
    }
    return solids;
}

}

void Body::addFeature(const FeaturePtr& feature)
{
    insertFeature(features_.size(), feature);
}

void Body::appendFeatures(const std::vector<FeaturePtr>& features)
{
    std::unordered_set<std::string> ids;
    ids.reserve(features_.size() + features.size());
    for (const auto& feature : features_) ids.insert(feature->id());
    for (const auto& feature : features) {
        if (!feature) throw std::invalid_argument("Body feature must not be null");
        if (!ids.insert(feature->id()).second)
            throw std::invalid_argument("Feature with id '" + feature->id() + "' already exists");
        for (const auto& weak : feature->dependencies()) {
            const auto dependency = weak.lock();
            if (!dependency || !ids.contains(dependency->id()))
                throw std::invalid_argument("Dependencies must precede feature '" + feature->id() + "'");
        }
    }
    features_.insert(features_.end(), features.begin(), features.end());
    if (!features.empty()) ++geometryRevision_;
}

void Body::insertFeature(std::size_t position, const FeaturePtr& feature)
{
    if (!feature) throw std::invalid_argument("Body feature must not be null");
    if (position > features_.size()) throw std::out_of_range("Invalid feature position");
    if (findFeature(feature->id())) {
        throw std::invalid_argument("Feature with id '" + feature->id() + "' already exists");
    }
    const auto insertion = features_.begin() + static_cast<std::ptrdiff_t>(position);
    for (const auto& weak : feature->dependencies()) {
        const auto dependency = weak.lock();
        if (!dependency || std::find(features_.begin(), insertion, dependency) == insertion) {
            throw std::invalid_argument("Dependencies must precede feature '" + feature->id() + "'");
        }
    }
    features_.insert(insertion, feature);
    ++geometryRevision_;
}

bool Body::canRemoveFeature(const std::string& featureId) const
{
    const auto source = findFeature(featureId);
    if (!source) return false;
    for (const auto& feature : features_) {
        for (const auto& dependency : feature->dependencies()) {
            if (dependency.lock() == source) return false;
        }
    }
    return true;
}

bool Body::removeFeature(const std::string& featureId)
{
    const auto iterator = std::find_if(
        features_.begin(),
        features_.end(),
        [&featureId](const FeaturePtr& feature) {
            return feature->id() == featureId;
        }
    );

    if (iterator == features_.end()) {
        return false;
    }

    if (!canRemoveFeature(featureId)) {
        throw std::invalid_argument("Remove dependent features before deleting '" + featureId + "'");
    }
    features_.erase(iterator);
    resultShape_.Nullify();
    ++geometryRevision_;
    return true;
}

Body::FeaturePtr Body::findFeature(const std::string& featureId) const
{
    const auto iterator = std::find_if(
        features_.begin(),
        features_.end(),
        [&featureId](const FeaturePtr& feature) {
            return feature->id() == featureId;
        }
    );

    return iterator == features_.end()
        ? FeaturePtr{}
        : *iterator;
}

const std::vector<Body::FeaturePtr>& Body::features() const noexcept
{
    return features_;
}

ModelComponent* Body::findComponent(const std::string& componentId) noexcept
{
    auto iterator = std::find_if(components_.begin(), components_.end(),
        [&componentId](const ModelComponent& component) { return component.id == componentId; });
    return iterator == components_.end() ? nullptr : &*iterator;
}

const ModelComponent* Body::findComponent(const std::string& componentId) const noexcept
{
    auto iterator = std::find_if(components_.begin(), components_.end(),
        [&componentId](const ModelComponent& component) { return component.id == componentId; });
    return iterator == components_.end() ? nullptr : &*iterator;
}

void Body::replaceComponents(std::vector<ModelComponent> components)
{
    std::unordered_set<std::string> ids;
    for (const auto& component : components) {
        if (component.id.empty() || !ids.insert(component.id).second)
            throw std::invalid_argument("Component ids must be non-empty and unique");
        if (!findFeature(component.sourceFeatureId))
            throw std::invalid_argument("Component source feature is missing");
    }
    components_ = std::move(components);
}

const TopoDS_Shape& Body::shape() const noexcept
{
    return resultShape_;
}

bool Body::recompute()
{
    beginIncrementalRecompute();
    for (std::size_t index = 0; index < features_.size(); ++index) {
        if (!recomputeFeature(index)) return finishIncrementalRecompute();
    }
    return finishIncrementalRecompute();
}

void Body::beginIncrementalRecompute()
{
    lastError_.clear();
    resultShape_.Nullify();
    incrementalRecomputeActive_ = true;
}

bool Body::recomputeFeature(const std::size_t index)
{
    if (index >= features_.size()) return true;
    const FeaturePtr& feature = features_[index];

    if (feature->isDirty()) {
        ++geometryRevision_;
        if (!feature->recompute()) {
            resultShape_.Nullify();
            lastError_ =
                "Feature '" + feature->name() + "' failed: " + feature->error();
            return false;
        }
    }

    if (feature->state() == FeatureState::Failed) {
        resultShape_.Nullify();
        lastError_ =
            "Feature '" + feature->name() + "' is in failed state: "
            + feature->error();
        return false;
    }

    if (!feature->shape().IsNull()) {
        resultShape_ = feature->shape();
    }
    return true;
}

bool Body::finishIncrementalRecompute()
{
    incrementalRecomputeActive_ = false;
    if (!resultShape_.IsNull()) refreshComponentGeometry();
    return !resultShape_.IsNull() || features_.empty();
}

void Body::refreshComponentGeometry()
{
    std::unordered_map<std::string, std::vector<ModelComponent*>> grouped;
    for (auto& component : components_)
        grouped[component.sourceFeatureId].push_back(&component);

    for (auto& [sourceFeatureId, components] : grouped) {
        const auto source = findFeature(sourceFeatureId);
        if (!source) continue;
        const auto solids = solidsOf(source->shape());
        // A one-to-one source/result relationship is unambiguous even when
        // the result moved. Keep the persistent component ID and refresh only
        // its geometric signature used by ComponentResolver.
        if (components.size() == 1 && solids.size() == 1) {
            updateComponentGeometry(*components.front(), solids.front());
        }
    }
}

void Body::markDirtyFrom(const std::string& featureId)
{
    if (!findFeature(featureId)) return;
    std::unordered_set<std::string> affected{featureId};
    // Body insertion order is dependency order; unrelated branches stay clean.
    for (const auto& feature : features_) {
        for (const auto& dependency : feature->dependencies()) {
            const auto source = dependency.lock();
            if (source && affected.contains(source->id())) affected.insert(feature->id());
        }
        if (affected.contains(feature->id())) feature->markDirty();
    }
}

const std::string& Body::lastError() const noexcept
{
    return lastError_;
}

std::uint64_t Body::featureGeometryRevision(const std::string& featureId) const noexcept
{
    const auto feature = findFeature(featureId);
    return feature ? feature->geometryRevision() : 0;
}

} // namespace cad::parametric
