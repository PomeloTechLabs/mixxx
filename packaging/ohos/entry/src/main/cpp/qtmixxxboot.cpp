// Minimal Qt boot for the Mixxx OHOS HAP shell (TASK-003).
// dlopen'ed by Qt's ohos QPA (qohosjsmain.cpp) which calls main() on a
// dedicated thread. Validates the HAP -> Qt -> QML chain before the real
// libmixxx.so CoreServices path (TASK-004).
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTimer>
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
    // The QML module lives at qrc:/qt/qml/<uri> with the modern resource
    // prefix policy (QTP0001 NEW), or at qrc:/<uri> with the legacy default.
    QUrl url(QStringLiteral("qrc:/qt/qml/mixxx/ohos/boot/Main.qml"));
    engine.load(url);
    if (engine.rootObjects().isEmpty()) {
        url = QUrl(QStringLiteral("qrc:/mixxx/ohos/boot/Main.qml"));
        engine.load(url);
    }
    if (engine.rootObjects().isEmpty())
        qFatal("Mixxx boot: failed to load Main.qml from either resource prefix");

    // Qt's OHOS QPA maps the first QWindow created by the app onto the
    // ability's main window, and that requires the ArkUI window stage to
    // exist already. Creating the window straight from main() races with
    // QAbility.onWindowStageCreate and aborts inside
    // makeWindowProxyDataForExistingMainWindowInJsThread, so keep the QML
    // window hidden until the stage is up.
    auto *rootWindow = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    QTimer::singleShot(1500, &app, [rootWindow]() {
        if (rootWindow) {
            rootWindow->setVisible(true);
            qDebug("Mixxx boot: window shown (Qt on OHOS)");
        }
    });

    qDebug("Mixxx boot: QML engine loaded, entering event loop");
    return app.exec();
}
