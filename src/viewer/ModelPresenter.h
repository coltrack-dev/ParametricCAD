#pragma once

#include "model/Body.h"
#include "application/VisibilityManager.h"

#include <string>
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
    ModelPresenter(cad::parametric::Body& body,
                   cad::application::VisibilityManager& visibilityManager,
                   CadViewer& viewer);

    PresentationResult refreshModel();
    void refreshVisibility();
    void clear();

private:
    void applyVisibility();

    cad::parametric::Body& body_;
    CadViewer& viewer_;
    cad::application::VisibilityManager& visibilityManager_;
};

} // namespace cad::viewer
