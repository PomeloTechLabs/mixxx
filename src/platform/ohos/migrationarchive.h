#pragma once

#include <QFile>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <atomic>
#include <functional>

namespace mixxx::ohos {

class MigrationArchive final {
  public:
    explicit MigrationArchive(const QString& path);
    QJsonObject manifest() const;
    quint64 unpackedSize() const;
    void extract(const QString& directory, const std::atomic_bool& cancelled,
            const std::function<void(quint64)>& progress);

  private:
    struct Entry {
        quint64 size;
        quint64 offset;
    };
    QByteArray read(quint64 offset, quint64 size);
    void copy(const QString& name, QFile* output, const QByteArray& hash,
            const std::atomic_bool& cancelled,
            const std::function<void(quint64)>& progress);
    QFile m_file;
    QMap<QString, Entry> m_entries;
    QJsonObject m_manifest;
    quint64 m_size = 0;
};

void validateMigrationPath(const QString& name);

}
