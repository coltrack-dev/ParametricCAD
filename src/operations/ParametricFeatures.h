#pragma once

#include "model/ParametricFeature.h"

#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>

#include <gp_Ax1.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <utility>
#include <vector>

namespace cad::parametric {

class BoxParametricFeature final : public ParametricFeature
{
public:
    BoxParametricFeature(
        std::string id,
        double width,
        double depth,
        double height
    );

    void setSize(double width, double depth, double height);

protected:
    TopoDS_Shape build() const override;

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

    void setRadius(double radius);
    void setHeight(double height);

protected:
    TopoDS_Shape build() const override;

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

protected:
    TopoDS_Shape build() const override;

private:
    double bottomRadius_;
    double topRadius_;
    double height_;
};

class SphereFeature final : public ParametricFeature
{
public:
    SphereFeature(std::string id, double radius);

protected:
    TopoDS_Shape build() const override;

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

protected:
    TopoDS_Shape build() const override;

private:
    double majorRadius_;
    double minorRadius_;
};

class ExtrudeFeature final : public ParametricFeature
{
public:
    ExtrudeFeature(
        std::string id,
        const Ptr& profile,
        gp_Vec vector
    );

    void setVector(gp_Vec vector);

protected:
    TopoDS_Shape build() const override;

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

protected:
    TopoDS_Shape build() const override;

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
        std::vector<std::pair<TopoDS_Edge, TopoDS_Face>> edges,
        double distance
    );

protected:
    TopoDS_Shape build() const override;

private:
    Ptr base_;
    std::vector<TopoDS_Edge> edges;
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

protected:
    TopoDS_Shape build() const override;

private:
    TopoDS_Wire path_;
    Ptr profile_;
};

} // namespace cad::parametric
