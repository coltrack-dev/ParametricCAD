#include "automation/CadAutomationService.h"

#include "viewer/MainWindow.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QPointer>

namespace {
constexpr qsizetype MaxRequestBytes = 1024 * 1024;

bool traceAutomation()
{
    return qEnvironmentVariableIsSet("PARAMETRICCAD_TRACE_MCP");
}
}

CadAutomationService::CadAutomationService(MainWindow& window, QObject* parent)
    : QObject(parent), window_(window), server_(new QLocalServer(this))
{
    connect(server_, &QLocalServer::newConnection,
        this, &CadAutomationService::acceptConnection);
}

CadAutomationService::~CadAutomationService()
{
    stop();
}

bool CadAutomationService::start(const QString& socketName, QString& error)
{
    stop();
    if (socketName.isEmpty()) {
        error = "Automation socket name must not be empty";
        return false;
    }
    server_->setSocketOptions(QLocalServer::UserAccessOption);
    if (!server_->listen(socketName)) {
        const QString firstError = server_->errorString();
        // A previous crash may leave a stale filesystem socket. Only remove
        // it after the initial listen failed, so a live automation endpoint
        // is never silently displaced by another GUI instance.
        QLocalServer::removeServer(socketName);
        if (!server_->listen(socketName)) {
            error = firstError + "; retry: " + server_->errorString();
            return false;
        }
    }
    return true;
}

void CadAutomationService::stop()
{
    if (!server_) return;
    while (server_->hasPendingConnections()) {
        closeClient(server_->nextPendingConnection());
    }
    server_->close();
    if (!server_->serverName().isEmpty())
        QLocalServer::removeServer(server_->serverName());
}

QString CadAutomationService::socketName() const
{
    return server_ ? server_->serverName() : QString{};
}

void CadAutomationService::acceptConnection()
{
    while (server_->hasPendingConnections()) {
        auto* client = server_->nextPendingConnection();
        connect(client, &QLocalSocket::readyRead, this, &CadAutomationService::readClient);
        connect(client, &QLocalSocket::disconnected, this, &CadAutomationService::clientDisconnected);
    }
}

void CadAutomationService::readClient()
{
    auto* client = qobject_cast<QLocalSocket*>(sender());
    if (!client) return;
    if (client->bytesAvailable() > MaxRequestBytes) {
        closeClient(client);
        return;
    }
    while (client->canReadLine()) {
        const QByteArray line = client->readLine(MaxRequestBytes + 1);
        if (line.size() > MaxRequestBytes) {
            closeClient(client);
            return;
        }
        QJsonParseError parseError;
        const QJsonDocument request = QJsonDocument::fromJson(line, &parseError);
        QJsonObject response;
        QElapsedTimer timer;
        timer.start();
        if (parseError.error != QJsonParseError::NoError || !request.isObject()) {
            response = QJsonObject{{"jsonrpc", "2.0"}, {"id", QJsonValue()},
                {"error", QJsonObject{{"code", -32700}, {"message", "Invalid JSON"}}}};
        } else {
            const auto requestObject = request.object();
            const QString requestId = requestObject.value("id").toVariant().toString();
            const QString tool = requestObject.value("params").toObject().value("tool").toString();
            if (traceAutomation()) {
                qInfo().noquote() << QString("[MCP] receive id=%1 tool=%2")
                    .arg(requestId, tool);
            }
            response = window_.handleAutomationRequest(requestObject);
            if (traceAutomation()) {
                qInfo().noquote() << QString("[MCP] dispatch-complete id=%1 tool=%2 elapsedMs=%3")
                    .arg(requestId, tool).arg(timer.elapsed());
            }
        }
        const QByteArray serialized = QJsonDocument(response).toJson(QJsonDocument::Compact);
        client->write(serialized);
        client->write("\n");
        client->flush();
        if (traceAutomation()) {
            qInfo().noquote() << QString("[MCP] response-written bytes=%1 elapsedMs=%2 flushed=%3")
                .arg(serialized.size()).arg(timer.elapsed())
                .arg(client->bytesToWrite() == 0 ? 1 : 0);
        }
    }
}

void CadAutomationService::clientDisconnected()
{
    auto* client = qobject_cast<QLocalSocket*>(sender());
    if (client) client->deleteLater();
}

void CadAutomationService::closeClient(QLocalSocket* client)
{
    if (!client) return;
    client->disconnect(this);
    client->disconnectFromServer();
    client->deleteLater();
}
