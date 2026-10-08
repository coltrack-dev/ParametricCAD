#pragma once

#include <QPointF>

#include "operations/ParametricFeatures.h"

#include <optional>
#include <vector>

namespace cad::viewer {

struct SketchLineScreenCandidate
{
    cad::parametric::SketchEntityId id;
    QPointF start;
    QPointF end;
    bool construction{false};
};

std::optional<cad::parametric::SketchEntityId> pickSketchLine(
    const std::vector<SketchLineScreenCandidate>& candidates,
    const QPointF& position,
    double tolerancePixels);

} // namespace cad::viewer
