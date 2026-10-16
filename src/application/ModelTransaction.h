#pragma once

#include "model/Body.h"

#include <QUndoStack>
#include <QString>

#include <stdexcept>

namespace cad::application {

// A transaction is deliberately independent from QUndoStack macros.  It
// records the stack boundary and explicitly undoes every command added after
// that boundary when validation or recompute fails.
class ModelTransaction final
{
public:
    ModelTransaction(cad::parametric::Body& body, QUndoStack& stack, QString text)
        : body_(body), stack_(stack), stackIndex_(stack.index())
    {
        body_.setRecomputeDeferred(true);
        stack_.beginMacro(std::move(text));
    }

    ModelTransaction(const ModelTransaction&) = delete;
    ModelTransaction& operator=(const ModelTransaction&) = delete;

    ~ModelTransaction()
    {
        if (!finished_) rollback();
    }

    bool commit()
    {
        if (finished_) return false;
        body_.setRecomputeDeferred(false);
        if (!body_.recompute()) {
            error_ = QString::fromStdString(body_.lastError());
            rollback();
            return false;
        }
        stack_.endMacro();
        finished_ = true;
        return true;
    }

    void rollback() noexcept
    {
        if (finished_) return;
        // Close the macro before undoing it.  This ensures rollback is not
        // represented as another user-visible command.
        stack_.endMacro();
        while (stack_.index() > stackIndex_) stack_.undo();
        body_.setRecomputeDeferred(false);
        // Body::recompute() always closes its incremental-recompute scope,
        // including when restoring the pre-transaction state fails.  Do not
        // start a new scope here: that would leave the body internally active
        // after rollback.
        body_.recompute();
        finished_ = true;
    }

    int stackIndex() const noexcept { return stackIndex_; }
    const QString& error() const noexcept { return error_; }

private:
    cad::parametric::Body& body_;
    QUndoStack& stack_;
    int stackIndex_{0};
    bool finished_{false};
    QString error_;
};

} // namespace cad::application
