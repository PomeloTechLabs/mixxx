#include "migration.h"

#include <QDateTime>
#include <QDir>
#include <QDomDocument>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QProgressDialog>
#include <QSaveFile>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStorageInfo>
#include <QTimer>
#include <QUuid>
#include <QtConcurrent>
#include <atomic>
#include <algorithm>
#include <memory>
#include <stdexcept>

#include "database/schemamanager.h"
#include "platform/ohos/migrationarchive.h"
#include "util/cmdlineargs.h"
#include "util/logging.h"

namespace mixxx::ohos {
namespace {
void check(bool success, const char* message) {
    if (!success) {
        throw std::runtime_error(message);
    }
}

void writeJson(const QString& path, const QJsonObject& object) {
    QSaveFile output(path);
    check(output.open(QIODevice::WriteOnly), "Cannot write migration state");
    const auto bytes = QJsonDocument(object).toJson();
    check(output.write(bytes) == bytes.size() && output.commit(), "Cannot commit migration state");
}

QString filesRoot(const UserSettingsPointer& settings) {
    const auto parent = QFileInfo(QDir::cleanPath(settings->getSettingsPath())).absolutePath();
    return QFileInfo(parent).fileName() == "profiles" ? QFileInfo(parent).absolutePath() : parent;
}

void activateProfile(const QString& root, const QString& id) {
    QSaveFile output(root + "/active-profile");
    const auto bytes = id.toUtf8();
    check(output.open(QIODevice::WriteOnly) && output.write(bytes) == bytes.size() &&
                    output.commit(), "Cannot select profile");
}

void migrateDatabase(const QString& path, const QJsonObject& manifest,
        const QString& media, const QString& schemaPath) {
    const QString connection = QStringLiteral("migration-") + QUuid::createUuid().toString();
    QString failure;
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
        db.setDatabaseName(path);
        try {
            check(db.open(), "Cannot open imported database");
            QSqlQuery query(db);
            check(query.exec("PRAGMA quick_check") && query.next() && query.value(0) == "ok", "Source database integrity check failed");
            QFile schema(schemaPath);
            QDomDocument document;
            check(schema.open(QIODevice::ReadOnly) && document.setContent(&schema), "Cannot read application schema");
            int target = 0;
            auto revisions = document.documentElement().childNodes();
            for (int i = 0; i < revisions.size(); ++i) {
                target = std::max(target, revisions.at(i).toElement().attribute("version").toInt());
            }
            SchemaManager manager(db);
            check(manager.readCurrentVersion() <= target && target > 0, "Source database is newer than this application");
            const auto result = manager.upgradeToSchemaVersion(target, schemaPath);
            check(result == SchemaManager::Result::CurrentVersion || result == SchemaManager::Result::UpgradeSucceeded, "Database schema upgrade failed");
            check(db.transaction(), "Cannot start migration transaction");
            check(query.exec("SELECT count(*) FROM library") && query.next() &&
                            query.value(0).toInt() == manifest.value("tracks").toArray().size(), "Track manifest does not match database");
            QSet<qint64> locations;
            for (const auto& value : manifest.value("tracks").toArray()) {
                auto track = value.toObject();
                query.prepare("SELECT t.id,t.location,t.filename FROM library l JOIN track_locations t ON t.id=l.location WHERE l.id=?");
                query.addBindValue(track.value("trackId").toInteger());
                check(query.exec() && query.next() && query.value(0).toLongLong() == track.value("locationId").toInteger(), "Source track relationship mismatch");
                auto original = query.value(1).toString();
                original.replace('\\', '/');
                check(original == track.value("original").toString(), "Source track path mismatch");
                const auto filename = query.value(2).toString();
                validateMigrationPath(filename);
                const auto locationId = track.value("locationId").toInteger();
                if (locations.contains(locationId)) {
                    continue;
                }
                locations.insert(locationId);
                const auto audio = track.value("audioPath").toString();
                const QString finalPath = audio.isEmpty()
                        ? QString(media + "/Missing/location-" + QString::number(locationId) + '/' + filename)
                        : QString(media + '/' + audio);
                query.prepare("UPDATE track_locations SET location=?, directory=?, filename=?, filesize=CASE WHEN ? THEN filesize ELSE ? END, fs_deleted=CASE WHEN ? THEN 1 ELSE fs_deleted END, needs_verification=1 WHERE id=?");
                query.addBindValue(finalPath);
                query.addBindValue(QFileInfo(finalPath).absolutePath());
                query.addBindValue(QFileInfo(finalPath).fileName());
                query.addBindValue(audio.isEmpty());
                query.addBindValue(QFileInfo(finalPath).size());
                query.addBindValue(audio.isEmpty());
                query.addBindValue(locationId);
                check(query.exec() && query.numRowsAffected() == 1, "Cannot relocate track");
            }
            check(query.exec("DELETE FROM directories"), "Cannot replace Windows music roots");
            query.prepare("INSERT INTO directories(directory) VALUES(?)");
            query.addBindValue(QString(media + "/audio"));
            check(query.exec(), "Cannot register imported music root");
            check(query.exec("UPDATE library SET coverart_location='' WHERE coverart_location IS NOT NULL AND coverart_location!=''"), "Cannot clear stale cover paths");
            for (const auto& value : manifest.value("covers").toArray()) {
                const auto cover = value.toObject();
                query.prepare("UPDATE library SET coverart_location=? WHERE id=?");
                query.addBindValue(QString(media + '/' + cover.value("coverPath").toString()));
                query.addBindValue(cover.value("trackId").toInteger());
                check(query.exec() && query.numRowsAffected() == 1, "Cannot relocate artwork");
            }
            check(db.commit(), "Cannot commit relocated database");
            check(query.exec("PRAGMA quick_check") && query.next() && query.value(0) == "ok", "Imported database integrity check failed");
            check(query.exec("SELECT count(*) FROM library l LEFT JOIN track_locations t ON t.id=l.location WHERE t.id IS NULL") && query.next() && query.value(0).toInt() == 0, "Imported track relationships invalid");
            query.exec("PRAGMA wal_checkpoint(TRUNCATE)");
            db.close();
        } catch (const std::exception& error) {
            db.rollback();
            db.close();
            failure = QString::fromUtf8(error.what());
        }
    }
    QSqlDatabase::removeDatabase(connection);
    if (!failure.isEmpty()) {
        throw std::runtime_error(failure.toStdString());
    }
}

struct ImportProgress {
    std::atomic_bool cancelled{false};
    std::atomic<quint64> bytes{0};
};

void copyConfigDirectory(const QString& source, const QString& destination,
        const std::shared_ptr<ImportProgress>& progress) {
    check(QDir().mkpath(destination), "Cannot create profile directory");
    for (const auto& entry : QDir(source).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString target = destination + '/' + entry.fileName();
        if (entry.isDir()) {
            copyConfigDirectory(entry.absoluteFilePath(), target, progress);
            continue;
        }
        QFile input(entry.absoluteFilePath());
        QFile output(target);
        check(input.open(QIODevice::ReadOnly) && output.open(QIODevice::WriteOnly | QIODevice::NewOnly), "Cannot copy profile resource");
        while (!input.atEnd()) {
            check(!progress->cancelled.load(), "Import cancelled");
            const auto bytes = input.read(1024 * 1024);
            check(!bytes.isEmpty() && output.write(bytes) == bytes.size(), "Cannot copy profile resource");
        }
        check(output.flush(), "Cannot flush profile resource");
    }
}

QString importPackage(const QString& package, const QString& root, const QString& music,
        const QString& originalSettings, const QString& schema,
        const std::shared_ptr<ImportProgress>& progress) {
    QString staging;
    QString profile;
    QString media;
    bool ready = false;
    bool ownsStaging = false;
    bool ownsProfile = false;
    bool ownsMedia = false;
    try {
        MigrationArchive archive(package);
        const auto manifest = archive.manifest();
        const auto id = QUuid::createUuid().toString(QUuid::Id128);
        const auto publicRoot = QFileInfo(QDir::cleanPath(music)).absolutePath();
        staging = publicRoot + "/.migration-" + id;
        profile = root + "/profiles/" + id;
        media = music + "/Imported/" + id;
        check(QDir().mkpath(publicRoot) && QDir().mkpath(root + "/profiles"), "Music folder is not authorized or writable");
        QStorageInfo publicStorage(publicRoot);
        QStorageInfo privateStorage(root);
        qint64 configBytes = 0;
        for (const auto& value : manifest.value("files").toArray()) {
            const auto file = value.toObject();
            if (file.value("path").toString().startsWith("config/")) {
                configBytes += file.value("size").toInteger();
            }
        }
        check(publicStorage.isValid() && publicStorage.bytesAvailable() > qint64(archive.unpackedSize()) + 64 * 1024 * 1024 &&
                        privateStorage.bytesAvailable() > configBytes + 64 * 1024 * 1024, "Not enough storage for import");
        check(!QFileInfo::exists(staging) && !QFileInfo::exists(profile) && !QFileInfo::exists(media), "Import directory already exists");
        check(QDir().mkpath(staging), "Cannot create import staging folder");
        ownsStaging = true;
        archive.extract(staging, progress->cancelled, [progress](quint64 bytes) { progress->bytes += bytes; });
        check(!progress->cancelled.load(), "Import cancelled");
        check(QDir().mkpath(media), "Cannot create imported music folder");
        ownsMedia = true;
        for (const auto& name : {QStringLiteral("audio"), QStringLiteral("artwork")}) {
            if (QFileInfo::exists(staging + '/' + name)) {
                check(QDir().rename(staging + '/' + name, media + '/' + name), "Cannot commit imported media");
            }
        }
        check(QDir().mkpath(profile), "Cannot create imported profile");
        ownsProfile = true;
        const QString config = staging + "/config/Mixxx";
        copyConfigDirectory(config, profile, progress);
        if (QFileInfo::exists(profile + "/mixxxdb.sqlite")) {
            migrateDatabase(profile + "/mixxxdb.sqlite", manifest, media, schema);
        }
        if (QFileInfo::exists(originalSettings + "/soundconfig.xml")) {
            QFile input(originalSettings + "/soundconfig.xml");
            QSaveFile output(profile + "/soundconfig.xml");
            check(input.open(QIODevice::ReadOnly) && input.size() <= 1024 * 1024 &&
                            output.open(QIODevice::WriteOnly), "Cannot open device audio configuration");
            const auto bytes = input.readAll();
            check(input.error() == QFile::NoError && output.write(bytes) == bytes.size() &&
                            output.commit(), "Cannot retain device audio configuration");
        }
        QFile samplers(profile + "/samplers.xml");
        if (samplers.exists()) {
            QDomDocument bank;
            check(samplers.open(QIODevice::ReadOnly) && bank.setContent(&samplers), "Cannot read sampler bank");
            samplers.close();
            auto samplerNodes = bank.elementsByTagName("sampler");
            for (int i = 0; i < samplerNodes.size(); ++i) {
                auto slot = samplerNodes.at(i).toElement();
                const auto location = slot.attribute("location");
                if (location.startsWith("@transfer/")) {
                    const auto relative = location.mid(10);
                    validateMigrationPath(relative);
                    check(relative.startsWith("audio/") && QFileInfo::exists(media + '/' + relative), "Sampler audio missing");
                    slot.setAttribute("location", media + '/' + relative);
                } else if (!location.isEmpty()) {
                    slot.setAttribute("location", QString());
                }
            }
            QSaveFile output(samplers.fileName());
            const auto bytes = bank.toByteArray();
            check(output.open(QIODevice::WriteOnly) && output.write(bytes) == bytes.size() && output.commit(), "Cannot relocate sampler bank");
        }
        check(!progress->cancelled.load(), "Import cancelled");
        const QJsonObject report{{"id", id}, {"state", "ready"},
                {"date", QDateTime::currentDateTime().toString(Qt::ISODate)},
                {"included", manifest.value("includedTracks")}, {"missing", manifest.value("missingTracks")},
                {"packageId", manifest.value("packageId")}};
        writeJson(profile + "/import.json", report);
        ready = true;
        try {
            QDir().mkpath(publicRoot + "/logs");
            writeJson(publicRoot + "/logs/import-" + id + ".json", report);
        } catch (const std::exception& error) {
            qWarning() << "OHOS import report export failed:" << error.what();
        }
        QDir(staging).removeRecursively();
        return id;
    } catch (const std::exception& error) {
        if (!ready) {
            if (ownsProfile) {
                QDir(profile).removeRecursively();
            }
            if (ownsMedia) {
                QDir(media).removeRecursively();
            }
        }
        if (ownsStaging) {
            QDir(staging).removeRecursively();
        }
        return QStringLiteral("ERROR: ") + QString::fromUtf8(error.what());
    }
}
}

