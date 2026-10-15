#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

#ifndef PARAMETRICCAD_APP_PATH
#error PARAMETRICCAD_APP_PATH must be defined by CMake
#endif
#ifndef PARAMETRICCAD_MCP_PATH
#error PARAMETRICCAD_MCP_PATH must be defined by CMake
#endif
#ifndef PARAMETRICCAD_SOURCE_DIR
#error PARAMETRICCAD_SOURCE_DIR must be defined by CMake
#endif

class McpModelingIntegrationTests final : public QObject
{
    Q_OBJECT

private:
    QProcess application_;
    QTemporaryDir temporaryDirectory_;
    QString socketName_;

    QJsonObject callRequest(const QJsonObject& request)
    {
        QProcess mcp;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert("PARAMETRICCAD_AUTOMATION_SOCKET", socketName_);
        mcp.setProcessEnvironment(environment);
        mcp.setProgram(QStringLiteral(PARAMETRICCAD_MCP_PATH));
        mcp.start();
        if (!mcp.waitForStarted(5000)) {
            qWarning() << "MCP failed to start:" << mcp.errorString();
            return {};
        }
        mcp.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
        mcp.closeWriteChannel();
        if (!mcp.waitForFinished(15000)) {
            qWarning() << "MCP did not finish:" << mcp.errorString();
            mcp.kill();
            return {};
        }
        const auto lines = mcp.readAllStandardOutput().split('\n');
        for (const auto& line : lines) {
            if (line.trimmed().isEmpty()) continue;
            QJsonParseError error;
            const auto document = QJsonDocument::fromJson(line, &error);
            if (error.error == QJsonParseError::NoError && document.isObject())
                return document.object();
        }
        qWarning() << "MCP output:" << mcp.readAllStandardError();
        return {};
    }

    QJsonObject call(const QString& name, const QJsonObject& arguments = {})
    {
        return callRequest(QJsonObject{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/call"},
            {"params", QJsonObject{{"name", name}, {"arguments", arguments}}}});
    }

    QJsonObject protocolCall(const QString& method, const QJsonObject& params = {})
    {
        return callRequest(QJsonObject{{"jsonrpc", "2.0"}, {"id", 1}, {"method", method},
            {"params", params}});
    }

    QJsonObject callTool(const QString& name, const QJsonObject& arguments = {})
    {
        const auto response = call(name, arguments);
        if (!response.contains("result")) {
            qWarning() << "MCP tool failed:" << name
                       << QJsonDocument(response).toJson(QJsonDocument::Compact);
            return {};
        }
        return response.value("result").toObject().value("structuredContent").toObject();
    }

    QString createRectangleExtrude(double width, double height, double depth,
                                   double x = 0.0, double y = 0.0,
                                   const QString& support = QStringLiteral("XY"))
    {
        const auto sketch = callTool("cad.create_sketch", QJsonObject{{"support", support}})
            .value("featureId").toString();
        if (sketch.isEmpty()) return {};
        callTool("cad.add_rectangle", QJsonObject{{"sketchId", sketch}, {"x", x}, {"y", y},
            {"width", width}, {"height", height}, {"units", "mm"}});
        const auto result = callTool("cad.extrude", QJsonObject{{"sketchId", sketch},
            {"distance", depth}, {"units", "mm"}});
        const auto id = result.value("featureId").toString();
        return id;
    }

    QJsonObject topologyReference(const QString& featureId, const QString& kind, int index = 0)
    {
        const auto topology = callTool("cad.get_topology", QJsonObject{{"featureId", featureId}});
        const auto values = topology.value(kind).toArray();
        if (values.isEmpty()) return {};
        return values.at(index < values.size() ? index : 0).toObject();
    }

    void verifyOneSolid(const QString& featureId, double x, double y, double z,
                        double tolerance = 0.2)
    {
        const auto shape = callTool("cad.inspect_shape", QJsonObject{{"featureId", featureId}});
        QVERIFY(shape.value("valid").toBool());
        QCOMPARE(shape.value("solidCount").toInt(), 1);
        const auto bounds = shape.value("boundingBox").toObject();
        const auto min = bounds.value("min").toArray();
        const auto max = bounds.value("max").toArray();
        QVERIFY(std::abs(max.at(0).toDouble() - min.at(0).toDouble() - x) < tolerance);
        QVERIFY(std::abs(max.at(1).toDouble() - min.at(1).toDouble() - y) < tolerance);
        QVERIFY(std::abs(max.at(2).toDouble() - min.at(2).toDouble() - z) < tolerance);
    }

    void verifyDependency(const QString& featureId, const QString& dependencyId)
    {
        QVERIFY(callTool("cad.get_feature_dependencies", QJsonObject{{"featureId", featureId}})
            .value("dependencies").toArray().contains(dependencyId));
    }

