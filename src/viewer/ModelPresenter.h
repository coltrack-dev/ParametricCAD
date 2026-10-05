#pragma once

#include "model/Body.h"
#include "viewer/VisibilityMode.h"

#include <string>
#include <set>
#include <vector>

class CadViewer;

namespace cad::viewer {

struct PresentationResult
{
    bool rebuilt{false};
    std::vector<std::string> presentedIds;
    std::string error;
};

class ModelPresenter final
{
public:
    ModelPresenter(cad::parametric::Body& body, CadViewer& viewer);

    PresentationResult refreshModel();
    void refreshVisibility();
    void clear();
    void setIsolatedFeatures(const std::vector<std::string>& featureIds);
    void clearIsolation();
    bool isolationActive() const noexcept;
    void ghostOthers(const std::vector<std::string>& selectedIds);
    void clearGhosting();

private:
    void applyVisibility();

    cad::parametric::Body& body_;
    CadViewer& viewer_;
    std::set<std::string> isolatedFeatureIds_;
    std::set<std::string> ghostedSelectionIds_;
};

} // namespace cad::viewer
