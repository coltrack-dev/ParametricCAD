#include "application/IfcImporter.h"
#include "application/ModelingController.h"
#include "operations/ImportedFeature.h"

#include <BRepPrimAPI_MakeBox.hxx>

#include <iostream>
#include <stdexcept>

#ifndef PARAMETRIC_CAD_SOURCE_DIR
#define PARAMETRIC_CAD_SOURCE_DIR "."
#endif

namespace {
void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
}

int main()
{
    try {
        cad::application::ModelingController controller;
        const auto native = controller.createBox();
        check(native.success, "native feature setup");
        const auto before = controller.body().features().size();

        std::vector<cad::parametric::ParametricFeature::Ptr> prepared;
        prepared.push_back(std::make_shared<cad::parametric::ImportedFeature>(
            "ifc-workflow-a", "Imported A", BRepPrimAPI_MakeBox(10, 10, 10).Shape()));
        prepared.push_back(std::make_shared<cad::parametric::ImportedFeature>(
            "ifc-workflow-b", "Imported B", BRepPrimAPI_MakeBox(20, 20, 20).Shape()));
        const auto imported = controller.importFeatures(std::move(prepared));
        check(imported.success, "bulk import commit");
        check(controller.body().features().size() == before + 2, "bulk feature count");
        check(controller.undoStack().count() == 2, "bulk import is one undo command");

        controller.undoStack().undo();
        check(controller.body().features().size() == before, "bulk import undo");
        controller.undoStack().redo();
        check(controller.body().features().size() == before + 2, "bulk import redo");
        check(controller.body().findFeature("ifc-workflow-a") != nullptr,
              "redo reuses prepared feature");

#if defined(PARAMETRIC_CAD_HAS_IFCOPENSHELL)
        cad::application::IfcImporter importer;
        cad::parametric::Body untouched;
        const auto source = QStringLiteral(PARAMETRIC_CAD_SOURCE_DIR)
            + "/examples/ifc/Duplex_A_20110505.ifc";
        const auto cancelled = importer.prepare(source, {}, []() { return true; });
        check(cancelled.cancelled, "cooperative cancellation");
        check(untouched.features().empty(), "cancelled import leaves body unchanged");

        const auto fatal = importer.prepare(QStringLiteral("missing-file.ifc"));
        check(fatal.fatal && !fatal.succeeded(), "fatal import result");
        check(untouched.features().empty(), "fatal import leaves body unchanged");
#endif
        std::cout << "IFC workflow tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