    void verifyStructuredToolError(const QString& tool, const QJsonObject& arguments,
                                   const QString& messagePart = {})
    {
        const auto response = call(tool, arguments);
        QVERIFY(response.value("result").toObject().value("isError").toBool());
        const auto error = response.value("result").toObject().value("structuredContent")
            .toObject().value("error").toObject();
        QVERIFY(error.value("code").toInt() == -32602 || error.value("code").toInt() == -32020
            || error.value("code").toInt() == -32001 || error.value("code").toInt() == -32002);
        if (!messagePart.isEmpty())
            QVERIFY(error.value("message").toString().contains(messagePart, Qt::CaseInsensitive));
    }

private slots:
    void initTestCase()
    {
        QVERIFY(temporaryDirectory_.isValid());
        socketName_ = temporaryDirectory_.filePath("parametriccad-mcp-2a");
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert("QT_QPA_PLATFORM", "xcb");
        environment.insert("PARAMETRICCAD_ENABLE_AUTOMATION", "1");
        environment.insert("PARAMETRICCAD_AUTOMATION_SOCKET", socketName_);
        application_.setProcessEnvironment(environment);
        application_.setProgram(QStringLiteral(PARAMETRICCAD_APP_PATH));
        application_.setArguments({"--enable-automation"});
        application_.start();
        QVERIFY2(application_.waitForStarted(10000), qPrintable(application_.errorString()));

        bool connected = false;
        for (int attempt = 0; attempt < 30 && !connected; ++attempt) {
            QLocalSocket socket;
            socket.connectToServer(socketName_);
            connected = socket.waitForConnected(500);
            if (!connected) QTest::qWait(100);
        }
        QVERIFY2(connected, qPrintable(QString("Automation socket did not become ready: %1; app output: %2")
            .arg(socketName_, QString::fromUtf8(application_.readAllStandardError()))));
        QVERIFY2(application_.state() != QProcess::NotRunning,
                 qPrintable(QString::fromUtf8(application_.readAllStandardError())));
    }

    void modelingWorkflowThroughMcp()
    {
        QVERIFY(callTool("cad.get_application_status").value("automationEnabled").toBool());
        QCOMPARE(callTool("cad.new_project").value("featureCount").toInt(), 0);

        const auto sketch = callTool("cad.create_sketch", QJsonObject{{"support", "XY"}});
        const QString sketchId = sketch.value("featureId").toString();
        QVERIFY(!sketchId.isEmpty());
        callTool("cad.add_rectangle", QJsonObject{{"sketchId", sketchId}, {"x", 0.0},
            {"y", 0.0}, {"width", 120.0}, {"height", 80.0}, {"units", "mm"}});

        const auto extrude = callTool("cad.extrude", QJsonObject{{"sketchId", sketchId},
            {"distance", 30.0}, {"units", "mm"}});
        const QString extrudeId = extrude.value("featureId").toString();
        QVERIFY(!extrudeId.isEmpty());

        const auto extrudeFeature = callTool("cad.get_feature", QJsonObject{{"featureId", extrudeId}});
        const auto faces = extrudeFeature.value("topology").toObject().value("faces").toArray();
        QVERIFY(!faces.isEmpty());
        QJsonObject faceReference;
        for (const auto& candidate : faces) {
            if (candidate.toObject().value("semanticId").isString()
                || candidate.toObject().value("signature").isObject()) {
                faceReference = candidate.toObject();
                break;
            }
        }
        QVERIFY(!faceReference.isEmpty());

        const auto attachedSketch = callTool("cad.create_sketch",
            QJsonObject{{"faceReference", faceReference}});
        const QString attachedSketchId = attachedSketch.value("featureId").toString();
        QVERIFY(!attachedSketchId.isEmpty());
        callTool("cad.add_rectangle", QJsonObject{{"sketchId", attachedSketchId}, {"x", 2.0},
            {"y", 2.0}, {"width", 116.0}, {"height", 76.0}, {"units", "mm"}});

        const auto pocket = callTool("cad.pocket", QJsonObject{{"sketchId", attachedSketchId},
            {"depth", 28.0}, {"units", "mm"}});
        const QString pocketId = pocket.value("featureId").toString();
        QVERIFY(!pocketId.isEmpty());
        const auto shape = callTool("cad.inspect_shape", QJsonObject{{"featureId", pocketId}});
        QVERIFY(shape.value("valid").toBool());
        QCOMPARE(shape.value("solidCount").toInt(), 1);
        QVERIFY(shape.value("volume").toDouble() > 0.0);

        const QString path = qEnvironmentVariable("PARAMETRICCAD_MCP_EXAMPLE_PATH",
            temporaryDirectory_.filePath("electronics-enclosure.pcad"));
        QVERIFY(callTool("cad.save_project", QJsonObject{{"path", path}}).value("success").toBool());
        QVERIFY(QFileInfo::exists(path));
        QVERIFY(callTool("cad.open_project", QJsonObject{{"path", path}}).value("success").toBool());

        const double beforeEdit = callTool("cad.inspect_shape", QJsonObject{{"featureId", pocketId}})
            .value("volume").toDouble();
        QVERIFY(callTool("cad.edit_feature", QJsonObject{{"featureId", extrudeId},
            {"property", "distance"}, {"value", 32.0}, {"units", "mm"}}).value("success").toBool());
        QVERIFY(callTool("cad.recompute").value("success").toBool());
        const double afterEdit = callTool("cad.inspect_shape", QJsonObject{{"featureId", pocketId}})
            .value("volume").toDouble();
        QVERIFY(afterEdit > beforeEdit);

        QVERIFY(callTool("cad.undo").value("success").toBool());
        const double afterUndo = callTool("cad.inspect_shape", QJsonObject{{"featureId", pocketId}})
            .value("volume").toDouble();
        QVERIFY(afterUndo < afterEdit);
        QVERIFY(callTool("cad.redo").value("success").toBool());
        const double afterRedo = callTool("cad.inspect_shape", QJsonObject{{"featureId", pocketId}})
            .value("volume").toDouble();
        QVERIFY(std::abs(afterRedo - afterEdit) < 1.0e-6);
        QVERIFY(callTool("cad.inspect_shape", QJsonObject{{"featureId", pocketId}})
                    .value("valid").toBool());
    }

