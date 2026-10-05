#pragma once
#include <QString>
#include <functional>
#include <cstdint>

struct ProjectLoadMetrics
{
    std::int64_t parseMilliseconds{0};
    std::int64_t deserializeMilliseconds{0};
    std::int64_t recomputeMilliseconds{0};
    std::int64_t cleanupMilliseconds{0};
    std::int64_t presentationMilliseconds{0};
    std::int64_t totalMilliseconds{0};
    int featureCount{0};
};

using ProjectLoadProgress = std::function<void(int loadedFeatures, int totalFeatures)>;

class Document;
namespace cad::parametric { class Body; }
namespace cad::application { class VisibilityManager; }
namespace ProjectFile {
bool save(const QString& path, const Document& document, const cad::parametric::Body& body,
          QString& error, const cad::application::VisibilityManager* visibilityManager = nullptr);
bool load(const QString& path, Document& document, cad::parametric::Body& body, QString& error,
          ProjectLoadProgress progress = {}, ProjectLoadMetrics* metrics = nullptr,
          bool recompute = true,
          cad::application::VisibilityManager* visibilityManager = nullptr);
}
