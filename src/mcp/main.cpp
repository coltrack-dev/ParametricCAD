#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QElapsedTimer>
#include <QTextStream>
#include <QTimer>

namespace {
constexpr int DefaultTimeoutMs = 5000;
constexpr int ProjectOperationTimeoutMs = 120000;

bool traceMcp()
{
    return qEnvironmentVariableIsSet("PARAMETRICCAD_TRACE_MCP");
}

void traceMcpMessage(const QString& message)
{
    if (!traceMcp()) return;
    QTextStream(stderr) << "[MCP] " << message << '\n';
}

QJsonArray toolDefinitions()
{
    const QJsonObject empty{{"type", "object"}, {"additionalProperties", false}};
    const QJsonObject feature{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{{"featureId", QJsonObject{{"type", "string"}}}}},
        {"required", QJsonArray{"featureId"}}};
    const QJsonObject units{{"type", "string"}, {"enum", QJsonArray{"mm"}},
        {"description", "Model length unit; ParametricCAD uses millimeters"}};
    const QJsonObject newProject{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{{"units", units}}}};
    const QJsonObject project{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{{"path", QJsonObject{{"type", "string"}}}}},
        {"required", QJsonArray{"path"}}};
    const QJsonObject importIfc{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{
            {"path", QJsonObject{{"type", "string"}}},
            {"replaceCurrent", QJsonObject{{"type", "boolean"}}}}},
        {"required", QJsonArray{"path"}}};
    const QJsonObject operationStatus{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{{"operationId", QJsonObject{{"type", "string"}}}}}};
    const QJsonObject sketch{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{
            {"support", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"XY", "XZ", "YZ"}}}},
            {"faceReference", QJsonObject{{"type", "object"}, {"description", "Serialized TopologicalReference for a planar face"}}},
            {"units", units}}}};
    const QJsonObject rectangle{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{
            {"sketchId", QJsonObject{{"type", "string"}}}, {"x", QJsonObject{{"type", "number"}}},
            {"y", QJsonObject{{"type", "number"}}}, {"width", QJsonObject{{"type", "number"}}},
            {"height", QJsonObject{{"type", "number"}}}, {"units", units}}},
        {"required", QJsonArray{"sketchId", "x", "y", "width", "height", "units"}}};
    const QJsonObject circle{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{
            {"sketchId", QJsonObject{{"type", "string"}}}, {"x", QJsonObject{{"type", "number"}}},
            {"y", QJsonObject{{"type", "number"}}}, {"radius", QJsonObject{{"type", "number"}}},
            {"units", units}}},
        {"required", QJsonArray{"sketchId", "x", "y", "radius", "units"}}};
    const QJsonObject extrude{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{{"sketchId", QJsonObject{{"type", "string"}}},
            {"distance", QJsonObject{{"type", "number"}}}, {"reversed", QJsonObject{{"type", "boolean"}}}, {"units", units}}},
        {"required", QJsonArray{"sketchId", "distance", "units"}}};
    const QJsonObject pocket{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{{"sketchId", QJsonObject{{"type", "string"}}},
            {"depth", QJsonObject{{"type", "number"}}}, {"units", units}}},
        {"required", QJsonArray{"sketchId", "depth", "units"}}};
    const QJsonObject edit{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{{"featureId", QJsonObject{{"type", "string"}}},
            {"property", QJsonObject{{"type", "string"}}}, {"value", QJsonObject{}}, {"units", units}}},
        {"required", QJsonArray{"featureId", "property", "value"}}};
    const QJsonObject recompute{{"type", "object"}, {"additionalProperties", false}};
    const QJsonObject sketchId{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{{"sketchId", QJsonObject{{"type", "string"}}}}},
        {"required", QJsonArray{"sketchId"}}};
    const QJsonObject line{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{{"sketchId", QJsonObject{{"type", "string"}}},
            {"x1", QJsonObject{{"type", "number"}}}, {"y1", QJsonObject{{"type", "number"}}},
            {"x2", QJsonObject{{"type", "number"}}}, {"y2", QJsonObject{{"type", "number"}}},
            {"construction", QJsonObject{{"type", "boolean"}}}, {"units", units}}},
        {"required", QJsonArray{"sketchId", "x1", "y1", "x2", "y2", "units"}}};
    const QJsonObject editLine{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{{"sketchId", QJsonObject{{"type", "string"}}},
            {"entityId", QJsonObject{{"type", "string"}}}, {"x1", QJsonObject{{"type", "number"}}},
            {"y1", QJsonObject{{"type", "number"}}}, {"x2", QJsonObject{{"type", "number"}}},
            {"y2", QJsonObject{{"type", "number"}}}, {"units", units}}},
        {"required", QJsonArray{"sketchId", "entityId", "x1", "y1", "x2", "y2", "units"}}};
    const QJsonObject removeEntity{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{{"sketchId", QJsonObject{{"type", "string"}}},
            {"entityId", QJsonObject{{"type", "string"}}}}},
        {"required", QJsonArray{"sketchId", "entityId"}}};
    const QJsonObject topology{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{{"featureId", QJsonObject{{"type", "string"}}}}},
        {"required", QJsonArray{"featureId"}}};
    const QJsonObject profileSegment{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{{"x1", QJsonObject{{"type", "number"}}},
            {"y1", QJsonObject{{"type", "number"}}}, {"x2", QJsonObject{{"type", "number"}}},
            {"y2", QJsonObject{{"type", "number"}}}}},
        {"required", QJsonArray{"x1", "y1", "x2", "y2"}}};
    const QJsonObject editProfile{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{{"sketchId", QJsonObject{{"type", "string"}}},
            {"segments", QJsonObject{{"type", "array"}, {"items", profileSegment}}},
            {"units", units}}},
        {"required", QJsonArray{"sketchId", "segments", "units"}}};
    const QJsonObject batchOperation{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{{"tool", QJsonObject{{"type", "string"}}},
            {"arguments", QJsonObject{{"type", "object"}}}}},
        {"required", QJsonArray{"tool", "arguments"}}};
    const QJsonObject batchOperations{{"type", "array"}, {"items", batchOperation}};
    const QJsonObject batch{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{{"operations", batchOperations}}},
        {"required", QJsonArray{"operations"}}};
    const QJsonObject findTopology{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{{"featureId", QJsonObject{{"type", "string"}}},
            {"kind", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"Face", "Edge", "Vertex"}}}},
            {"surfaceKind", QJsonObject{{"type", "string"}}},
            {"curveKind", QJsonObject{{"type", "string"}}},
            {"normal", QJsonObject{{"type", "array"}}},
            {"location", QJsonObject{{"type", "array"}}},
            {"tolerance", QJsonObject{{"type", "number"}}},
            {"area", QJsonObject{{"type", "number"}}},
            {"length", QJsonObject{{"type", "number"}}}}},
        {"required", QJsonArray{"featureId"}}};
    const QJsonObject chamfer{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{{"sketchId", QJsonObject{{"type", "string"}}},
            {"corners", QJsonObject{{"type", "array"}}}, {"featureId", QJsonObject{{"type", "string"}}},
            {"edgeReferences", QJsonObject{{"type", "array"}}}, {"distance", QJsonObject{{"type", "number"}}},
            {"units", units}}},
        {"required", QJsonArray{"distance", "units"}}};
    const QJsonObject boolean{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{{"leftFeatureId", QJsonObject{{"type", "string"}}},
            {"rightFeatureId", QJsonObject{{"type", "string"}}},
            {"operation", QJsonObject{{"type", "string"},
                {"enum", QJsonArray{"Fuse", "Cut", "Common"}}}}}},
        {"required", QJsonArray{"leftFeatureId", "rightFeatureId", "operation"}}};
    const QJsonObject edgeRefs{{"type", "array"}, {"items", QJsonObject{{"type", "object"}}}};
    const QJsonObject fillet{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{{"featureId", QJsonObject{{"type", "string"}}},
            {"edgeReferences", edgeRefs}, {"radius", QJsonObject{{"type", "number"}}}, {"units", units}}},
        {"required", QJsonArray{"featureId", "edgeReferences", "radius", "units"}}};
    const QJsonObject shell{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{{"featureId", QJsonObject{{"type", "string"}}},
            {"openingFaces", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "object"}}}}},
            {"thickness", QJsonObject{{"type", "number"}}}, {"units", units}}},
        {"required", QJsonArray{"featureId", "openingFaces", "thickness", "units"}}};
    const QJsonObject revolve{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{{"sketchId", QJsonObject{{"type", "string"}}},
            {"axis", QJsonObject{{"type", "object"}}},
            {"angleDegrees", QJsonObject{{"type", "number"}}}}},
        {"required", QJsonArray{"sketchId", "axis", "angleDegrees"}}};
    const QJsonObject sweep{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{{"profileSketchId", QJsonObject{{"type", "string"}}},
            {"pathSketchId", QJsonObject{{"type", "string"}}}, {"pathReference", QJsonObject{{"type", "object"}}}}},
        {"required", QJsonArray{"profileSketchId"}}};
    const QJsonObject deleteFeature{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{{"featureId", QJsonObject{{"type", "string"}}}}},
        {"required", QJsonArray{"featureId"}}};
    const QJsonObject arc{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{{"sketchId", QJsonObject{{"type", "string"}}},
            {"center", QJsonObject{{"type", "array"}}}, {"start", QJsonObject{{"type", "array"}}},
            {"end", QJsonObject{{"type", "array"}}}, {"units", units}}},
        {"required", QJsonArray{"sketchId", "center", "start", "end", "units"}}};
    const QJsonObject constraint{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{{"sketchId", QJsonObject{{"type", "string"}}},
            {"type", QJsonObject{{"type", "string"}}}, {"entityId", QJsonObject{{"type", "string"}}},
            {"firstEntityId", QJsonObject{{"type", "string"}}}, {"secondEntityId", QJsonObject{{"type", "string"}}},
            {"first", QJsonObject{{"type", "object"}}}, {"second", QJsonObject{{"type", "object"}}},
            {"value", QJsonObject{{"type", "number"}}}, {"anchorStart", QJsonObject{{"type", "boolean"}}},
            {"constraintId", QJsonObject{{"type", "string"}}}, {"radians", QJsonObject{{"type", "number"}}}}},
        {"required", QJsonArray{"sketchId", "type"}}};
    const QJsonObject linearPattern{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{{"featureId", QJsonObject{{"type", "string"}}},
            {"direction", QJsonObject{{"type", "array"}}}, {"spacing", QJsonObject{{"type", "number"}}},
            {"count", QJsonObject{{"type", "integer"}}}, {"includeSource", QJsonObject{{"type", "boolean"}}},
            {"units", units}}},
        {"required", QJsonArray{"featureId", "direction", "spacing", "count", "units"}}};
    const QJsonObject pathPattern{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{{"featureId", QJsonObject{{"type", "string"}}},
            {"pathFeatureId", QJsonObject{{"type", "string"}}}, {"spacing", QJsonObject{{"type", "number"}}},
            {"count", QJsonObject{{"type", "integer"}}},
            {"distribution", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"FixedSpacing", "FitCount"}}}},
            {"orientation", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"Fixed", "Tangent"}}}},
            {"startOffset", QJsonObject{{"type", "number"}}}, {"endOffset", QJsonObject{{"type", "number"}}},
            {"includeSource", QJsonObject{{"type", "boolean"}}}, {"units", units}}},
        {"required", QJsonArray{"featureId", "pathFeatureId", "spacing", "count", "units"}}};
    const QJsonObject transform{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{
            {"featureIds", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}}}},
            {"mode", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"copy", "replace_result"}}}},
            {"operation", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"mirror", "translate", "rotate"}}}},
            {"origin", QJsonObject{{"type", "array"}}},
            {"normal", QJsonObject{{"type", "array"}}},
            {"vector", QJsonObject{{"type", "array"}}},
            {"axisOrigin", QJsonObject{{"type", "array"}}},
            {"axisDirection", QJsonObject{{"type", "array"}}},
            {"angleDegrees", QJsonObject{{"type", "number"}}},
            {"units", units}}},
        {"required", QJsonArray{"featureIds", "mode", "units"}}};
    const QJsonObject splitFeature{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{
            {"featureId", QJsonObject{{"type", "string"}}},
            {"componentFeatureId", QJsonObject{{"type", "string"}}},
            {"splitCoordinate", QJsonObject{{"type", "number"}}},
            {"moveNegative", QJsonObject{{"type", "array"}}}, {"units", units}}},
        {"required", QJsonArray{"featureId", "componentFeatureId", "splitCoordinate", "moveNegative", "units"}}};
    const QJsonObject componentId{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{{"componentId", QJsonObject{{"type", "string"}}}}},
        {"required", QJsonArray{"componentId"}}};
    const QJsonObject components{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{{"featureId", QJsonObject{{"type", "string"}}}}}};
    const QJsonObject componentTransform{
        {"type", "object"},
        {"additionalProperties", false},
        {"properties", QJsonObject{
            {"componentId", QJsonObject{{"type", "string"}}},
            {"operation", QJsonObject{
                {"type", "string"},
                {"enum", QJsonArray{"mirror", "translate", "rotate"}}}},
            {"mode", QJsonObject{
                {"type", "string"},
                {"enum", QJsonArray{"copy", "replace_result"}}}},
            {"origin", QJsonObject{{"type", "array"}}},
            {"normal", QJsonObject{{"type", "array"}}},
            {"vector", QJsonObject{{"type", "array"}}},
            {"axisOrigin", QJsonObject{{"type", "array"}}},
            {"axisDirection", QJsonObject{{"type", "array"}}},
            {"angleDegrees", QJsonObject{{"type", "number"}}},
            {"units", units}
        }},
        {"required", QJsonArray{"componentId", "operation", "mode", "units"}}
    };
    const QJsonObject componentSplit{
        {"type", "object"},
        {"additionalProperties", false},
        {"properties", QJsonObject{
            {"componentId", QJsonObject{{"type", "string"}}},
            {"origin", QJsonObject{{"type", "array"}}},
            {"normal", QJsonObject{{"type", "array"}}},
            {"units", units}
        }},
        {"required", QJsonArray{"componentId", "origin", "normal", "units"}}
    };
    const QJsonObject componentVisibility{{"type", "object"}, {"additionalProperties", false},
        {"properties", QJsonObject{
            {"componentId", QJsonObject{{"type", "string"}}},
            {"mode", QJsonObject{{"type", "string"},
                {"enum", QJsonArray{"visible", "hidden", "ghosted"}}}},
            {"isolate", QJsonObject{{"type", "boolean"}}}}},
        {"required", QJsonArray{"componentId", "mode"}}};
    const QJsonObject tools = QJsonObject{
        {"cad.get_model_tree", QJsonObject{{"description", "Inspect the parametric model tree"}, {"inputSchema", empty}}},
        {"cad.get_feature", QJsonObject{{"description", "Inspect one parametric feature"}, {"inputSchema", feature}}},
        {"cad.inspect_shape", QJsonObject{{"description", "Inspect OCCT shape validity and metrics"}, {"inputSchema", feature}}},
        {"cad.get_selection", QJsonObject{{"description", "Read the current viewer selection"}, {"inputSchema", empty}}},
        {"cad.get_view_state", QJsonObject{{"description", "Read viewer camera and display state"}, {"inputSchema", empty}}},
        {"cad.screenshot", QJsonObject{{"description", "Capture the current viewer as PNG"}, {"inputSchema", empty}}},
        {"cad.get_application_status", QJsonObject{{"description", "Read application and automation status"}, {"inputSchema", empty}}},
        {"cad.get_operation_status", QJsonObject{{"description", "Read an asynchronous MCP operation status"}, {"inputSchema", operationStatus}}},
        {"cad.import_ifc", QJsonObject{{"description", "Import an IFC file asynchronously through IfcImporter"}, {"inputSchema", importIfc}}},
        {"cad.new_project", QJsonObject{{"description", "Create an empty parametric project"}, {"inputSchema", newProject}}},
        {"cad.open_project", QJsonObject{{"description", "Open a .pcad project"}, {"inputSchema", project}}},
        {"cad.save_project", QJsonObject{{"description", "Save the current parametric project as .pcad"}, {"inputSchema", project}}},
        {"cad.create_sketch", QJsonObject{{"description", "Create a global or planar-face-attached Sketch"}, {"inputSchema", sketch}}},
        {"cad.add_rectangle", QJsonObject{{"description", "Add four parametric line entities to a Sketch"}, {"inputSchema", rectangle}}},
        {"cad.add_circle", QJsonObject{{"description", "Add a parametric circle entity to a Sketch"}, {"inputSchema", circle}}},
        {"cad.extrude", QJsonObject{{"description", "Create an Extrude from a Sketch"}, {"inputSchema", extrude}}},
        {"cad.pocket", QJsonObject{{"description", "Create a Pocket from a face-attached Sketch"}, {"inputSchema", pocket}}},
        {"cad.recompute", QJsonObject{{"description", "Recompute the parametric document"}, {"inputSchema", recompute}}},
        {"cad.edit_feature", QJsonObject{{"description", "Edit an existing editable feature property"}, {"inputSchema", edit}}},
        {"cad.undo", QJsonObject{{"description", "Undo the latest modeling command"}, {"inputSchema", empty}}},
        {"cad.redo", QJsonObject{{"description", "Redo the latest undone modeling command"}, {"inputSchema", empty}}},
        {"cad.get_sketch_entities", QJsonObject{{"description", "Inspect Sketch entities and constraints"}, {"inputSchema", sketchId}}},
        {"cad.add_line", QJsonObject{{"description", "Add a parametric Sketch Line"}, {"inputSchema", line}}},
        {"cad.edit_sketch_entity", QJsonObject{{"description", "Edit a Sketch Line with Undo/Redo"}, {"inputSchema", editLine}}},
        {"cad.remove_sketch_entity", QJsonObject{{"description", "Remove an unconstrained Sketch entity with Undo/Redo"}, {"inputSchema", removeEntity}}},
        {"cad.get_topology", QJsonObject{{"description", "Get stable face and edge topology references"}, {"inputSchema", topology}}},
        {"cad.find_topology", QJsonObject{{"description", "Find topology references using explicit geometric filters"}, {"inputSchema", findTopology}}},
        {"cad.inspect_model", QJsonObject{{"description", "Inspect aggregate parametric model health and geometry"}, {"inputSchema", empty}}},
        {"cad.edit_sketch_profile", QJsonObject{{"description", "Atomically replace an ordered closed Sketch profile"}, {"inputSchema", editProfile}}},
        {"cad.batch", QJsonObject{{"description", "Execute eligible modeling tools as one transaction"}, {"inputSchema", batch}}},
        {"cad.chamfer", QJsonObject{{"description", "Chamfer Sketch corners or referenced 3D edges"}, {"inputSchema", chamfer}}},
        {"cad.boolean", QJsonObject{{"description", "Fuse, Cut, or Common two feature solids"}, {"inputSchema", boolean}}},
        {"cad.fillet", QJsonObject{{"description", "Fillet referenced 3D edges"}, {"inputSchema", fillet}}},
        {"cad.shell", QJsonObject{{"description", "Create a shell with referenced opening faces"}, {"inputSchema", shell}}},
        {"cad.revolve", QJsonObject{{"description", "Revolve a Sketch around a global, Sketch, or model axis"}, {"inputSchema", revolve}}},
        {"cad.sweep", QJsonObject{{"description", "Sweep a Sketch profile along a Sketch or model edge"}, {"inputSchema", sweep}}},
        {"cad.delete_feature", QJsonObject{{"description", "Delete a feature and its dependent history when allowed"}, {"inputSchema", deleteFeature}}}
        ,{"cad.add_arc", QJsonObject{{"description", "Add a parametric Sketch Arc"}, {"inputSchema", arc}}}
        ,{"cad.add_constraint", QJsonObject{{"description", "Add a Sketch constraint"}, {"inputSchema", constraint}}}
        ,{"cad.edit_constraint", QJsonObject{{"description", "Edit a dimensional Sketch constraint"}, {"inputSchema", constraint}}}
        ,{"cad.remove_constraint", QJsonObject{{"description", "Remove a Sketch constraint"}, {"inputSchema", constraint}}}
        ,{"cad.get_constraints", QJsonObject{{"description", "Inspect Sketch constraints"}, {"inputSchema", sketchId}}}
        ,{"cad.linear_pattern", QJsonObject{{"description", "Create a parametric linear pattern"}, {"inputSchema", linearPattern}}}
        ,{"cad.path_pattern", QJsonObject{{"description", "Create a parametric path pattern"}, {"inputSchema", pathPattern}}}
        ,{"cad.get_feature_dependencies", QJsonObject{{"description", "Inspect direct feature dependencies"}, {"inputSchema", feature}}}
        ,{"cad.mirror", QJsonObject{{"description", "Create a persistent mirror transform"}, {"inputSchema", transform}}}
        ,{"cad.translate", QJsonObject{{"description", "Create a persistent translation transform"}, {"inputSchema", transform}}}
        ,{"cad.rotate", QJsonObject{{"description", "Create a persistent rotation transform"}, {"inputSchema", transform}}}
        ,{"cad.transform_model", QJsonObject{{"description", "Create a persistent parametric spatial transform"}, {"inputSchema", transform}}}
        ,{"cad.split_feature", QJsonObject{{"description", "Split one component of a composite result and move its negative-X segment"}, {"inputSchema", splitFeature}}}
        ,{"cad.get_components", QJsonObject{{"description", "Discover independently addressable solid components"}, {"inputSchema", components}}}
        ,{"cad.get_component", QJsonObject{{"description", "Resolve and inspect one persistent model component"}, {"inputSchema", componentId}}}
        ,{"cad.get_component_topology", QJsonObject{{"description", "Inspect topology of one model component"}, {"inputSchema", componentId}}}
        ,{"cad.set_component_visibility", QJsonObject{{"description", "Set visible, hidden, or ghosted state for one component"},
            {"inputSchema", componentVisibility}}}
        ,{"cad.transform_component", QJsonObject{{"description", "Transform one persistent model component"},
            {"inputSchema", componentTransform}}}
        ,{"cad.split_component", QJsonObject{{"description", "Split one persistent model component by a plane"},
            {"inputSchema", componentSplit}}}
    };
    QJsonArray result;
    for (auto it = tools.begin(); it != tools.end(); ++it) {
        QJsonObject item = it.value().toObject();
        item.insert("name", it.key());
        result.append(item);
    }
    return result;
}

