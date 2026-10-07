#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace cad::application {

enum class InteractiveOperationKind
{
    Extrude,
    Pocket,
    PushPull,
    Fillet,
    Chamfer,
    Shell
};

struct InteractiveOperationContext
{
    InteractiveOperationKind kind;
    std::vector<std::string> sourceFeatureIds;
    std::string editingFeatureId;
    std::map<std::string, double> parameters;

    bool editingExisting() const noexcept { return !editingFeatureId.empty(); }
};

// Owns the lifecycle state shared by transient operation tools. It deliberately
// contains no model or AIS objects: preview state is discarded on cancel and
// only the commit boundary is allowed to call the persistent command layer.
class InteractiveOperationSession final
{
public:
    bool begin(InteractiveOperationContext context)
    {
        if (active_) return false;
        context_ = std::move(context);
        active_ = true;
        previewing_ = true;
        return true;
    }

    bool updatePreview(const std::string& key, double value)
    {
        if (!active_) return false;
        context_.parameters[key] = value;
        previewing_ = true;
        return true;
    }

    std::optional<InteractiveOperationContext> commit()
    {
        if (!active_) return std::nullopt;
        auto committed = std::move(context_);
        reset();
        return committed;
    }

    void cancel() noexcept { reset(); }

    bool active() const noexcept { return active_; }
    bool previewing() const noexcept { return active_ && previewing_; }
    const InteractiveOperationContext& context() const noexcept { return context_; }

private:
    void reset() noexcept
    {
        context_ = {};
        active_ = false;
        previewing_ = false;
    }

    InteractiveOperationContext context_{};
    bool active_{false};
    bool previewing_{false};
};

} // namespace cad::application
