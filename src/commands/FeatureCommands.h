#pragma once

#include "model/Feature.h"
#include "model/Document.h"
#include "model/Body.h"
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
