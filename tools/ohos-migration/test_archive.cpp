#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTextStream>
#include <atomic>
#include "platform/ohos/migrationarchive.h"

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    try {
        mixxx::ohos::MigrationArchive archive(application.arguments().at(1));
        if (application.arguments().size() > 2) {
            std::atomic_bool cancelled{application.arguments().contains("--cancel")};
            archive.extract(application.arguments().at(2), cancelled, [](quint64) {});
        }
        QTextStream(stdout) << "PASS files=" << archive.manifest().value("files").toArray().size()
                            << " bytes=" << archive.unpackedSize() << '\n';
        return 0;
    } catch (const std::exception& error) {
        QTextStream(stderr) << error.what() << '\n';
        return 1;
    }
}
