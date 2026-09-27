// Minimal Qt boot for the Mixxx OHOS HAP shell (TASK-003).
// dlopen'ed by Qt's ohos QPA (qohosjsmain.cpp) which calls main() on a
// dedicated thread. Validates the HAP -> Qt -> QML chain before the real
// libmixxx.so CoreServices path (TASK-004).
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSurfaceFormat>
#include <QtDebug>

int main(int argc, char *argv[])
{
    qputenv("QT_LOGGING_RULES", "*.debug=false;qt.qml.*=false");

    QGuiApplication app(argc, argv);

    QSurfaceFormat format;
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    QSurfaceFormat::setDefaultFormat(format);

    QQmlApplicationEngine engine;
    const QUrl url(QStringLiteral("qrc:/qt/qml/mixxx/ohos/boot/Main.qml"));
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreated, &app,
        [url](QObject *obj, const QUrl &objUrl) {
            if (!obj)
                qFatal("Mixxx boot: failed to load %s", qPrintable(objUrl.toString()));
        },
        Qt::QueuedConnection);
    engine.load(url);

    qDebug("Mixxx boot: QML engine loaded, entering event loop");
    return app.exec();
}
