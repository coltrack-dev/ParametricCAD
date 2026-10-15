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

    QJsonObject call(const QString& name, const QJsonObject& arguments = {})
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
        const QJsonObject request{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/call"},
            {"params", QJsonObject{{"name", name}, {"arguments", arguments}}}};
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
        QVERIFY(invalid.value("error").toObject().value("code").toInt() == -32602);
        const auto missing = call("cad.get_feature", QJsonObject{{"featureId", "missing"}});
        QVERIFY(missing.value("error").toObject().value("code").toInt() == -32004);
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

    void cleanupTestCase()
    {
        application_.terminate();
        if (!application_.waitForFinished(5000)) application_.kill();
    }
};

QTEST_MAIN(McpModelingIntegrationTests)
#include "mcp_modeling_integration_tests.moc"
