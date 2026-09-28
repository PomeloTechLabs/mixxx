#pragma once

#include <QObject>
#include <QString>
#include <functional>

namespace mixxx::ohos {
Q_DECL_EXPORT void attachMediaBridge(QObject* receiver,
        std::function<void(const QString&, double)> handler);
Q_DECL_EXPORT void detachMediaBridge(QObject* receiver);
Q_DECL_EXPORT void publishMediaState(const QByteArray& state);
Q_DECL_EXPORT void attachWindowBridge(QObject* receiver, std::function<void(int)> handler);
Q_DECL_EXPORT void detachWindowBridge(QObject* receiver);
Q_DECL_EXPORT void publishWindowState(const QByteArray& state);
}
