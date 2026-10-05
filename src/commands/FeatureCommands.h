#pragma once

#include "model/Feature.h"
#include "model/Document.h"
#include "model/Body.h"
#include "application/VisibilityManager.h"
#include "operations/ParametricFeatures.h"
#include <QUndoCommand>
#include <QStringList>
#include <functional>
#include <stdexcept>
#include <utility>

namespace cad::commands {

// Document and Body outlive their project's undo stack. Commands retain feature
// ownership, but never own or replace those two stable project containers.
class AddFeatureCommand final : public QUndoCommand
{
public:
    AddFeatureCommand(parametric::Body& body, parametric::ParametricFeature::Ptr feature,
                      const QString& text);
    void undo() override;
    void redo() override;
private:
    parametric::Body& body_;
    parametric::ParametricFeature::Ptr feature_;
    std::size_t position_;
};

class AddSketchEntityCommand final : public QUndoCommand
{
public:
    AddSketchEntityCommand(
        parametric::Body& body,
        std::shared_ptr<parametric::SketchFeature> sketch,
        parametric::SketchEntity entity
    );
    void undo() override;
    void redo() override;

private:
    parametric::Body& body_;
    std::shared_ptr<parametric::SketchFeature> sketch_;
    parametric::SketchEntity entity_;
};

class TrimSketchEntityCommand final : public QUndoCommand
{
public:
    TrimSketchEntityCommand(parametric::Body& body,
                            std::shared_ptr<parametric::SketchFeature> sketch,
                            std::size_t index,
                            parametric::SketchEntity original,
                            std::vector<parametric::SketchEntity> replacements);
    void undo() override;
    void redo() override;

private:
    parametric::Body& body_;
    std::shared_ptr<parametric::SketchFeature> sketch_;
    std::size_t index_;
    parametric::SketchEntity original_;
    std::vector<parametric::SketchEntity> replacements_;
};

class ExtendSketchEntityCommand final : public QUndoCommand
{
public:
    ExtendSketchEntityCommand(parametric::Body& body,
                              std::shared_ptr<parametric::SketchFeature> sketch,
                              std::size_t index,
                              parametric::SketchEntity original,
                              parametric::SketchEntity extended,
                              std::vector<parametric::SketchEntity> beforeEntities = {},
                              std::vector<parametric::SketchEntity> afterEntities = {});
    void undo() override;
    void redo() override;

private:
    parametric::Body& body_;
    std::shared_ptr<parametric::SketchFeature> sketch_;
    std::size_t index_;
    parametric::SketchEntity original_;
    parametric::SketchEntity extended_;
    std::vector<parametric::SketchEntity> beforeEntities_;
    std::vector<parametric::SketchEntity> afterEntities_;
};

class AddSketchConstraintCommand final : public QUndoCommand
{
public:
    AddSketchConstraintCommand(
        parametric::Body& body,
        std::shared_ptr<parametric::SketchFeature> sketch,
        parametric::SketchConstraint constraint,
        std::vector<parametric::SketchEntity> beforeEntities,
        std::vector<parametric::SketchEntity> afterEntities,
        std::vector<parametric::SketchConstraint> beforeConstraints,
        std::vector<parametric::SketchConstraint> afterConstraints);
    void undo() override;
    void redo() override;

private:
    void apply(const std::vector<parametric::SketchEntity>& entities,
               const std::vector<parametric::SketchConstraint>& constraints);
    parametric::Body& body_;
    std::shared_ptr<parametric::SketchFeature> sketch_;
    parametric::SketchConstraint constraint_;
    std::vector<parametric::SketchEntity> beforeEntities_;
    std::vector<parametric::SketchEntity> afterEntities_;
    std::vector<parametric::SketchConstraint> beforeConstraints_;
    std::vector<parametric::SketchConstraint> afterConstraints_;
};

class UpdateSketchConstraintCommand final : public QUndoCommand
{
public:
    UpdateSketchConstraintCommand(
        parametric::Body& body,
        std::shared_ptr<parametric::SketchFeature> sketch,
        std::vector<parametric::SketchEntity> beforeEntities,
        std::vector<parametric::SketchEntity> afterEntities,
        std::vector<parametric::SketchConstraint> beforeConstraints,
        std::vector<parametric::SketchConstraint> afterConstraints);
    void undo() override;
    void redo() override;
private:
    void apply(const std::vector<parametric::SketchEntity>& entities,
               const std::vector<parametric::SketchConstraint>& constraints);
    parametric::Body& body_;
    std::shared_ptr<parametric::SketchFeature> sketch_;
    std::vector<parametric::SketchEntity> beforeEntities_;
    std::vector<parametric::SketchEntity> afterEntities_;
    std::vector<parametric::SketchConstraint> beforeConstraints_;
    std::vector<parametric::SketchConstraint> afterConstraints_;
};

class RemoveSketchConstraintCommand final : public QUndoCommand
{
public:
    RemoveSketchConstraintCommand(
        parametric::Body& body,
        std::shared_ptr<parametric::SketchFeature> sketch,
        std::vector<parametric::SketchEntity> beforeEntities,
        std::vector<parametric::SketchEntity> afterEntities,
        std::vector<parametric::SketchConstraint> beforeConstraints,
        std::vector<parametric::SketchConstraint> afterConstraints);
    void undo() override;
    void redo() override;
private:
    void apply(const std::vector<parametric::SketchEntity>& entities,
               const std::vector<parametric::SketchConstraint>& constraints);
    parametric::Body& body_;
    std::shared_ptr<parametric::SketchFeature> sketch_;
    std::vector<parametric::SketchEntity> beforeEntities_;
    std::vector<parametric::SketchEntity> afterEntities_;
    std::vector<parametric::SketchConstraint> beforeConstraints_;
    std::vector<parametric::SketchConstraint> afterConstraints_;
};

class DuplicateFeatureCommand final : public QUndoCommand
{
public:
    DuplicateFeatureCommand(
        parametric::Body& body,
        parametric::ParametricFeature::Ptr feature,
        const QString& text
    );
    void undo() override;
    void redo() override;

private:
    parametric::Body& body_;
    parametric::ParametricFeature::Ptr feature_;
    std::size_t position_;
};

class ChangeParametricPropertyCommand final : public QUndoCommand
{
public:
    ChangeParametricPropertyCommand(
        parametric::Body& body,
        parametric::ParametricFeature::Ptr feature,
        std::string key,
        parametric::PropertyValue before,
        parametric::PropertyValue after,
        const QString& text
    );
    void undo() override;
    void redo() override;
private:
    void apply(const parametric::PropertyValue& value);
    parametric::Body& body_;
    parametric::ParametricFeature::Ptr feature_;
    std::string key_;
    parametric::PropertyValue before_;
    parametric::PropertyValue after_;
};

class TransformFeatureCommand final : public QUndoCommand
{
public:
    TransformFeatureCommand(
        parametric::Body& body,
        std::string featureId,
        gp_Trsf before,
        gp_Trsf after
    );
    void undo() override;
    void redo() override;

private:
    void apply(const gp_Trsf& placement);