    void invalidArgumentsReturnStructuredErrors()
    {
        const auto invalid = call("cad.add_circle", QJsonObject{{"sketchId", "missing"},
            {"x", 0.0}, {"y", 0.0}, {"radius", 2.0}});
        QVERIFY(invalid.value("result").toObject().value("isError").toBool());
        QCOMPARE(invalid.value("result").toObject().value("structuredContent")
            .toObject().value("error").toObject().value("code").toInt(), -32602);
        const auto missing = call("cad.get_feature", QJsonObject{{"featureId", "missing"}});
        QVERIFY(missing.value("result").toObject().value("isError").toBool());
        QCOMPARE(missing.value("result").toObject().value("structuredContent")
            .toObject().value("error").toObject().value("code").toInt(), -32004);
    }

    void stage1SolidOperationsThroughMcp()
    {
        QVERIFY(callTool("cad.new_project").value("featureCount").toInt() == 0);
        const auto plateSketch = callTool("cad.create_sketch", QJsonObject{{"support", "XY"}})
            .value("featureId").toString();
        QVERIFY(!plateSketch.isEmpty());
        callTool("cad.add_rectangle", QJsonObject{{"sketchId", plateSketch}, {"x", 0.0},
            {"y", 0.0}, {"width", 100.0}, {"height", 60.0}, {"units", "mm"}});
        const auto plate = callTool("cad.extrude", QJsonObject{{"sketchId", plateSketch},
            {"distance", 6.0}, {"units", "mm"}}).value("featureId").toString();
        QVERIFY(!plate.isEmpty());

        const auto plateFeature = callTool("cad.get_feature", QJsonObject{{"featureId", plate}});
        QJsonObject topFace;
        for (const auto& value : plateFeature.value("topology").toObject().value("faces").toArray()) {
            if (value.toObject().value("semanticId").toString() == "Box.Face:+Z") {
                topFace = value.toObject();
                break;
            }
        }
        QVERIFY(!topFace.isEmpty());
        const auto pipeSketch = callTool("cad.create_sketch",
            QJsonObject{{"faceReference", topFace}}).value("featureId").toString();
        QVERIFY(!pipeSketch.isEmpty());
        callTool("cad.add_circle", QJsonObject{{"sketchId", pipeSketch}, {"x", 0.0},
            {"y", 0.0}, {"radius", 16.0}, {"units", "mm"}});
        callTool("cad.add_circle", QJsonObject{{"sketchId", pipeSketch}, {"x", 0.0},
            {"y", 0.0}, {"radius", 12.0}, {"units", "mm"}});
        const auto pipe = callTool("cad.extrude", QJsonObject{{"sketchId", pipeSketch},
            {"distance", 20.0}, {"units", "mm"}}).value("featureId").toString();
        QVERIFY(!pipe.isEmpty());

        const auto fused = callTool("cad.boolean", QJsonObject{{"leftFeatureId", plate},
            {"rightFeatureId", pipe}, {"operation", "Fuse"}});
        QVERIFY(fused.value("success").toBool());
        const auto fusedId = fused.value("featureId").toString();
        QVERIFY(!fusedId.isEmpty());
        const auto fusedShape = callTool("cad.inspect_shape", QJsonObject{{"featureId", fusedId}});
        QVERIFY(fusedShape.value("valid").toBool());
        QCOMPARE(fusedShape.value("solidCount").toInt(), 1);
        const auto fusedBox = fusedShape.value("boundingBox").toObject();
        const auto fusedMin = fusedBox.value("min").toArray();
        const auto fusedMax = fusedBox.value("max").toArray();
        QVERIFY(std::abs(fusedMax.at(0).toDouble() - fusedMin.at(0).toDouble() - 100.0) < 0.2);
        QVERIFY(std::abs(fusedMax.at(1).toDouble() - fusedMin.at(1).toDouble() - 60.0) < 0.2);
        QVERIFY(std::abs(fusedMax.at(2).toDouble() - fusedMin.at(2).toDouble() - 26.0) < 0.2);

        const auto tree = callTool("cad.get_model_tree");
        const auto booleanFeature = tree.value("features").toArray().last().toObject();
        const auto dependencies = booleanFeature.value("dependencies").toArray();
        QVERIFY(dependencies.contains(plate));
        QVERIFY(dependencies.contains(pipe));

        QVERIFY(callTool("cad.undo").value("success").toBool());
        QVERIFY(callTool("cad.redo").value("success").toBool());
        QVERIFY(callTool("cad.inspect_shape", QJsonObject{{"featureId", fusedId}})
                    .value("valid").toBool());

        const QString saved = temporaryDirectory_.filePath("stage1-fused.pcad");
        QVERIFY(callTool("cad.save_project", QJsonObject{{"path", saved}})
                    .value("success").toBool());
        QVERIFY(callTool("cad.open_project", QJsonObject{{"path", saved}})
                    .value("success").toBool());
        QVERIFY(callTool("cad.inspect_shape", QJsonObject{{"featureId", fusedId}})
                    .value("solidCount").toInt() == 1);

        QVERIFY(callTool("cad.edit_feature", QJsonObject{{"featureId", plate},
            {"property", "distance"}, {"value", 7.0}, {"units", "mm"}})
                    .value("success").toBool());
        QVERIFY(callTool("cad.recompute").value("success").toBool());
        const auto resized = callTool("cad.inspect_shape", QJsonObject{{"featureId", fusedId}});
        QVERIFY(resized.value("valid").toBool());
        QCOMPARE(resized.value("solidCount").toInt(), 1);
        QVERIFY(std::abs(resized.value("boundingBox").toObject().value("max").toArray().at(2).toDouble()
                         - resized.value("boundingBox").toObject().value("min").toArray().at(2).toDouble()
                         - 27.0) < 0.2);
    }

