#pragma once

#include "model/ParametricFeature.h"

#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>

#include <gp_Ax1.hxx>
#include <gp_Vec.hxx>

#include <string>
#include <vector>

namespace cad::parametric {

class SketchFeature final : public ParametricFeature
{
public:
    SketchFeature(std::string id, double width, double height);
    const char* typeId() const noexcept override { return "Sketch"; }
    FeatureRole role() const noexcept override { return FeatureRole::Sketch; }
    std::vector<FeatureProperty> properties() const override;
    bool setNumericProperty(const std::string& key, double value) override;
    void setSize(double width, double height);
    double width() const noexcept;
    double height() const noexcept;

protected:
    TopoDS_Shape build() const override;
    void writeParameters(QJsonObject& object) const override;

private:
    double width_;
    double height_;
};

class FaceFeature final : public ParametricFeature
{
public:
    FaceFeature(std::string id, const Ptr& source);
    const char* typeId() const noexcept override { return "Face"; }
    FeatureRole role() const noexcept override { return FeatureRole::Face; }
    std::vector<FeatureProperty> properties() const override;
    const std::string& sourceFeatureId() const noexcept;
    Ptr source() const;

protected:
    TopoDS_Shape build() const override;
    void writeParameters(QJsonObject& object) const override;

private:
    std::string sourceFeatureId_;
    std::weak_ptr<ParametricFeature> source_;
};

class BoxParametricFeature final : public ParametricFeature
{
public:
    BoxParametricFeature(
        std::string id,
        double width,
        double depth,
        double height
    );
    const char* typeId() const noexcept override { return "Box"; }
    std::vector<FeatureProperty> properties() const override;
    bool setNumericProperty(const std::string& key, double value) override;

    void setSize(double width, double depth, double height);

    double width() const noexcept;
    double depth() const noexcept;
    double height() const noexcept;

protected:
    TopoDS_Shape build() const override;
    void writeParameters(QJsonObject& object) const override;

private:
    double width_;
    double depth_;
    double height_;
};

class CylinderParametricFeature final : public ParametricFeature
{
public:
    CylinderParametricFeature(
        std::string id,
        double radius,
        double height
    );
    const char* typeId() const noexcept override { return "Cylinder"; }
    std::vector<FeatureProperty> properties() const override;
    bool setNumericProperty(const std::string& key, double value) override;

    void setRadius(double radius);
    void setHeight(double height);

    double radius() const noexcept;
    double height() const noexcept;

protected:
    TopoDS_Shape build() const override;
    void writeParameters(QJsonObject& object) const override;

private:
    double radius_;
    double height_;
};

class ConeFeature final : public ParametricFeature
{
public:
    ConeFeature(
        std::string id,
        double bottomRadius,
        double topRadius,
        double height
    );
    const char* typeId() const noexcept override { return "Cone"; }
    std::vector<FeatureProperty> properties() const override;
    bool setNumericProperty(const std::string& key, double value) override;

    void setBottomRadius(double radius);
    void setTopRadius(double radius);
    void setHeight(double height);

    double bottomRadius() const noexcept;
    double topRadius() const noexcept;
    double height() const noexcept;

protected:
    TopoDS_Shape build() const override;
    void writeParameters(QJsonObject& object) const override;

private:
    double bottomRadius_;
    double topRadius_;
    double height_;
};

class SphereFeature final : public ParametricFeature
{
public:
    SphereFeature(std::string id, double radius);
    const char* typeId() const noexcept override { return "Sphere"; }
    std::vector<FeatureProperty> properties() const override;
    bool setNumericProperty(const std::string& key, double value) override;

    void setRadius(double radius);
    double radius() const noexcept;

protected:
    TopoDS_Shape build() const override;
    void writeParameters(QJsonObject& object) const override;

private:
    double radius_;
};

class TorusFeature final : public ParametricFeature
{
public:
    TorusFeature(
        std::string id,
        double majorRadius,
        double minorRadius
    );
    const char* typeId() const noexcept override { return "Torus"; }
    std::vector<FeatureProperty> properties() const override;
    bool setNumericProperty(const std::string& key, double value) override;

    void setMajorRadius(double radius);
    void setMinorRadius(double radius);

    double majorRadius() const noexcept;
    double minorRadius() const noexcept;

protected:
    TopoDS_Shape build() const override;
    void writeParameters(QJsonObject& object) const override;

private:
    double majorRadius_;
    double minorRadius_;
};


class HexagonFeature final : public ParametricFeature
{
public:
    HexagonFeature(
        std::string id,
        double acrossFlats,
        double height
    );
    const char* typeId() const noexcept override { return "Hexagon"; }
    std::vector<FeatureProperty> properties() const override;
    bool setNumericProperty(const std::string& key, double value) override;

    void setAcrossFlats(double acrossFlats);
    void setHeight(double height);

    double acrossFlats() const noexcept;
    double height() const noexcept;

protected:
    TopoDS_Shape build() const override;
    void writeParameters(QJsonObject& object) const override;

private:
    double acrossFlats_;
    double height_;
};

class ExtrudeFeature final : public ParametricFeature
{
public:
    ExtrudeFeature(
        std::string id,
        const Ptr& profile,
        gp_Vec vector
    );
    const char* typeId() const noexcept override { return "Extrude"; }
    std::vector<FeatureProperty> properties() const override;
    bool setNumericProperty(const std::string& key, double value) override;

    void setVector(gp_Vec vector);