    parametric::Body& body_;
    std::string featureId_;
    gp_Trsf before_;
    gp_Trsf after_;
};

class SetFeatureVisibilityCommand final : public QUndoCommand
{
public:
    SetFeatureVisibilityCommand(
        parametric::Body& body,
        std::vector<std::string> featureIds,
        std::vector<bool> before,
        std::vector<bool> after,
        const QString& text);
    void undo() override;
    void redo() override;

private:
    void apply(const std::vector<bool>& values);

    parametric::Body& body_;
    std::vector<std::string> featureIds_;
    std::vector<bool> before_;
    std::vector<bool> after_;
};

class SetVisibilityGroupsCommand final : public QUndoCommand
{
public:
    SetVisibilityGroupsCommand(
        application::VisibilityManager& visibilityManager,
        std::vector<application::VisibilityGroup> before,
        std::vector<application::VisibilityGroup> after,
        const QString& text);
    void undo() override;
    void redo() override;

private:
    void apply(const std::vector<application::VisibilityGroup>& groups);
    application::VisibilityManager& visibilityManager_;
    std::vector<application::VisibilityGroup> before_;
    std::vector<application::VisibilityGroup> after_;
};

class SetVisibilityFiltersCommand final : public QUndoCommand
{
public:
    SetVisibilityFiltersCommand(
        application::VisibilityManager& visibilityManager,
        application::VisibilityFilterState before,
        application::VisibilityFilterState after,
        const QString& text);
    void undo() override;
    void redo() override;

private:
    void apply(const application::VisibilityFilterState& filters);
    application::VisibilityManager& visibilityManager_;
    application::VisibilityFilterState before_;
    application::VisibilityFilterState after_;
};

class RemoveFeatureCommand final : public QUndoCommand
{
public:
    RemoveFeatureCommand(parametric::Body& body, const std::string& featureId);
    void undo() override;
    void redo() override;
    QStringList dependentNames() const;
private:
    parametric::Body& body_;
    std::vector<std::pair<std::size_t, parametric::ParametricFeature::Ptr>> removed_;
    bool applied_{false};
};

class RemoveDocumentFeatureCommand final : public QUndoCommand
{
public:
    RemoveDocumentFeatureCommand(Document& document, std::size_t position);
    void undo() override;
    void redo() override;
private:
    Document& document_;
    std::size_t position_;
    std::unique_ptr<Feature> feature_;
};

template<class FeatureType, class Value>
class ChangeFeatureParameterCommand final : public QUndoCommand
{
public:
    using Setter = std::function<void(FeatureType&, const Value&)>;
    ChangeFeatureParameterCommand(parametric::Body& body, std::shared_ptr<FeatureType> feature,
                                  Value before, Value after, Setter setter, const QString& text)
        : QUndoCommand(text), body_(body), feature_(std::move(feature)),
          before_(std::move(before)), after_(std::move(after)), setter_(std::move(setter))
    {
        if (!feature_ || body_.findFeature(feature_->id()) != feature_) {
            throw std::invalid_argument("Parameter command requires a feature in the active Body");
        }
    }
    void undo() override { apply(before_); }
    void redo() override { apply(after_); }
private:
    void apply(const Value& value)
    {
        setter_(*feature_, value);
        body_.markDirtyFrom(feature_->id());
        // Invalid edits remain undoable; recompute records their model error state.
        body_.recompute();
    }
    parametric::Body& body_;
    std::shared_ptr<FeatureType> feature_;
    Value before_;
    Value after_;
    Setter setter_;
};

class AddDocumentFeatureCommand final : public QUndoCommand
{
public:
    AddDocumentFeatureCommand(Document& document, std::unique_ptr<Feature> feature, const QString& text);
    void undo() override;
    void redo() override;
private:
    Document& document_;
    std::unique_ptr<Feature> feature_;
    std::size_t position_;
};

class ClearProjectCommand final : public QUndoCommand
{
public:
    ClearProjectCommand(Document& document, parametric::Body& body);
    void undo() override;
    void redo() override;
private:
    void swapContents();
    Document& document_;
    parametric::Body& body_;
    Document savedDocument_;
    parametric::Body savedBody_;
};

} // namespace cad::commands
