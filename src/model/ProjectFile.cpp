#include "model/ProjectFile.h"
#include "model/Feature.h"
#include "model/Document.h"
#include "model/Body.h"
#include "operations/BoxFeature.h"
#include "operations/CylinderFeature.h"
#include "operations/ParametricFeatures.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <Standard_Failure.hxx>
#include <cmath>
#include <stdexcept>

namespace {
using namespace cad::parametric;
void require(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}
double number(const QJsonObject& o, const char* key)
{
    const auto v = o.value(QLatin1String(key));
    require(v.isDouble() && std::isfinite(v.toDouble()), "Invalid numeric parameter");
    return v.toDouble();
}
std::string string(const QJsonObject& o, const char* key)
{
    const auto v = o.value(QLatin1String(key));
    require(v.isString(), "Invalid string field");
    return v.toString().toStdString();
}
QJsonObject encode(const ParametricFeature::Ptr& feature, const Body& preceding)
{
    QJsonObject o{{"id", QString::fromStdString(feature->id())},
                  {"name", QString::fromStdString(feature->name())}};
    if (auto f = std::dynamic_pointer_cast<SketchFeature>(feature)) {
        require(std::isfinite(f->width()) && f->width() > 0
                && std::isfinite(f->height()) && f->height() > 0, "Invalid Sketch dimensions");
        o.insert("type", "Sketch"); o.insert("plane", "XY");
        o.insert("width", f->width()); o.insert("height", f->height());
    } else if (auto f = std::dynamic_pointer_cast<FaceFeature>(feature)) {
        if (!f->source() || preceding.findFeature(f->sourceFeatureId()) != f->source()) {
            throw std::runtime_error("Face '" + f->id() + "' references missing or forward Sketch '"
                                     + f->sourceFeatureId() + "'");
        }
        o.insert("type", "Face");
        o.insert("sourceFeatureId", QString::fromStdString(f->sourceFeatureId()));
    } else if (auto f = std::dynamic_pointer_cast<BoxParametricFeature>(feature)) {
        o.insert("type", "Box"); o.insert("width", f->width());
        o.insert("depth", f->depth()); o.insert("height", f->height());
    } else if (auto f = std::dynamic_pointer_cast<CylinderParametricFeature>(feature)) {
        o.insert("type", "Cylinder"); o.insert("radius", f->radius()); o.insert("height", f->height());
    } else if (auto f = std::dynamic_pointer_cast<ConeFeature>(feature)) {
        o.insert("type", "Cone"); o.insert("bottomRadius", f->bottomRadius());
        o.insert("topRadius", f->topRadius()); o.insert("height", f->height());
    } else if (auto f = std::dynamic_pointer_cast<SphereFeature>(feature)) {
        o.insert("type", "Sphere"); o.insert("radius", f->radius());
    } else if (auto f = std::dynamic_pointer_cast<TorusFeature>(feature)) {
        o.insert("type", "Torus"); o.insert("majorRadius", f->majorRadius()); o.insert("minorRadius", f->minorRadius());
    } else if (auto f = std::dynamic_pointer_cast<HexagonFeature>(feature)) {
        o.insert("type", "Hexagon"); o.insert("acrossFlats", f->acrossFlats()); o.insert("height", f->height());
    } else if (auto f = std::dynamic_pointer_cast<BooleanFeature>(feature)) {
        require(f->left() && f->right() && preceding.findFeature(f->left()->id()) == f->left()
                && preceding.findFeature(f->right()->id()) == f->right(), "Invalid boolean references");
        o.insert("type", "Boolean");
        o.insert("left", QString::fromStdString(f->left()->id()));
        o.insert("right", QString::fromStdString(f->right()->id()));
        switch (f->operation()) {
        case BooleanOperation::Fuse: o.insert("operation", "Fuse"); break;
        case BooleanOperation::Cut: o.insert("operation", "Cut"); break;
        case BooleanOperation::Common: o.insert("operation", "Common"); break;
        }
    } else throw std::runtime_error("Unsupported parametric feature type");
    return o;
}
ParametricFeature::Ptr decode(const QJsonObject& o, const Body& body)
{
    const auto id = string(o, "id");
    require(!id.empty(), "Empty feature id");
    const auto type = string(o, "type");
    ParametricFeature::Ptr f;
    if (type == "Sketch") {
        require(string(o, "plane") == "XY", "Unsupported Sketch plane (expected XY)");
        f = std::make_shared<SketchFeature>(id, number(o, "width"), number(o, "height"));
    } else if (type == "Face") {
        const auto sourceId = string(o, "sourceFeatureId");
        const auto source = body.findFeature(sourceId);
        if (!std::dynamic_pointer_cast<SketchFeature>(source)) {
            throw std::runtime_error("Face '" + id + "' references missing, forward or non-Sketch source '"
                                     + sourceId + "'");
        }
        f = std::make_shared<FaceFeature>(id, source);
    } else if (type == "Box") f = std::make_shared<BoxParametricFeature>(id, number(o,"width"), number(o,"depth"), number(o,"height"));
    else if (type == "Cylinder") f = std::make_shared<CylinderParametricFeature>(id, number(o,"radius"), number(o,"height"));
    else if (type == "Cone") f = std::make_shared<ConeFeature>(id, number(o,"bottomRadius"), number(o,"topRadius"), number(o,"height"));
    else if (type == "Sphere") f = std::make_shared<SphereFeature>(id, number(o,"radius"));
    else if (type == "Torus") f = std::make_shared<TorusFeature>(id, number(o,"majorRadius"), number(o,"minorRadius"));
    else if (type == "Hexagon") f = std::make_shared<HexagonFeature>(id, number(o,"acrossFlats"), number(o,"height"));
    else if (type == "Boolean") {
        const auto left = body.findFeature(string(o,"left"));
        const auto right = body.findFeature(string(o,"right"));
        require(left && right, "Missing or forward boolean reference");
        const auto op = string(o,"operation");
        require(op == "Fuse" || op == "Cut" || op == "Common", "Unknown boolean operation");
        f = std::make_shared<BooleanFeature>(id, left, right, op == "Fuse" ? BooleanOperation::Fuse :
            op == "Cut" ? BooleanOperation::Cut : BooleanOperation::Common);
    } else throw std::runtime_error("Unsupported parametric feature type");
    f->setName(string(o,"name"));
    return f;
}
}