QJsonObject errorResponse(const QJsonValue& id, int code, const QString& message)
{
    return QJsonObject{{"jsonrpc", "2.0"}, {"id", id},
        {"error", QJsonObject{{"code", code}, {"message", message}}}};
}

QJsonObject callApplication(const QString& socketName, const QString& tool,
                            const QJsonObject& arguments, const QJsonValue& id)
{
    QElapsedTimer timer;
    timer.start();
    const int timeoutMs = tool == "cad.open_project" || tool == "cad.save_project"
        ? ProjectOperationTimeoutMs : DefaultTimeoutMs;
    bool timeoutOverrideOk = false;
    const int timeoutOverride = qEnvironmentVariable("PARAMETRICCAD_MCP_TIMEOUT_MS")
        .toInt(&timeoutOverrideOk);
    const int effectiveTimeoutMs = timeoutOverrideOk && timeoutOverride > 0
        ? timeoutOverride : timeoutMs;
    traceMcpMessage(QString("request id=%1 tool=%2 timeoutMs=%3")
        .arg(id.toVariant().toString(), tool).arg(effectiveTimeoutMs));
    QLocalSocket socket;
    socket.connectToServer(socketName, QIODevice::ReadWrite);
    if (!socket.waitForConnected(effectiveTimeoutMs))
        return errorResponse(id, -32001, "ParametricCAD is not connected: " + socket.errorString());
    const QJsonObject request{{"jsonrpc", "2.0"}, {"id", id}, {"method", "automation/call"},
        {"params", QJsonObject{{"tool", tool}, {"arguments", arguments}}}};
    socket.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
    if (!socket.waitForBytesWritten(effectiveTimeoutMs)
        || !socket.waitForReadyRead(effectiveTimeoutMs)) {
        traceMcpMessage(QString("timeout id=%1 tool=%2 elapsedMs=%3")
            .arg(id.toVariant().toString(), tool).arg(timer.elapsed()));
        return errorResponse(id, -32002, "Timed out waiting for ParametricCAD");
    }
    QByteArray line = socket.readLine();
    while (!line.endsWith('\n') && socket.waitForReadyRead(effectiveTimeoutMs))
        line += socket.readLine();
    QJsonParseError parseError;
    const QJsonDocument response = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !response.isObject())
        return errorResponse(id, -32003, "ParametricCAD returned invalid JSON");
    traceMcpMessage(QString("response id=%1 tool=%2 elapsedMs=%3 bytes=%4")
        .arg(id.toVariant().toString(), tool).arg(timer.elapsed()).arg(line.size()));
    return response.object();
}

