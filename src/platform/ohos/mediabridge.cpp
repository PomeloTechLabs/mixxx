#include "mediabridge.h"

#include <QMetaObject>
#include <mutex>

#include <napi/native_api.h>

namespace {
std::mutex s_mutex;
QByteArray s_state = R"({"ready":false,"playing":false,"assetId":"","title":"","artist":"","album":"","duration":0,"position":0})";
QObject* s_receiver = nullptr;
std::function<void(const QString&, double)> s_handler;
QObject* s_windowReceiver = nullptr;
std::function<void(int)> s_windowHandler;
int s_keyboardHeight = 0;
QByteArray s_windowState = "{}";

void postKeyboardHeight() {
    if (s_windowReceiver) {
        auto* receiver = s_windowReceiver;
        const int height = s_keyboardHeight;
        QMetaObject::invokeMethod(receiver, [receiver, height] {
            std::function<void(int)> handler;
            {
                std::lock_guard<std::mutex> lock(s_mutex);
                if (s_windowReceiver == receiver) {
                    handler = s_windowHandler;
                }
            }
            if (handler) {
                handler(height);
            }
        }, Qt::QueuedConnection);
    }
}

napi_value setKeyboardHeight(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    int32_t height = 0;
    if (argc == 1 && napi_get_value_int32(env, args[0], &height) == napi_ok &&
            height >= 0 && height <= 16384) {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_keyboardHeight = height;
        postKeyboardHeight();
    }
    napi_value result;
    napi_get_undefined(env, &result);
    return result;
}

napi_value readState(napi_env env, napi_callback_info) {
    std::lock_guard<std::mutex> lock(s_mutex);
    napi_value result;
    napi_create_string_utf8(env, s_state.constData(), s_state.size(), &result);
    return result;
}

napi_value readWindowState(napi_env env, napi_callback_info) {
    std::lock_guard<std::mutex> lock(s_mutex);
    napi_value result;
    napi_create_string_utf8(env, s_windowState.constData(), s_windowState.size(), &result);
    return result;
}

napi_value sendCommand(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    char name[32] = {};
    size_t length = 0;
    double value = 0;
    bool accepted = false;
    if (argc > 0 && napi_get_value_string_utf8(env, args[0], name, sizeof(name), &length) == napi_ok) {
        const QString command = QString::fromUtf8(name, static_cast<int>(length));
        if (argc > 1) {
            napi_get_value_double(env, args[1], &value);
        }
        if (command == "play" || command == "pause" || command == "stop" ||
                command == "next" || command == "previous" || command == "seek") {
            std::lock_guard<std::mutex> lock(s_mutex);
            if (s_receiver) {
                accepted = QMetaObject::invokeMethod(s_receiver, [command, value] {
                    std::function<void(const QString&, double)> handler;
                    {
                        std::lock_guard<std::mutex> lock(s_mutex);
                        handler = s_handler;
                    }
                    if (handler) {
                        handler(command, value);
                    }
                }, Qt::QueuedConnection);
            }
        }
    }
    napi_value result;
    napi_get_boolean(env, accepted, &result);
    return result;
}

napi_value initialize(napi_env env, napi_value exports) {
    napi_property_descriptor properties[] = {
            {"readState", nullptr, readState, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"readWindowState", nullptr, readWindowState, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"sendCommand", nullptr, sendCommand, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setKeyboardHeight", nullptr, setKeyboardHeight, nullptr, nullptr, nullptr, napi_default, nullptr}};
    napi_define_properties(env, exports, 4, properties);
    return exports;
}

napi_module s_module = {1, 0, nullptr, initialize, "mixxxohosmedia", nullptr, {0}};
__attribute__((constructor)) void registerModule() {
    napi_module_register(&s_module);
}
}

namespace mixxx::ohos {
void attachWindowBridge(QObject* receiver, std::function<void(int)> handler) {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_windowReceiver = receiver;
    s_windowHandler = std::move(handler);
    postKeyboardHeight();
}

void detachWindowBridge(QObject* receiver) {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_windowReceiver == receiver) {
        s_windowReceiver = nullptr;
        s_windowHandler = {};
        s_windowState = "{}";
    }
}

void publishWindowState(const QByteArray& state) {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_windowState = state;
}

void attachMediaBridge(QObject* receiver, std::function<void(const QString&, double)> handler) {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_receiver = receiver;
    s_handler = std::move(handler);
}

void detachMediaBridge(QObject* receiver) {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_receiver == receiver) {
        s_receiver = nullptr;
        s_handler = {};
        s_state = R"({"ready":false,"playing":false,"assetId":"","title":"","artist":"","album":"","duration":0,"position":0})";
    }
}

void publishMediaState(const QByteArray& state) {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_state = state;
}
}