    void mountingPlateCornerEditingThroughMcp()
    {
        const QString source = QDir(QStringLiteral(PARAMETRICCAD_SOURCE_DIR))
            .filePath("examples/mcp_mounting_plate.pcad");
        QVERIFY2(QFileInfo::exists(source), qPrintable(source));
        QVERIFY(callTool("cad.open_project", QJsonObject{{"path", source}})
                    .value("success").toBool());

        const auto tree = callTool("cad.get_model_tree");
        QCOMPARE(tree.value("featureCount").toInt(), 4);
        const auto features = tree.value("features").toArray();
        QVERIFY(features.size() == 4);
        const QString sketchId = features.at(0).toObject().value("id").toString();
        const QString finalFeatureId = features.at(3).toObject().value("id").toString();
        QVERIFY(!sketchId.isEmpty());
        QVERIFY(!finalFeatureId.isEmpty());

        const auto before = callTool("cad.inspect_shape", QJsonObject{{"featureId", finalFeatureId}});
        QVERIFY(before.value("valid").toBool());
        QCOMPARE(before.value("solidCount").toInt(), 1);
        const double originalVolume = before.value("volume").toDouble();
        QVERIFY(std::abs(originalVolume - 33436.460394670736) < 1.0e-3);

        const auto entities = callTool("cad.get_sketch_entities", QJsonObject{{"sketchId", sketchId}});
        QCOMPARE(entities.value("entityCount").toInt(), 4);
        const auto entityArray = entities.value("entities").toArray();
        QVERIFY(entityArray.size() == 4);
        QJsonArray corners;
        for (int i = 0; i < entityArray.size(); ++i) {
            const auto first = entityArray.at(i).toObject();
            const auto firstStart = first.value("start").toArray();
            const auto firstEnd = first.value("end").toArray();
            for (int j = i + 1; j < entityArray.size(); ++j) {
                const auto second = entityArray.at(j).toObject();
                const auto secondStart = second.value("start").toArray();
                const auto secondEnd = second.value("end").toArray();
                const auto distance = [](const QJsonArray& a, const QJsonArray& b) {
                    const double dx = a.at(0).toDouble() - b.at(0).toDouble();
                    const double dy = a.at(1).toDouble() - b.at(1).toDouble();
                    return std::hypot(dx, dy);
                };
                if (distance(firstEnd, secondStart) < 1.0e-6
                    || distance(firstEnd, secondEnd) < 1.0e-6
                    || distance(firstStart, secondStart) < 1.0e-6
                    || distance(firstStart, secondEnd) < 1.0e-6) {
                    corners.append(QJsonObject{{"firstLineId", first.value("id")},
                        {"secondLineId", second.value("id")}});
                }
            }
        }
        QCOMPARE(corners.size(), 4);

        const auto chamfer = callTool("cad.chamfer", QJsonObject{{"sketchId", sketchId},
            {"corners", corners}, {"distance", 5.0}, {"units", "mm"}});
        QVERIFY(chamfer.value("success").toBool());
        const auto editedEntities = callTool("cad.get_sketch_entities",
            QJsonObject{{"sketchId", sketchId}});
        QCOMPARE(editedEntities.value("entityCount").toInt(), 8);

        const auto after = callTool("cad.inspect_shape", QJsonObject{{"featureId", finalFeatureId}});
        QVERIFY(after.value("valid").toBool());
        QCOMPARE(after.value("solidCount").toInt(), 1);
        QVERIFY(std::abs(after.value("volume").toDouble()
                         - (originalVolume - 300.0)) < 1.0e-3);
        const auto bbox = after.value("boundingBox").toObject();
        const auto min = bbox.value("min").toArray();
        const auto max = bbox.value("max").toArray();
        QVERIFY(std::abs(max.at(0).toDouble() - min.at(0).toDouble() - 100.0) < 0.1);
        QVERIFY(std::abs(max.at(1).toDouble() - min.at(1).toDouble() - 60.0) < 0.1);
        QVERIFY(std::abs(max.at(2).toDouble() - min.at(2).toDouble() - 6.0) < 0.1);

        const auto holeSketch = callTool("cad.get_sketch_entities",
            QJsonObject{{"sketchId", features.at(2).toObject().value("id")}});
        QCOMPARE(holeSketch.value("entityCount").toInt(), 5);
        QVERIFY(callTool("cad.recompute").value("success").toBool());

        QVERIFY(callTool("cad.undo").value("success").toBool());
        const auto undone = callTool("cad.inspect_shape", QJsonObject{{"featureId", finalFeatureId}});
        QVERIFY(std::abs(undone.value("volume").toDouble() - originalVolume) < 1.0e-3);
        QVERIFY(callTool("cad.redo").value("success").toBool());
        const auto redone = callTool("cad.inspect_shape", QJsonObject{{"featureId", finalFeatureId}});
        QVERIFY(std::abs(redone.value("volume").toDouble() - (originalVolume - 300.0)) < 1.0e-3);

        const QString saved = temporaryDirectory_.filePath("mounting-plate-chamfered.pcad");
        QVERIFY(callTool("cad.save_project", QJsonObject{{"path", saved}})
                    .value("success").toBool());
        QVERIFY(QFileInfo::exists(saved));
        QVERIFY(callTool("cad.open_project", QJsonObject{{"path", saved}})
                    .value("success").toBool());
        const auto reopenedTree = callTool("cad.get_model_tree");
        QCOMPARE(reopenedTree.value("featureCount").toInt(), 4);
        const auto reopenedFinal = reopenedTree.value("features").toArray().at(3)
            .toObject().value("id").toString();
        const auto reopenedShape = callTool("cad.inspect_shape",
            QJsonObject{{"featureId", reopenedFinal}});
        QVERIFY(reopenedShape.value("valid").toBool());
        QCOMPARE(reopenedShape.value("solidCount").toInt(), 1);
        QVERIFY(std::abs(reopenedShape.value("volume").toDouble()
                         - (originalVolume - 300.0)) < 1.0e-3);
    }