    const Ptr& profile() const noexcept;
    const gp_Vec& vector() const noexcept;

protected:
    TopoDS_Shape build() const override;
    void writeParameters(QJsonObject& object) const override;

private:
    Ptr profile_;
    gp_Vec vector_;
};

class RevolveFeature final : public ParametricFeature
{
public:
    RevolveFeature(
        std::string id,
        const Ptr& profile,
        gp_Ax1 axis,
        double angleRadians
    );
    const char* typeId() const noexcept override { return "Revolve"; }

    void setAxis(gp_Ax1 axis);
    void setAngleRadians(double angleRadians);

    const Ptr& profile() const noexcept;
    const gp_Ax1& axis() const noexcept;
    double angleRadians() const noexcept;

protected:
    TopoDS_Shape build() const override;

private:
    Ptr profile_;
    gp_Ax1 axis_;
    double angleRadians_;
};

enum class BooleanOperation
{
    Fuse,
    Cut,
    Common
};

class BooleanFeature final : public ParametricFeature
{
public:
    BooleanFeature(
        std::string id,
        const Ptr& left,
        const Ptr& right,
        BooleanOperation operation
    );
    const char* typeId() const noexcept override { return "Boolean"; }
    std::vector<FeatureProperty> properties() const override;
    bool setNumericProperty(const std::string&, double) override { return false; }

    void setOperation(BooleanOperation operation);

    const Ptr& left() const noexcept;
    const Ptr& right() const noexcept;
    BooleanOperation operation() const noexcept;
    std::string creationLabel() const override;
    std::vector<std::string> hiddenDependencyIds() const override;

protected:
    TopoDS_Shape build() const override;
    void writeParameters(QJsonObject& object) const override;

private:
    Ptr left_;
    Ptr right_;
    BooleanOperation operation_;
};

class FilletFeature final : public ParametricFeature
{
public:
    FilletFeature(
        std::string id,
        const Ptr& base,
        std::vector<TopoDS_Edge> edges,
        double radius
    );
    const char* typeId() const noexcept override { return "Fillet"; }

    void setEdges(std::vector<TopoDS_Edge> edges);
    void setRadius(double radius);

    const Ptr& base() const noexcept;
    const std::vector<TopoDS_Edge>& edges() const noexcept;
    double radius() const noexcept;

protected:
    TopoDS_Shape build() const override;

private:
    Ptr base_;
    std::vector<TopoDS_Edge> edges_;
    double radius_;
};

class ChamferFeature final : public ParametricFeature
{
public:
    ChamferFeature(
        std::string id,
        const Ptr& base,
        std::vector<TopoDS_Edge> edges,
        double distance
    );
    const char* typeId() const noexcept override { return "Chamfer"; }

    void setEdges(std::vector<TopoDS_Edge> edges);
    void setDistance(double distance);

    const Ptr& base() const noexcept;
    const std::vector<TopoDS_Edge>& edges() const noexcept;
    double distance() const noexcept;

protected:
    TopoDS_Shape build() const override;

private:
    Ptr base_;
    std::vector<TopoDS_Edge> edges_;
    double distance_;
};

class ShellFeature final : public ParametricFeature
{
public:
    ShellFeature(
        std::string id,
        const Ptr& base,
        std::vector<TopoDS_Face> facesToRemove,
        double thickness
    );
    const char* typeId() const noexcept override { return "Shell"; }

    void setFacesToRemove(std::vector<TopoDS_Face> faces);
    void setThickness(double thickness);

    const Ptr& base() const noexcept;
    const std::vector<TopoDS_Face>& facesToRemove() const noexcept;
    double thickness() const noexcept;

protected:
    TopoDS_Shape build() const override;

private:
    Ptr base_;
    std::vector<TopoDS_Face> facesToRemove_;
    double thickness_;
};

class OffsetFeature final : public ParametricFeature
{
public:
    OffsetFeature(
        std::string id,
        const Ptr& base,
        double distance
    );
    const char* typeId() const noexcept override { return "Offset"; }

    void setDistance(double distance);

    const Ptr& base() const noexcept;
    double distance() const noexcept;

protected:
    TopoDS_Shape build() const override;

private:
    Ptr base_;
    double distance_;
};

class LoftFeature final : public ParametricFeature
{
public:
    LoftFeature(
        std::string id,
        std::vector<TopoDS_Wire> sections,
        bool makeSolid = true,
        bool ruled = false
    );
    const char* typeId() const noexcept override { return "Loft"; }

    void setSections(std::vector<TopoDS_Wire> sections);
    void setMakeSolid(bool makeSolid);
    void setRuled(bool ruled);

    const std::vector<TopoDS_Wire>& sections() const noexcept;
    bool makeSolid() const noexcept;
    bool ruled() const noexcept;

protected:
    TopoDS_Shape build() const override;

private:
    std::vector<TopoDS_Wire> sections_;
    bool makeSolid_;
    bool ruled_;
};

class SweepFeature final : public ParametricFeature
{
public:
    SweepFeature(
        std::string id,
        TopoDS_Wire path,
        const Ptr& profile
    );
    const char* typeId() const noexcept override { return "Sweep"; }

    void setPath(TopoDS_Wire path);

    const TopoDS_Wire& path() const noexcept;
    const Ptr& profile() const noexcept;

protected:
    TopoDS_Shape build() const override;

private:
    TopoDS_Wire path_;
    Ptr profile_;
};

} // namespace cad::parametric
