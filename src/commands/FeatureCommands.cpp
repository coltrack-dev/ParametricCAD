#include "commands/FeatureCommands.h"
#include <QLoggingCategory>
#include <algorithm>
#include <unordered_set>

namespace cad::commands {

Q_LOGGING_CATEGORY(pcadCommandLog, "parametric.command")

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

AddSketchEntityCommand::AddSketchEntityCommand(
    parametric::Body& body,
    std::shared_ptr<parametric::SketchFeature> sketch,
    parametric::SketchEntity entity
)
    : QUndoCommand("Add Sketch Entity"), body_(body),
      sketch_(std::move(sketch)), entity_(std::move(entity))
{
    if (!sketch_ || body_.findFeature(sketch_->id()) != sketch_)
        throw std::invalid_argument("Sketch entity command requires an active sketch");
}

TrimSketchEntityCommand::TrimSketchEntityCommand(
    parametric::Body& body, std::shared_ptr<parametric::SketchFeature> sketch,
    const std::size_t index, parametric::SketchEntity original,
    std::vector<parametric::SketchEntity> replacements)
    : QUndoCommand("Trim Sketch Entity"), body_(body), sketch_(std::move(sketch)),
      index_(index), original_(std::move(original)), replacements_(std::move(replacements))
{
    if (!sketch_ || body_.findFeature(sketch_->id()) != sketch_ || index_ >= sketch_->entityCount())
        throw std::invalid_argument("Trim command requires an active sketch entity");
}

void TrimSketchEntityCommand::redo()
{
    sketch_->replaceEntities(index_, 1, replacements_);
    body_.markDirtyFrom(sketch_->id());
}

void TrimSketchEntityCommand::undo()
{
    sketch_->replaceEntities(index_, replacements_.size(), {original_});
    body_.markDirtyFrom(sketch_->id());
}

void AddSketchEntityCommand::redo()
{
    sketch_->addEntity(entity_);
    body_.markDirtyFrom(sketch_->id());
    if (!body_.recompute()) throw std::runtime_error(body_.lastError());
}

void AddSketchEntityCommand::undo()
{
    sketch_->removeLastEntity();
    body_.markDirtyFrom(sketch_->id());
    body_.recompute();
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
    qCDebug(pcadCommandLog) << "property apply"
                            << QString::fromStdString(feature_->id())
                            << QString::fromStdString(key_)
                            << "ptr" << static_cast<const void*>(feature_.get())
                            << "valueIndex" << static_cast<int>(value.index());
    if (!feature_->setProperty(key_, value)) {
        throw std::invalid_argument("Feature does not expose property '" + key_ + "'");
    }
    body_.markDirtyFrom(feature_->id());
    const bool recomputed = body_.recompute();
    qCDebug(pcadCommandLog) << "property recompute complete"
                            << QString::fromStdString(feature_->id())
                            << "ok" << recomputed
                            << "state" << static_cast<int>(feature_->state())
                            << "error" << QString::fromStdString(feature_->error());
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