    void booleanCutAndCommonFixturesThroughMcp()
    {
        QVERIFY(callTool("cad.new_project").value("featureCount").toInt() == 0);
        const auto base = createRectangleExtrude(20.0, 20.0, 10.0);
        const auto overlapping = createRectangleExtrude(10.0, 10.0, 10.0, 5.0, 5.0);
        QVERIFY(!base.isEmpty() && !overlapping.isEmpty());

        const auto cut = callTool("cad.boolean", QJsonObject{{"leftFeatureId", base},
            {"rightFeatureId", overlapping}, {"operation", "Cut"}});
        QVERIFY(cut.value("success").toBool());
        const auto cutId = cut.value("featureId").toString();
        verifyOneSolid(cutId, 20.0, 20.0, 10.0);
        QVERIFY(std::abs(callTool("cad.inspect_shape", QJsonObject{{"featureId", cutId}})
            .value("volume").toDouble() - 3000.0) < 1.0);

        QVERIFY(callTool("cad.undo").value("success").toBool());
        QVERIFY(callTool("cad.redo").value("success").toBool());
        const auto saved = temporaryDirectory_.filePath("boolean-cut.pcad");
        QVERIFY(callTool("cad.save_project", QJsonObject{{"path", saved}}).value("success").toBool());
        QVERIFY(callTool("cad.open_project", QJsonObject{{"path", saved}}).value("success").toBool());
        verifyOneSolid(cutId, 20.0, 20.0, 10.0);

        const auto common = callTool("cad.boolean", QJsonObject{{"leftFeatureId", base},
            {"rightFeatureId", overlapping}, {"operation", "Common"}});
        QVERIFY(common.value("success").toBool());
        const auto commonId = common.value("featureId").toString();
        verifyOneSolid(commonId, 10.0, 10.0, 10.0);
        QVERIFY(std::abs(callTool("cad.inspect_shape", QJsonObject{{"featureId", commonId}})
            .value("volume").toDouble() - 1000.0) < 1.0);

        const auto disjoint = createRectangleExtrude(5.0, 5.0, 10.0, 30.0, 0.0);
        const auto contained = createRectangleExtrude(5.0, 5.0, 10.0, 7.5, 7.5);
        QVERIFY(!disjoint.isEmpty() && !contained.isEmpty());
        verifyStructuredToolError("cad.boolean", QJsonObject{{"leftFeatureId", base},
            {"rightFeatureId", disjoint}, {"operation", "Common"}});
        const auto containedCut = callTool("cad.boolean", QJsonObject{{"leftFeatureId", base},
            {"rightFeatureId", contained}, {"operation", "Cut"}});
        QVERIFY(containedCut.value("success").toBool());
        QVERIFY(std::abs(callTool("cad.inspect_shape", QJsonObject{{"featureId", containedCut.value("featureId")}})
            .value("volume").toDouble() - 3750.0) < 1.0);
    }

    void filletAndChamferFixturesThroughMcp()
    {
        QVERIFY(callTool("cad.new_project").value("featureCount").toInt() == 0);
        const auto box = createRectangleExtrude(20.0, 20.0, 10.0);
        const auto edge = topologyReference(box, "edges");
        QVERIFY(!edge.isEmpty());
        const auto fillet = callTool("cad.fillet", QJsonObject{{"featureId", box},
            {"edgeReferences", QJsonArray{edge}}, {"radius", 1.0}, {"units", "mm"}});
        QVERIFY(fillet.value("success").toBool());
        const auto filletId = fillet.value("featureId").toString();
        verifyOneSolid(filletId, 20.0, 20.0, 10.0);
        verifyDependency(filletId, box);
        QVERIFY(callTool("cad.undo").value("success").toBool());
        QVERIFY(callTool("cad.redo").value("success").toBool());
        const auto saved = temporaryDirectory_.filePath("fillet.pcad");
        QVERIFY(callTool("cad.save_project", QJsonObject{{"path", saved}}).value("success").toBool());
        QVERIFY(callTool("cad.open_project", QJsonObject{{"path", saved}}).value("success").toBool());
        verifyOneSolid(filletId, 20.0, 20.0, 10.0);

        const auto chamfer = callTool("cad.chamfer", QJsonObject{{"featureId", box},
            {"edgeReferences", QJsonArray{edge}}, {"distance", 1.0}, {"units", "mm"}});
        QVERIFY(chamfer.value("success").toBool());
        const auto chamferId = chamfer.value("featureId").toString();
        verifyOneSolid(chamferId, 20.0, 20.0, 10.0);
        verifyDependency(chamferId, box);
        const auto chamferSaved = temporaryDirectory_.filePath("chamfer.pcad");
        QVERIFY(callTool("cad.save_project", QJsonObject{{"path", chamferSaved}}).value("success").toBool());
        QVERIFY(callTool("cad.open_project", QJsonObject{{"path", chamferSaved}}).value("success").toBool());
        verifyOneSolid(chamferId, 20.0, 20.0, 10.0);
        const int before = callTool("cad.get_model_tree").value("featureCount").toInt();
        verifyStructuredToolError("cad.fillet", QJsonObject{{"featureId", box},
            {"edgeReferences", QJsonArray{edge}}, {"radius", 20.0}, {"units", "mm"}}, "failed");
        QCOMPARE(callTool("cad.get_model_tree").value("featureCount").toInt(), before);
    }

