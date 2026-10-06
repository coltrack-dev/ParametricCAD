#include "model/ProjectArchive.h"

#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryFile>

#include <minizip/unzip.h>
#include <minizip/zip.h>

#include <filesystem>
#include <algorithm>

namespace cad::persistence {
namespace {

constexpr int ChunkSize = 1024 * 1024;

bool validEntryName(const QString& name)
{
    if (name.isEmpty() || name.startsWith('/') || name.contains('\\')
        || name.contains(':')) return false;
    const auto parts = name.split('/', Qt::KeepEmptyParts);
    for (const auto& part : parts) {
        if (part.isEmpty() || part == "." || part == "..") return false;
    }
    return true;
}

QString zipError(const char* operation, const QString& name = {})
{
    return QStringLiteral("%1%2 failed").arg(QString::fromLatin1(operation),
        name.isEmpty() ? QString() : QStringLiteral(" for '%1'").arg(name));
}

bool replaceFile(const QString& temporary, const QString& destination, QString& error)
{
    std::error_code code;
    // std::string is not a safe representation for non-ASCII Windows paths.
    // Use the wide filesystem path there; UTF-8 is the native narrow path
    // representation on the Unix platforms supported by the project.
#ifdef _WIN32
    const std::filesystem::path temporaryPath(temporary.toStdWString());
    const std::filesystem::path destinationPath(destination.toStdWString());
#else
    const std::filesystem::path temporaryPath(temporary.toUtf8().constData());
    const std::filesystem::path destinationPath(destination.toUtf8().constData());
#endif
    std::filesystem::rename(temporaryPath, destinationPath, code);
    if (!code) return true;

    // std::filesystem::rename replaces an existing file on POSIX, but not on
    // all supported platforms. The archive is complete before this fallback.
    if ((QFile::exists(destination) && !QFile::remove(destination))
        || !QFile::rename(temporary, destination)) {
        error = QStringLiteral("Could not replace project file: %1")
            .arg(QString::fromStdString(code.message()));
        return false;
    }
    return true;
}

}

ProjectArchiveWriter::~ProjectArchiveWriter()
{
    if (archive_) {
        zipClose(reinterpret_cast<zipFile>(archive_), nullptr);
        archive_ = nullptr;
    }
    cleanupTemporaryFile();
}

void ProjectArchiveWriter::cleanupTemporaryFile() noexcept
{
    if (!temporaryPath_.isEmpty()) QFile::remove(temporaryPath_);
}

bool ProjectArchiveWriter::open(const QString& destination, QString& error)
{
    error.clear();
    destination_ = destination;
    const QFileInfo info(destination);
    if (!info.dir().exists()) {
        error = QStringLiteral("Project directory does not exist");
        return false;
    }

    QTemporaryFile temporary(info.dir().filePath(QStringLiteral(".%1.XXXXXX.tmp")
        .arg(info.fileName())));
    temporary.setAutoRemove(false);
    if (!temporary.open()) {
        error = temporary.errorString();
        return false;
    }
    temporaryPath_ = temporary.fileName();
    temporary.close();
    archive_ = zipOpen64(temporaryPath_.toLocal8Bit().constData(), APPEND_STATUS_CREATE);
    if (!archive_) {
        error = zipError("Opening project archive");
        cleanupTemporaryFile();
        return false;
    }
    return true;
}

bool ProjectArchiveWriter::addEntry(const QString& name, const QByteArray& data, QString& error)
{
    if (!archive_ || !validEntryName(name)) {
        error = QStringLiteral("Invalid archive entry path");
        return false;
    }
    const auto encodedName = name.toUtf8();
    zip_fileinfo info{};
    auto* archive = reinterpret_cast<zipFile>(archive_);
    if (zipOpenNewFileInZip64(archive, encodedName.constData(), &info, nullptr, 0,
                              nullptr, 0, nullptr, Z_DEFLATED, Z_DEFAULT_COMPRESSION, 1)
        != ZIP_OK) {
        error = zipError("Creating archive entry", name);
        return false;
    }
    for (qsizetype offset = 0; offset < data.size(); offset += ChunkSize) {
        const auto count = static_cast<unsigned>(std::min<qsizetype>(ChunkSize,
            data.size() - offset));
        if (zipWriteInFileInZip(archive, data.constData() + offset, count) != ZIP_OK) {
            zipCloseFileInZip(archive);
            error = zipError("Writing archive entry", name);
            return false;
        }
    }
    if (zipCloseFileInZip(archive) != ZIP_OK) {
        error = zipError("Closing archive entry", name);
        return false;
    }
    return true;
}

bool ProjectArchiveWriter::close(QString& error)
{
    if (!archive_) {
        error = QStringLiteral("Project archive is not open");
        return false;
    }
    const auto result = zipClose(reinterpret_cast<zipFile>(archive_), nullptr);
    archive_ = nullptr;
    if (result != ZIP_OK) {
        error = zipError("Closing project archive");
        cleanupTemporaryFile();
        return false;
    }
    if (!replaceFile(temporaryPath_, destination_, error)) {
        cleanupTemporaryFile();
        return false;
    }
    temporaryPath_.clear();
    return true;
}

ProjectArchiveReader::ProjectArchiveReader(const QString& path) : path_(path) {}

ProjectArchiveReader::~ProjectArchiveReader()
{
    if (archive_) unzClose(reinterpret_cast<unzFile>(archive_));
}

bool ProjectArchiveReader::open(QString& error)
{
    error.clear();
    archive_ = unzOpen64(path_.toLocal8Bit().constData());
    if (!archive_) {
        error = QStringLiteral("Invalid project archive");
        return false;
    }
    auto* archive = reinterpret_cast<unzFile>(archive_);
    if (unzGoToFirstFile(archive) != UNZ_OK) {
        error = QStringLiteral("Project archive is empty");
        return false;
    }
    do {
        unz_file_info64 info{};
        char name[4096]{};
        if (unzGetCurrentFileInfo64(archive, &info, name, sizeof(name), nullptr, 0,
                                    nullptr, 0) != UNZ_OK) {
            error = QStringLiteral("Invalid project archive entry");
            return false;
        }
        const QString entry = QString::fromUtf8(name);
        if (!validEntryName(entry)) {
            error = QStringLiteral("Unsafe archive entry path: %1").arg(entry);
            return false;
        }
        entries_.append(entry);
    } while (unzGoToNextFile(archive) == UNZ_OK);
    return true;
}

bool ProjectArchiveReader::readEntry(const QString& name, QByteArray& data, QString& error,
                                     qsizetype maximumSize) const
{
    data.clear();
    if (!archive_ || !validEntryName(name) || !entries_.contains(name)) {
        error = QStringLiteral("Missing archive entry: %1").arg(name);
        return false;
    }
    auto* archive = reinterpret_cast<unzFile>(archive_);
    if (unzGoToFirstFile(archive) != UNZ_OK) {
        error = QStringLiteral("Invalid project archive");
        return false;
    }
    bool found = false;
    do {
        char currentName[4096]{};
        unz_file_info64 info{};
        if (unzGetCurrentFileInfo64(archive, &info, currentName, sizeof(currentName),
                                    nullptr, 0, nullptr, 0) != UNZ_OK) {
            error = QStringLiteral("Invalid project archive entry");
            return false;
        }
        if (QString::fromUtf8(currentName) == name) {
            found = true;
            break;
        }
    } while (unzGoToNextFile(archive) == UNZ_OK);
    if (!found) {
        error = QStringLiteral("Missing archive entry: %1").arg(name);
        return false;
    }

    unz_file_info64 info{};
    char currentName[4096]{};
    if (unzGetCurrentFileInfo64(archive, &info, currentName, sizeof(currentName),
                                nullptr, 0, nullptr, 0) != UNZ_OK
        || info.uncompressed_size > static_cast<ZPOS64_T>(maximumSize)
        || unzOpenCurrentFile(archive) != UNZ_OK) {
        error = QStringLiteral("Invalid or oversized archive entry: %1").arg(name);
        return false;
    }
    data.reserve(static_cast<qsizetype>(info.uncompressed_size));
    QByteArray buffer(ChunkSize, Qt::Uninitialized);
    int read = 0;
    do {
        read = unzReadCurrentFile(archive, buffer.data(), buffer.size());
        if (read < 0) {
            unzCloseCurrentFile(archive);
            error = QStringLiteral("Could not read archive entry: %1").arg(name);
            return false;
        }
        if (read > 0) data.append(buffer.constData(), read);
    } while (read > 0);
    if (unzCloseCurrentFile(archive) != UNZ_OK) {
        error = QStringLiteral("Could not close archive entry: %1").arg(name);
        return false;
    }
    return true;
}

bool isProjectArchive(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const auto signature = file.read(4);
    return signature.size() == 4 && static_cast<unsigned char>(signature[0]) == 'P'
        && static_cast<unsigned char>(signature[1]) == 'K';
}

} // namespace cad::persistence
