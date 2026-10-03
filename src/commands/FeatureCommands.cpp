#include "commands/FeatureCommands.h"
#include <algorithm>
#include <unordered_set>

namespace cad::commands {

AddFeatureCommand::AddFeatureCommand(parametric::Body& body,
    parametric::ParametricFeature::Ptr feature, const QString& text)
    : QUndoCommand(text), body_(body), feature_(std::move(feature)), position_(body.features().size())
{
    if (!feature_ || body_.findFeature(feature_->id())) {
        throw std::invalid_argument("New feature must have a unique ID");
    }
    for (const auto& weak : feature_->dependencies()) {
        const auto source = weak.lock();
        if (!source || body_.findFeature(source->id()) != source) {
            throw std::invalid_argument("New feature references a missing source");
        }
    }
    if (!feature_->recompute()) throw std::invalid_argument(feature_->error());
}

DuplicateFeatureCommand::DuplicateFeatureCommand(
    parametric::Body& body,
    parametric::ParametricFeature::Ptr feature,
    const QString& text
)
    : QUndoCommand(text),
      body_(body),
      feature_(std::move(feature)),
      position_(body.features().size())
{
    if (!feature_ || body_.findFeature(feature_->id())) {
        throw std::invalid_argument("Duplicate must have a unique ID");
    }
    for (const auto& weak : feature_->dependencies()) {
        const auto dependency = weak.lock();
        if (!dependency || body_.findFeature(dependency->id()) != dependency) {
            throw std::invalid_argument("Duplicate references a missing source");
        }
    }
    if (!feature_->recompute()) throw std::invalid_argument(feature_->error());
}

ChangeParametricPropertyCommand::ChangeParametricPropertyCommand(
    parametric::Body& body,
    parametric::ParametricFeature::Ptr feature,
    std::string key,
    parametric::PropertyValue before,
    parametric::PropertyValue after,
    const QString& text
)
    : QUndoCommand(text), body_(body), feature_(std::move(feature)),
      key_(std::move(key)), before_(before), after_(after)
{
    if (!feature_ || body_.findFeature(feature_->id()) != feature_) {
        throw std::invalid_argument("Property command requires a feature in the active Body");
    }
}

void ChangeParametricPropertyCommand::apply(const parametric::PropertyValue& value)
{
    if (!feature_->setProperty(key_, value)) {
        throw std::invalid_argument("Feature does not expose property '" + key_ + "'");
    }
    body_.markDirtyFrom(feature_->id());
    body_.recompute();
}

void ChangeParametricPropertyCommand::undo() { apply(before_); }
void ChangeParametricPropertyCommand::redo() { apply(after_); }

TransformFeatureCommand::TransformFeatureCommand(
    parametric::Body& body,
    std::string featureId,
    gp_Trsf before,
    gp_Trsf after
)
    : QUndoCommand("Transform Feature"),
      body_(body),
      featureId_(std::move(featureId)),
      before_(std::move(before)),
      after_(std::move(after))
{
    if (!body_.findFeature(featureId_)) {
        throw std::invalid_argument("Transform target does not exist");
    }
}

void TransformFeatureCommand::apply(const gp_Trsf& placement)
{
    const auto feature = body_.findFeature(featureId_);
    if (!feature) {
        throw std::invalid_argument("Transform target no longer exists");
    }
    feature->setPlacement(placement);
    body_.markDirtyFrom(featureId_);
    body_.recompute();
}

void TransformFeatureCommand::undo()
{
    apply(before_);
}

void TransformFeatureCommand::redo()
{
    apply(after_);
}

void AddFeatureCommand::undo()
{
    body_.removeFeature(feature_->id());
    body_.recompute();
}

void AddFeatureCommand::redo()
{
    body_.insertFeature(position_, feature_);
    body_.recompute();
}

void DuplicateFeatureCommand::undo()
{
    body_.removeFeature(feature_->id());
    body_.recompute();
}

void DuplicateFeatureCommand::redo()
{
    body_.insertFeature(position_, feature_);
    body_.recompute();
}

RemoveFeatureCommand::RemoveFeatureCommand(parametric::Body& body, const std::string& featureId)
    : QUndoCommand("Delete Feature"), body_(body)
{
    const auto root = body.findFeature(featureId);
    if (!root) throw std::invalid_argument("Cannot delete unknown feature: " + featureId);
    setText("Delete " + QString::fromStdString(root->name()));
    std::unordered_set<std::string> affected{featureId};
    // Body guarantees dependency order. Keep original positions for restoration.
    for (std::size_t i = 0; i < body.features().size(); ++i) {
        const auto& feature = body.features()[i];
        for (const auto& weak : feature->dependencies()) {
            const auto source = weak.lock();
            if (source && affected.contains(source->id())) affected.insert(feature->id());
        }
        if (affected.contains(feature->id())) removed_.emplace_back(i, feature);
    }
}

QStringList RemoveFeatureCommand::dependentNames() const
{
    QStringList names;
    for (std::size_t i = 1; i < removed_.size(); ++i) {
        const auto& feature = removed_[i].second;
        names.append(QString::fromStdString(feature->name()) + " (" + QString::fromStdString(feature->id()) + ")");
    }
    return names;
}

void RemoveFeatureCommand::undo()
{
    if (!applied_) return;
    for (const auto& [position, feature] : removed_) body_.insertFeature(position, feature);
    body_.recompute();
    applied_ = false;
}

void RemoveFeatureCommand::redo()
{
    if (applied_) return;
    for (auto it = removed_.rbegin(); it != removed_.rend(); ++it) body_.removeFeature(it->second->id());
    body_.recompute();
    applied_ = true;
}

RemoveDocumentFeatureCommand::RemoveDocumentFeatureCommand(Document& document, std::size_t position)
    : QUndoCommand("Delete Object"), document_(document), position_(position)
{
    if (position >= document.features().size()) throw std::invalid_argument("Cannot delete unknown object");
}

void RemoveDocumentFeatureCommand::undo()
{
    if (feature_) document_.insertFeature(position_, std::move(feature_));
}

void RemoveDocumentFeatureCommand::redo()
{
    if (!feature_) feature_ = document_.takeFeature(position_);
}

AddDocumentFeatureCommand::AddDocumentFeatureCommand(Document& document,
    std::unique_ptr<Feature> feature, const QString& text)
    : QUndoCommand(text), document_(document), feature_(std::move(feature)), position_(document.features().size())
{
    if (!feature_) throw std::invalid_argument("Feature must not be null");
    feature_->recompute();
}

void AddDocumentFeatureCommand::undo() { feature_ = document_.takeFeature(position_); }
void AddDocumentFeatureCommand::redo() { document_.insertFeature(position_, std::move(feature_)); }

ClearProjectCommand::ClearProjectCommand(Document& document, parametric::Body& body)
    : QUndoCommand("Clear Project"), document_(document), body_(body)
{
}

void ClearProjectCommand::swapContents()
{
    std::swap(document_, savedDocument_);
    std::swap(body_, savedBody_);
}

void ClearProjectCommand::undo() { swapContents(); }
void ClearProjectCommand::redo() { swapContents(); }

} // namespace cad::commands