    void shellFixtureThroughMcp()
    {
        QVERIFY(callTool("cad.new_project").value("featureCount").toInt() == 0);
        const auto box = createRectangleExtrude(20.0, 20.0, 10.0);
        const auto topology = callTool("cad.get_topology", QJsonObject{{"featureId", box}});
        QJsonObject top;
        for (const auto& value : topology.value("faces").toArray()) {
            if (value.toObject().value("semanticId").toString() == "Box.Face:+Z") {
                top = value.toObject();
                break;
            }
        }
        QVERIFY(!top.isEmpty());
        const auto shell = callTool("cad.shell", QJsonObject{{"featureId", box},
            {"openingFaces", QJsonArray{top}}, {"thickness", 1.0}, {"units", "mm"}});
        QVERIFY(shell.value("success").toBool());
        const auto shellId = shell.value("featureId").toString();
        verifyOneSolid(shellId, 22.0223, 22.0223, 11.0223, 0.05);
        verifyDependency(shellId, box);
        QVERIFY(std::abs(callTool("cad.inspect_shape", QJsonObject{{"featureId", shellId}})
            .value("volume").toDouble() - 1296.3421747365599) < 2.0);
        QVERIFY(callTool("cad.undo").value("success").toBool());
        QVERIFY(callTool("cad.redo").value("success").toBool());
        const auto saved = temporaryDirectory_.filePath("shell.pcad");
        QVERIFY(callTool("cad.save_project", QJsonObject{{"path", saved}}).value("success").toBool());
        QVERIFY(callTool("cad.open_project", QJsonObject{{"path", saved}}).value("success").toBool());
        verifyOneSolid(shellId, 22.0223, 22.0223, 11.0223, 0.05);
        const int before = callTool("cad.get_model_tree").value("featureCount").toInt();
        verifyStructuredToolError("cad.shell", QJsonObject{{"featureId", box},
            {"openingFaces", QJsonArray{}}, {"thickness", 1.0}, {"units", "mm"}});
        QCOMPARE(callTool("cad.get_model_tree").value("featureCount").toInt(), before);
    }

    void revolveFixtureThroughMcp()
    {
        QVERIFY(callTool("cad.new_project").value("featureCount").toInt() == 0);
        const auto sketch = callTool("cad.create_sketch", QJsonObject{{"support", "XZ"}})
            .value("featureId").toString();
        QVERIFY(!sketch.isEmpty());
        callTool("cad.add_rectangle", QJsonObject{{"sketchId", sketch}, {"x", 10.0}, {"y", 0.0},
            {"width", 10.0}, {"height", 10.0}, {"units", "mm"}});
        const auto revolve = callTool("cad.revolve", QJsonObject{{"sketchId", sketch},
            {"axis", QJsonObject{{"type", "GlobalZ"}}}, {"angleDegrees", 360.0}});
        QVERIFY(revolve.value("success").toBool());
        const auto id = revolve.value("featureId").toString();
        verifyOneSolid(id, 40.0, 40.0, 10.0);
        verifyDependency(id, sketch);
        QVERIFY(std::abs(callTool("cad.inspect_shape", QJsonObject{{"featureId", id}})
            .value("volume").toDouble() - 3000.0 * std::acos(-1.0)) < 5.0);
        QVERIFY(callTool("cad.undo").value("success").toBool());
        QVERIFY(callTool("cad.redo").value("success").toBool());
        const auto saved = temporaryDirectory_.filePath("revolve.pcad");
        QVERIFY(callTool("cad.save_project", QJsonObject{{"path", saved}}).value("success").toBool());
        QVERIFY(callTool("cad.open_project", QJsonObject{{"path", saved}}).value("success").toBool());
        verifyOneSolid(id, 40.0, 40.0, 10.0);
    }

    void sweepFixtureThroughMcp()
    {
        QVERIFY(callTool("cad.new_project").value("featureCount").toInt() == 0);
        const auto profile = callTool("cad.create_sketch", QJsonObject{{"support", "YZ"}})
            .value("featureId").toString();
        QVERIFY(!profile.isEmpty());
        callTool("cad.add_rectangle", QJsonObject{{"sketchId", profile}, {"x", -2.0}, {"y", -2.0},
            {"width", 4.0}, {"height", 4.0}, {"units", "mm"}});
        const auto path = callTool("cad.create_sketch", QJsonObject{{"support", "XY"}})
            .value("featureId").toString();
        QVERIFY(!path.isEmpty());
        callTool("cad.add_line", QJsonObject{{"sketchId", path}, {"x1", 0.0}, {"y1", 0.0},
            {"x2", 30.0}, {"y2", 0.0}, {"units", "mm"}});
        const auto sweep = callTool("cad.sweep", QJsonObject{{"profileSketchId", profile},
            {"pathSketchId", path}});
        QVERIFY(sweep.value("success").toBool());
        const auto id = sweep.value("featureId").toString();
        verifyOneSolid(id, 30.0, 4.0, 4.0, 0.5);
        verifyDependency(id, profile);
        verifyDependency(id, path);
        QVERIFY(std::abs(callTool("cad.inspect_shape", QJsonObject{{"featureId", id}})
            .value("volume").toDouble() - 480.0) < 1.0);
        QVERIFY(callTool("cad.undo").value("success").toBool());
        QVERIFY(callTool("cad.redo").value("success").toBool());
        const auto saved = temporaryDirectory_.filePath("sweep.pcad");
        QVERIFY(callTool("cad.save_project", QJsonObject{{"path", saved}}).value("success").toBool());
        QVERIFY(callTool("cad.open_project", QJsonObject{{"path", saved}}).value("success").toBool());
        verifyOneSolid(id, 30.0, 4.0, 4.0, 0.5);
        verifyStructuredToolError("cad.sweep", QJsonObject{{"profileSketchId", profile},
            {"pathSketchId", profile}});
        QVERIFY(callTool("cad.inspect_shape", QJsonObject{{"featureId", id}}).value("valid").toBool());
    }