QJsonObject handle(const QJsonObject& request, const QString& socketName)
{
    const QJsonValue id = request.value("id");
    const QString method = request.value("method").toString();
    if (request.value("jsonrpc").toString() != "2.0") return errorResponse(id, -32600, "JSON-RPC 2.0 is required");
    if (method == "initialize") {
        return QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"result", QJsonObject{
            {"protocolVersion", "2025-06-18"},
            {"capabilities", QJsonObject{{"tools", QJsonObject{}}}},
            {"serverInfo", QJsonObject{{"name", "parametriccad-mcp"}, {"version", "0.1.0"}}}}}};
    }
    if (method == "notifications/initialized") return {};
    if (method == "tools/list") return QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"result", QJsonObject{{"tools", toolDefinitions()}}}};
    if (method != "tools/call") return errorResponse(id, -32601, "Method not found");
    const QJsonObject params = request.value("params").toObject();
    const QString tool = params.value("name").toString();
    bool known = false;
    for (const auto& value : toolDefinitions()) if (value.toObject().value("name").toString() == tool) known = true;
    if (!known) return errorResponse(id, -32601, "Unknown tool: " + tool);
    const QJsonObject appResponse = callApplication(socketName, tool, params.value("arguments").toObject(), id);
    if (appResponse.contains("error")) {
        const auto error = appResponse.value("error").toObject();
        const auto structured = QJsonObject{{"error", error}};
        const auto text = QString::fromUtf8(QJsonDocument(structured).toJson(QJsonDocument::Compact));
        return QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"result", QJsonObject{
            {"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", text}}}},
            {"structuredContent", structured}, {"isError", true}}}};
    }
    const QJsonValue value = appResponse.value("result");
    QJsonObject content;
    if (value.toObject().value("mimeType").toString() == "image/png") {
        content = QJsonObject{{"type", "image"},
            {"data", value.toObject().value("data")}, {"mimeType", "image/png"}};
    } else {
        content = QJsonObject{{"type", "text"},
            {"text", QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact))}};
    }
    return QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"result", QJsonObject{
        {"content", QJsonArray{content}}, {"structuredContent", value}}}};
}
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    const QString socketName = qEnvironmentVariable("PARAMETRICCAD_AUTOMATION_SOCKET", "parametriccad-automation");
    QTextStream input(stdin);
    QTextStream output(stdout);
    while (!input.atEnd()) {
        const QString line = input.readLine();
        if (line.trimmed().isEmpty()) continue;
        QJsonParseError parseError;
        const QJsonDocument request = QJsonDocument::fromJson(line.toUtf8(), &parseError);
        QJsonObject response;
        if (parseError.error != QJsonParseError::NoError || !request.isObject())
            response = errorResponse(QJsonValue(), -32700, "Invalid JSON");
        else response = handle(request.object(), socketName);
        if (!response.isEmpty()) { output << QJsonDocument(response).toJson(QJsonDocument::Compact) << '\n'; output.flush(); }
    }
    return 0;
}