void installMigrationActions(QMainWindow* window, const UserSettingsPointer& settings) {
    auto* menu = window->findChild<QMenu*>(QStringLiteral("MixxxOptionsMenu"));
    if (!menu) {
        return;
    }
    const auto root = filesRoot(settings);
    menu->addSeparator();
    auto* import = menu->addAction(QStringLiteral("导入 Windows 迁移包…"));
    QObject::connect(import, &QAction::triggered, window, [window, settings, root, import] {
        const auto music = CmdlineArgs::Instance().getMediaPath();
        const auto package = QFileDialog::getOpenFileName(window, QStringLiteral("选择 PC 迁移助手生成的包"), QFileInfo(music).absolutePath(), QStringLiteral("Mixxx 迁移包 (*.zip)"));
        if (package.isEmpty()) {
            return;
        }
        try {
            MigrationArchive archive(package);
            const auto manifest = archive.manifest();
            const auto text = QStringLiteral("将导入为新配置档案。\n携带音乐 %1 首，未携带 %2 首。\n所需空间约 %3 MB。\n现有档案保留，可从菜单切回。\n\n是否开始导入？")
                    .arg(manifest.value("includedTracks").toInt()).arg(manifest.value("missingTracks").toInt()).arg((archive.unpackedSize() + 1024 * 1024 - 1) / (1024 * 1024));
            if (QMessageBox::question(window, QStringLiteral("迁移预览"), text) != QMessageBox::Yes) {
                return;
            }
            settings->save();
            import->setEnabled(false);
            auto progress = std::make_shared<ImportProgress>();
            auto* dialog = new QProgressDialog(QStringLiteral("正在校验并导入…"), QStringLiteral("取消"), 0, 100, window);
            dialog->setWindowModality(Qt::WindowModal);
            dialog->setMinimumDuration(0);
            dialog->setAutoClose(false);
            auto* timer = new QTimer(dialog);
            QObject::connect(timer, &QTimer::timeout, dialog, [dialog, progress, size = archive.unpackedSize()] {
                dialog->setValue(std::min(99, int(100.0 * progress->bytes.load() / std::max<quint64>(1, size))));
            });
            QObject::connect(dialog, &QProgressDialog::canceled, dialog, [progress] { progress->cancelled = true; });
            timer->start(200);
            auto* watcher = new QFutureWatcher<QString>(window);
            QObject::connect(watcher, &QFutureWatcher<QString>::finished, window, [watcher, dialog, timer, import, window, root] {
                const auto result = watcher->result();
                timer->stop();
                dialog->reset();
                delete dialog;
                watcher->deleteLater();
                import->setEnabled(true);
                QTimer::singleShot(0, window, [result, window, root] {
                    if (result.startsWith("ERROR:")) {
                        QMessageBox::warning(window, QStringLiteral("导入未完成"), result.mid(7));
                        return;
                    }
                    try {
                        activateProfile(root, result);
                        QMessageBox::information(window, QStringLiteral("导入完成"), QStringLiteral("文件与曲库已通过校验。请关闭并重新打开应用，加载新档案。\n原档案可从“配置档案”菜单切回。"));
                    } catch (const std::exception& error) {
                        QMessageBox::warning(window, QStringLiteral("档案已保存"), QString::fromUtf8(error.what()));
                    }
                });
            });
            watcher->setFuture(QtConcurrent::run([package, root, music, path = settings->getSettingsPath(), schema = QDir(settings->getResourcePath()).filePath("schema.xml"), progress] {
                return importPackage(package, root, music, path, schema, progress);
            }));
        } catch (const std::exception& error) {
            QMessageBox::warning(window, QStringLiteral("无法读取迁移包"), QString::fromUtf8(error.what()));
        }
    });
    auto* profiles = menu->addAction(QStringLiteral("配置档案／还原…"));
    QObject::connect(profiles, &QAction::triggered, window, [window, settings, root] {
        QStringList names{QStringLiteral("本机原始档案")};
        QStringList ids{QString()};
        for (const auto& dir : QDir(root + "/profiles").entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time)) {
            QFile file(root + "/profiles/" + dir + "/import.json");
            if (!file.open(QIODevice::ReadOnly)) {
                continue;
            }
            const auto report = QJsonDocument::fromJson(file.readAll()).object();
            if (report.value("state") == "ready") {
                names.append(QStringLiteral("%1 · %2 首 · %3").arg(report.value("date").toString(), QString::number(report.value("included").toInt()), dir.left(8)));
                ids.append(dir);
            }
        }
        bool accepted = false;
        const auto selection = QInputDialog::getItem(window, QStringLiteral("选择下次启动使用的档案"), QStringLiteral("每个档案独立保存设置和曲库"), names, 0, false, &accepted);
        if (accepted) {
            try {
                settings->save();
                activateProfile(root, ids.at(names.indexOf(selection)));
                QMessageBox::information(window, QStringLiteral("档案已选择"), QStringLiteral("请关闭并重新打开应用后生效。"));
            } catch (const std::exception& error) {
                QMessageBox::warning(window, QStringLiteral("无法选择档案"), QString::fromUtf8(error.what()));
            }
        }
    });
}
}