    void patternFixturesThroughMcp()
    {
        QVERIFY(callTool("cad.new_project").value("featureCount").toInt() == 0);
        const auto source = createRectangleExtrude(5.0, 5.0, 5.0);
        const auto linear = callTool("cad.linear_pattern", QJsonObject{{"featureId", source},
            {"direction", QJsonArray{1.0, 0.0, 0.0}}, {"spacing", 10.0}, {"count", 3},
            {"includeSource", true}, {"units", "mm"}});
        QVERIFY(linear.value("success").toBool());
        const auto linearId = linear.value("featureId").toString();
        const auto linearShape = callTool("cad.inspect_shape", QJsonObject{{"featureId", linearId}});
        QVERIFY(linearShape.value("valid").toBool());
        QCOMPARE(linearShape.value("solidCount").toInt(), 3);
        QVERIFY(std::abs(linearShape.value("volume").toDouble() - 375.0) < 1.0);
        verifyDependency(linearId, source);
        QVERIFY(std::abs(linearShape.value("boundingBox").toObject().value("max").toArray().at(0).toDouble()
                         - linearShape.value("boundingBox").toObject().value("min").toArray().at(0).toDouble()
                         - 25.0) < 0.5);
        QVERIFY(callTool("cad.undo").value("success").toBool());
        QVERIFY(callTool("cad.redo").value("success").toBool());
        const auto saved = temporaryDirectory_.filePath("linear-pattern.pcad");
        QVERIFY(callTool("cad.save_project", QJsonObject{{"path", saved}}).value("success").toBool());
        QVERIFY(callTool("cad.open_project", QJsonObject{{"path", saved}}).value("success").toBool());
        QCOMPARE(callTool("cad.inspect_shape", QJsonObject{{"featureId", linearId}})
            .value("solidCount").toInt(), 3);

        QVERIFY(callTool("cad.new_project").value("featureCount").toInt() == 0);
        const auto pathSource = createRectangleExtrude(5.0, 5.0, 5.0);
        const auto path = callTool("cad.create_sketch", QJsonObject{{"support", "XY"}})
            .value("featureId").toString();
        QVERIFY(!path.isEmpty());
        callTool("cad.add_line", QJsonObject{{"sketchId", path}, {"x1", 0.0}, {"y1", 0.0},
            {"x2", 30.0}, {"y2", 0.0}, {"units", "mm"}});
        const auto pattern = callTool("cad.path_pattern", QJsonObject{{"featureId", pathSource},
            {"pathFeatureId", path}, {"spacing", 10.0}, {"count", 3},
            {"distribution", "FitCount"}, {"orientation", "Fixed"}, {"units", "mm"}});
        QVERIFY(pattern.value("success").toBool());
        const auto patternShape = callTool("cad.inspect_shape",
            QJsonObject{{"featureId", pattern.value("featureId")}});
        QVERIFY(patternShape.value("valid").toBool());
        QCOMPARE(patternShape.value("solidCount").toInt(), 3);
        QVERIFY(std::abs(patternShape.value("volume").toDouble() - 375.0) < 1.0);
        verifyDependency(pattern.value("featureId").toString(), pathSource);
        verifyDependency(pattern.value("featureId").toString(), path);
        const auto pathSaved = temporaryDirectory_.filePath("path-pattern.pcad");
        QVERIFY(callTool("cad.save_project", QJsonObject{{"path", pathSaved}}).value("success").toBool());
        QVERIFY(callTool("cad.open_project", QJsonObject{{"path", pathSaved}}).value("success").toBool());
        QCOMPARE(callTool("cad.inspect_shape", QJsonObject{{"featureId", pattern.value("featureId")}})
            .value("solidCount").toInt(), 3);
    }

    void sketchArcAndConstraintLifecycleThroughMcp()
    {
        QVERIFY(callTool("cad.new_project").value("featureCount").toInt() == 0);
        const auto sketch = callTool("cad.create_sketch", QJsonObject{{"support", "XY"}})
            .value("featureId").toString();
        QVERIFY(!sketch.isEmpty());
        callTool("cad.add_arc", QJsonObject{{"sketchId", sketch},
            {"center", QJsonArray{0.0, 0.0}}, {"start", QJsonArray{10.0, 0.0}},
            {"end", QJsonArray{0.0, 10.0}}, {"units", "mm"}});
        callTool("cad.add_line", QJsonObject{{"sketchId", sketch},
            {"x1", 0.0}, {"y1", 0.0}, {"x2", 20.0}, {"y2", 0.0}, {"units", "mm"}});
        const auto sketchEntities = callTool("cad.get_sketch_entities", QJsonObject{{"sketchId", sketch}})
            .value("entities").toArray();
        const auto lineId = sketchEntities.last().toObject().value("id").toString();
        QVERIFY(!lineId.isEmpty());
        const auto entities = callTool("cad.get_sketch_entities", QJsonObject{{"sketchId", sketch}});
        QCOMPARE(entities.value("entityCount").toInt(), 2);

        QVERIFY(callTool("cad.add_constraint", QJsonObject{{"sketchId", sketch},
            {"type", "Distance"}, {"entityId", lineId}, {"value", 20.0}, {"anchorStart", true}})
            .value("success").toBool());
        const auto constraints = callTool("cad.get_constraints", QJsonObject{{"sketchId", sketch}});
        QCOMPARE(constraints.value("constraintCount").toInt(), 1);
        const auto constraintId = constraints.value("constraints").toArray().first().toObject().value("id").toString();
        QVERIFY(!constraintId.isEmpty());
        QVERIFY(callTool("cad.edit_constraint", QJsonObject{{"sketchId", sketch},
            {"constraintId", constraintId}, {"type", "Distance"}, {"value", 25.0}})
            .value("success").toBool());
        QVERIFY(callTool("cad.remove_constraint", QJsonObject{{"sketchId", sketch},
            {"constraintId", constraintId}}).value("success").toBool());
        QCOMPARE(callTool("cad.get_constraints", QJsonObject{{"sketchId", sketch}})
            .value("constraintCount").toInt(), 0);
        QVERIFY(callTool("cad.undo").value("success").toBool());
        QCOMPARE(callTool("cad.get_constraints", QJsonObject{{"sketchId", sketch}})
            .value("constraintCount").toInt(), 1);
        QVERIFY(callTool("cad.redo").value("success").toBool());
    }

