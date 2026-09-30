#include "migrationarchive.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>
#include <QtEndian>
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace mixxx::ohos {
namespace {
void require(bool ok, const char* message) {
    if (!ok) {
        throw std::runtime_error(message);
    }
}
quint16 u16(const QByteArray& bytes, int offset) {
    return qFromLittleEndian<quint16>(bytes.constData() + offset);
}
quint32 u32(const QByteArray& bytes, int offset) {
    return qFromLittleEndian<quint32>(bytes.constData() + offset);
}
quint64 u64(const QByteArray& bytes, int offset) {
    return qFromLittleEndian<quint64>(bytes.constData() + offset);
}
qint64 positiveId(const QJsonValue& value) {
    const auto id = value.toInteger(-1);
    require(id > 0 && id <= std::numeric_limits<int>::max(), "Invalid track ID");
    return id;
}
}

void validateMigrationPath(const QString& name) {
    require(!name.isEmpty() && !name.startsWith('/') && !name.contains('\\') &&
                    !name.contains(':') && !name.contains(QChar(0)), "Unsafe archive path");
    for (const auto& part : name.split('/')) {
        require(!part.isEmpty() && part != "." && part != "..", "Unsafe archive path");
    }
}

QByteArray MigrationArchive::read(quint64 offset, quint64 size) {
    require(offset <= quint64(m_file.size()) && size <= quint64(m_file.size()) - offset &&
                    size <= 16 * 1024 * 1024, "Archive bounds invalid");
    require(m_file.seek(offset), "Cannot seek archive");
    auto bytes = m_file.read(size);
    require(quint64(bytes.size()) == size, "Truncated archive");
    return bytes;
}

MigrationArchive::MigrationArchive(const QString& path) : m_file(path) {
    require(m_file.open(QIODevice::ReadOnly), "Cannot open migration package");
    require(m_file.size() >= 22, "Truncated archive");
    const auto tailOffset = std::max<qint64>(0, m_file.size() - 65557);
    const auto tail = read(tailOffset, m_file.size() - tailOffset);
    int eocd = -1;
    for (int i = tail.size() - 22; i >= 0; --i) {
        if (u32(tail, i) == 0x06054b50 && i + 22 + u16(tail, i + 20) == tail.size()) {
            eocd = i;
            break;
        }
    }
    require(eocd >= 0, "ZIP directory missing");
    require(u16(tail, eocd + 4) == 0 && u16(tail, eocd + 6) == 0 &&
                    u16(tail, eocd + 8) == u16(tail, eocd + 10), "Multi-volume ZIP unsupported");
    quint64 count = u16(tail, eocd + 10);
    quint64 centralSize = u32(tail, eocd + 12);
    quint64 centralOffset = u32(tail, eocd + 16);
    quint64 directoryEnd = tailOffset + eocd;
    if (count == 0xffff || centralSize == 0xffffffff || centralOffset == 0xffffffff) {
        const auto locator = read(directoryEnd - 20, 20);
        require(u32(locator, 0) == 0x07064b50 && u32(locator, 4) == 0 &&
                        u32(locator, 16) == 1, "Invalid ZIP64 locator");
        directoryEnd = u64(locator, 8);
        const auto record = read(directoryEnd, 56);
        require(u32(record, 0) == 0x06064b50 && u64(record, 4) >= 44 &&
                        u32(record, 16) == 0 && u32(record, 20) == 0 &&
                        u64(record, 24) == u64(record, 32), "Invalid ZIP64 directory");
        count = u64(record, 32);
        centralSize = u64(record, 40);
        centralOffset = u64(record, 48);
    }
    require(count > 0 && count <= 50001 && centralOffset <= directoryEnd &&
                    centralSize <= directoryEnd - centralOffset, "ZIP directory bounds invalid");
    quint64 cursor = centralOffset;
    for (quint64 i = 0; i < count; ++i) {
        const auto header = read(cursor, 46);
        require(u32(header, 0) == 0x02014b50 && u16(header, 10) == 0 &&
                        (u16(header, 8) & ~quint16(0x0808)) == 0 &&
                        u16(header, 34) == 0, "Only unencrypted stored ZIP supported");
        require(((u32(header, 38) >> 16) & 0170000) != 0120000, "Archive links unsupported");
        const auto nameBytes = read(cursor + 46, u16(header, 28));
        const auto name = QString::fromUtf8(nameBytes);
        require(name.toUtf8() == nameBytes, "Invalid UTF-8 archive path");
        validateMigrationPath(name);
        require(!m_entries.contains(name), "Duplicate archive file");
        quint64 compressed = u32(header, 20);
        quint64 size = u32(header, 24);
        quint64 offset = u32(header, 42);
        const auto extra = read(cursor + 46 + nameBytes.size(), u16(header, 30));
        bool zip64 = false;
        for (int p = 0; p + 4 <= extra.size();) {
            int length = u16(extra, p + 2);
            require(p + 4 + length <= extra.size(), "Invalid ZIP extra field");
            if (u16(extra, p) == 1) {
                zip64 = true;
                int q = p + 4;
                const int end = q + length;
                for (auto* value : {&size, &compressed, &offset}) {
                    if (*value == 0xffffffff) {
                        require(q + 8 <= end, "Truncated ZIP64 field");
                        *value = u64(extra, q);
                        q += 8;
                    }
                }
            }
            p += 4 + length;
        }
        require((size != 0xffffffff && compressed != 0xffffffff && offset != 0xffffffff) ||
                        zip64, "Missing ZIP64 fields");
        require(size == compressed && offset < centralOffset, "Invalid stored file size");
        const auto local = read(offset, 30);
        require(u32(local, 0) == 0x04034b50 && u16(local, 8) == 0 &&
                        u16(local, 6) == u16(header, 8) &&
                        read(offset + 30, u16(local, 26)) == nameBytes, "ZIP local header mismatch");
        const quint64 data = offset + 30 + u16(local, 26) + u16(local, 28);
        require(data <= centralOffset && size <= centralOffset - data, "File crosses ZIP directory");
        m_entries.insert(name, {size, data});
        cursor += 46 + nameBytes.size() + extra.size() + u16(header, 32);
        require(cursor <= centralOffset + centralSize, "ZIP directory overrun");
    }
    require(cursor == centralOffset + centralSize && m_entries.contains("manifest.json"), "Invalid ZIP directory");
    const auto manifestEntry = m_entries.value("manifest.json");
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(read(manifestEntry.offset, manifestEntry.size), &error);
    require(error.error == QJsonParseError::NoError && document.isObject(), "Invalid migration manifest");
    m_manifest = document.object();
    require(m_manifest.value("format") == "pomelo-mixxx-transfer" &&
                    m_manifest.value("version").toInt() == 1, "Unsupported migration format");
    require(QRegularExpression("^[a-f0-9]{32}$").match(m_manifest.value("packageId").toString()).hasMatch(), "Invalid package ID");
    QMap<QString, QString> roles;
    const auto files = m_manifest.value("files").toArray();
    require(files.size() + 1 == m_entries.size(), "Manifest file count mismatch");
    for (const auto& value : files) {
        const auto file = value.toObject();
        const auto name = file.value("path").toString();
        const auto role = file.value("role").toString();
        validateMigrationPath(name);
        require(!roles.contains(name) && m_entries.contains(name), "Manifest file mismatch");
        const bool permitted = (role == "settings" && name == "portable-settings.json") ||
                (role == "config" && name == "config/Mixxx/mixxx.cfg") ||
                (role == "database" && name == "config/Mixxx/mixxxdb.sqlite") ||
                (role == "resource" && name.startsWith("config/Mixxx/") &&
                        (name == "config/Mixxx/effects.xml" || name == "config/Mixxx/samplers.xml" ||
                                name.startsWith("config/Mixxx/effects/") || name.startsWith("config/Mixxx/controllers/") ||
                                name.startsWith("config/Mixxx/analysis/") || name.endsWith(".kbd.cfg"))) ||
                (role == "audio" && name.startsWith("audio/")) ||
                (role == "artwork" && name.startsWith("artwork/"));
        require(permitted, "Unsupported migration resource");
        const qint64 size = file.value("size").toInteger(-1);
        require(size >= 0 && quint64(size) == m_entries.value(name).size &&
                        QRegularExpression("^[a-f0-9]{64}$").match(file.value("sha256").toString()).hasMatch(), "Invalid file size or digest");
        require(m_size <= quint64(std::numeric_limits<qint64>::max()) - quint64(size), "Migration size overflow");
        m_size += size;
        roles.insert(name, role);
    }
    require(roles.value("config/Mixxx/mixxx.cfg") == "config", "Migration configuration missing");
    QSet<qint64> ids;
    QMap<qint64, QString> locations;
    for (const auto& value : m_manifest.value("tracks").toArray()) {
        const auto track = value.toObject();
        auto id = positiveId(track.value("trackId"));
        auto location = positiveId(track.value("locationId"));
        require(!ids.contains(id), "Duplicate track ID");
        ids.insert(id);
        const auto audio = track.value("audioPath").toString();
        require(audio.isEmpty() || roles.value(audio) == "audio", "Track audio missing");
        require(!locations.contains(location) || locations.value(location) == audio, "Conflicting location mapping");
        locations.insert(location, audio);
    }
    require(ids.isEmpty() || roles.value("config/Mixxx/mixxxdb.sqlite") == "database", "Migration database missing");
    QSet<qint64> covers;
    for (const auto& value : m_manifest.value("covers").toArray()) {
        const auto cover = value.toObject();
        auto id = positiveId(cover.value("trackId"));
        require(ids.contains(id) && !covers.contains(id) &&
                        roles.value(cover.value("coverPath").toString()) == "artwork", "Invalid artwork mapping");
        covers.insert(id);
    }
}

QJsonObject MigrationArchive::manifest() const {
    return m_manifest;
}
quint64 MigrationArchive::unpackedSize() const {
    return m_size;
}

void MigrationArchive::copy(const QString& name, QFile* output, const QByteArray& hash,
        const std::atomic_bool& cancelled, const std::function<void(quint64)>& progress) {
    const auto entry = m_entries.value(name);
    require(m_file.seek(entry.offset), "Cannot seek migration file");
    QCryptographicHash digest(QCryptographicHash::Sha256);
    quint64 remaining = entry.size;
    while (remaining > 0) {
        require(!cancelled.load(), "Import cancelled");
        const auto data = m_file.read(std::min<quint64>(remaining, 1024 * 1024));
        require(!data.isEmpty() && quint64(data.size()) <= remaining, "Migration file truncated");
        digest.addData(data);
        require(output->write(data) == data.size(), "Cannot write migration file");
        remaining -= data.size();
        progress(data.size());
    }
    require(digest.result().toHex() == hash, "Migration file checksum mismatch");
    require(output->flush(), "Cannot flush migration file");
}

void MigrationArchive::extract(const QString& directory, const std::atomic_bool& cancelled,
        const std::function<void(quint64)>& progress) {
    require(QDir().mkpath(directory), "Cannot create migration staging directory");
    for (const auto& value : m_manifest.value("files").toArray()) {
        require(!cancelled.load(), "Import cancelled");
        const auto file = value.toObject();
        const auto name = file.value("path").toString();
        const QString destination = directory + '/' + name;
        require(QDir().mkpath(QFileInfo(destination).absolutePath()), "Cannot create migration directory");
        QFile output(destination);
        require(output.open(QIODevice::WriteOnly | QIODevice::NewOnly), "Migration output already exists or cannot be opened");
        copy(name, &output, file.value("sha256").toString().toLatin1(), cancelled, progress);
    }
}

}
