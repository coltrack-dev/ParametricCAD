#pragma once

#include <QObject>

class MainWindow;
class QLocalServer;
class QLocalSocket;

class CadAutomationService final : public QObject
{
    Q_OBJECT

public:
    explicit CadAutomationService(MainWindow& window, QObject* parent = nullptr);
    ~CadAutomationService() override;

    bool start(const QString& socketName, QString& error);
    void stop();
    QString socketName() const;

private slots:
    void acceptConnection();
    void readClient();
    void clientDisconnected();

private:
    void closeClient(QLocalSocket* client);

    MainWindow& window_;
    QLocalServer* server_{nullptr};
};