    void dependencyDeletionAndProtocolLifecycleThroughMcp()
    {
        const auto initialized = protocolCall("initialize", QJsonObject{
            {"protocolVersion", "2025-06-18"}, {"capabilities", QJsonObject{}},
            {"clientInfo", QJsonObject{{"name", "mcp-2c-live-test"}, {"version", "1"}}}});
        QCOMPARE(initialized.value("result").toObject().value("protocolVersion").toString(),
            QString("2025-06-18"));
        const auto listed = protocolCall("tools/list");
        const auto tools = listed.value("result").toObject().value("tools").toArray();
        const QStringList requiredTools{"cad.boolean", "cad.fillet", "cad.shell", "cad.revolve",
            "cad.delete_feature", "cad.sweep", "cad.linear_pattern", "cad.path_pattern",
            "cad.add_arc", "cad.add_constraint", "cad.edit_constraint", "cad.remove_constraint",
            "cad.get_constraints", "cad.get_feature_dependencies"};
        for (const auto& required : requiredTools) {
            bool found = false;
            for (const auto& tool : tools)
                if (tool.toObject().value("name").toString() == required) found = true;
            QVERIFY2(found, qPrintable("Missing tool: " + required));
        }

        QVERIFY(callTool("cad.new_project").value("featureCount").toInt() == 0);
        const auto sketch = callTool("cad.create_sketch", QJsonObject{{"support", "XY"}})
            .value("featureId").toString();
        QVERIFY(!sketch.isEmpty());
        callTool("cad.add_rectangle", QJsonObject{{"sketchId", sketch}, {"x", 0.0}, {"y", 0.0},
            {"width", 10.0}, {"height", 10.0}, {"units", "mm"}});
        const auto extrude = callTool("cad.extrude", QJsonObject{{"sketchId", sketch},
            {"distance", 5.0}, {"units", "mm"}}).value("featureId").toString();
        QVERIFY(callTool("cad.get_feature_dependencies", QJsonObject{{"featureId", extrude}})
            .value("dependencies").toArray().contains(sketch));
        const int before = callTool("cad.get_model_tree").value("featureCount").toInt();
        QVERIFY(callTool("cad.delete_feature", QJsonObject{{"featureId", sketch}})
            .value("success").toBool());
        QCOMPARE(callTool("cad.get_model_tree").value("featureCount").toInt(), before - 2);
        QVERIFY(callTool("cad.undo").value("success").toBool());
        QCOMPARE(callTool("cad.get_model_tree").value("featureCount").toInt(), before);
        QVERIFY(callTool("cad.redo").value("success").toBool());

        verifyStructuredToolError("cad.fillet", QJsonObject{{"featureId", "missing"},
            {"edgeReferences", QJsonArray{}}, {"radius", 1.0}, {"units", "mm"}}, "references");
        const auto invalidTool = call("cad.not_a_tool");
        QCOMPARE(invalidTool.value("error").toObject().value("code").toInt(), -32601);
        const auto malformed = call("cad.boolean", QJsonObject{{"leftFeatureId", sketch}});
        QVERIFY(malformed.value("result").toObject().value("isError").toBool());
        QCOMPARE(malformed.value("result").toObject().value("structuredContent")
            .toObject().value("error").toObject().value("code").toInt(), -32602);

        application_.terminate();
        QVERIFY(application_.waitForFinished(5000));
        const auto disconnected = call("cad.get_application_status");
        QVERIFY(disconnected.value("result").toObject().value("isError").toBool());
        QCOMPARE(disconnected.value("result").toObject().value("structuredContent")
            .toObject().value("error").toObject().value("code").toInt(), -32001);

        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert("QT_QPA_PLATFORM", "xcb");
        environment.insert("PARAMETRICCAD_ENABLE_AUTOMATION", "1");
        environment.insert("PARAMETRICCAD_AUTOMATION_SOCKET", socketName_);
        application_.setProcessEnvironment(environment);
        application_.setProgram(QStringLiteral(PARAMETRICCAD_APP_PATH));
        application_.setArguments({"--enable-automation"});
        application_.start();
        QVERIFY(application_.waitForStarted(10000));
        bool connected = false;
        for (int attempt = 0; attempt < 30 && !connected; ++attempt) {
            QLocalSocket socket;
            socket.connectToServer(socketName_);
            connected = socket.waitForConnected(500);
            if (!connected) QTest::qWait(100);
        }
        QVERIFY(connected);
        QVERIFY(callTool("cad.get_application_status").value("automationEnabled").toBool());
    }

    void cleanupTestCase()
    {
        application_.terminate();
        if (!application_.waitForFinished(5000)) application_.kill();
    }
};

QTEST_MAIN(McpModelingIntegrationTests)
#include "mcp_modeling_integration_tests.moc"
