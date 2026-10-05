#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <memory>

namespace cad::persistence {

class ProjectArchiveWriter final
{
public:
    ProjectArchiveWriter() = default;
    ~ProjectArchiveWriter();

    ProjectArchiveWriter(const ProjectArchiveWriter&) = delete;
    ProjectArchiveWriter& operator=(const ProjectArchiveWriter&) = delete;

    bool open(const QString& destination, QString& error);
    bool addEntry(const QString& name, const QByteArray& data, QString& error);
    bool close(QString& error);

private:
    void cleanupTemporaryFile() noexcept;

    void* archive_{nullptr};
    QString destination_;
    QString temporaryPath_;
};

class ProjectArchiveReader final
{
public:
    explicit ProjectArchiveReader(const QString& path);
    ~ProjectArchiveReader();

    ProjectArchiveReader(const ProjectArchiveReader&) = delete;
    ProjectArchiveReader& operator=(const ProjectArchiveReader&) = delete;

    bool open(QString& error);
    bool readEntry(const QString& name, QByteArray& data, QString& error,
                   qsizetype maximumSize = 512 * 1024 * 1024) const;

private:
    void* archive_{nullptr};
    QString path_;
    QStringList entries_;
};

bool isProjectArchive(const QString& path);

} // namespace cad::persistence