bool ProjectFile::save(const QString& path, const Document& document,
                       const cad::parametric::Body& body, QString& error)
{
    error.clear();
    try {
        QJsonArray features;
        for (const auto& feature : document.features()) {
            if (auto f = dynamic_cast<const BoxFeature*>(feature.get())) {
                features.append(QJsonObject{{"type", "Box"}, {"width", f->width()}, {"depth", f->depth()}, {"height", f->height()}});
            } else if (auto f = dynamic_cast<const CylinderFeature*>(feature.get())) {
                features.append(QJsonObject{{"type", "Cylinder"}, {"radius", f->radius()}, {"height", f->height()}});
            } else throw std::runtime_error("Unsupported document feature type");
        }
        QJsonArray history;
        Body preceding;
        for (const auto& feature : body.features()) {
            history.append(encode(feature, preceding));
            preceding.addFeature(feature);
        }
        const QByteArray data = QJsonDocument(QJsonObject{{"format", "ParametricCAD"}, {"version", 1},
            {"features", features}, {"body", history}}).toJson();
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
            error = file.errorString(); return false;
        }
        return true;
    } catch (const Standard_Failure& e) { error = QString::fromUtf8(e.GetMessageString()); }
      catch (const std::exception& e) { error = QString::fromUtf8(e.what()); }
    return false;
}

bool ProjectFile::load(const QString& path, Document& document,
                       cad::parametric::Body& body, QString& error)
{
    error.clear();
    try {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
        const auto data = file.readAll();
        if (file.error() != QFileDevice::NoError) { error = file.errorString(); return false; }
        QJsonParseError parseError;
        const auto json = QJsonDocument::fromJson(data, &parseError);
        require(parseError.error == QJsonParseError::NoError && json.isObject(), "Invalid project JSON");
        const auto root = json.object();
        require(string(root,"format") == "ParametricCAD" && number(root,"version") == 1, "Unsupported project format or version");
        require(root.value("features").isArray() && root.value("body").isArray(), "Missing feature arrays");
        Document loaded;
        Body loadedBody;
        for (const auto& value : root.value("features").toArray()) {
            require(value.isObject(), "Invalid feature entry");
            const auto o = value.toObject();
            const auto type = string(o,"type");
            if (type == "Box") loaded.addFeature(std::make_unique<BoxFeature>(number(o,"width"), number(o,"depth"), number(o,"height")));
            else if (type == "Cylinder") loaded.addFeature(std::make_unique<CylinderFeature>(number(o,"radius"), number(o,"height")));
            else throw std::runtime_error("Unsupported document feature type");
        }
        for (const auto& value : root.value("body").toArray()) {
            require(value.isObject(), "Invalid body feature entry");
            loadedBody.addFeature(decode(value.toObject(), loadedBody));
        }
        if (!loadedBody.recompute()) throw std::runtime_error(loadedBody.lastError());
        document = std::move(loaded);
        body = std::move(loadedBody);
        return true;
    } catch (const Standard_Failure& e) { error = QString::fromUtf8(e.GetMessageString()); }
      catch (const std::exception& e) { error = QString::fromUtf8(e.what()); }
    return false;
}
