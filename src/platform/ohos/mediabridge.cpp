#include "mediabridge.h"

#include <QMetaObject>
#include <cstring>
#include <mutex>

#include <napi/native_api.h>

namespace {
std::mutex s_mutex;
const QByteArray kEmptyState = R"({"ready":false,"playing":false,"loading":false,"assetId":"","title":"","artist":"","album":"","artworkKey":"","artworkAvailable":false,"duration":0,"position":0})";
QByteArray s_state = kEmptyState;
QString s_artworkKey;
QByteArray s_artwork;
QObject* s_receiver = nullptr;
std::function<void(const QString&, double)> s_handler;
QObject* s_windowReceiver = nullptr;
std::function<void(int)> s_windowHandler;
int s_keyboardHeight = 0;
QByteArray s_windowState = "{}";
QObject* s_safeReceiver = nullptr;
std::function<void(const std::array<int, 4>&)> s_safeHandler;
std::array<int, 4> s_safeArea{};

void postSafeArea() {
    if (s_safeReceiver) {
        auto* receiver = s_safeReceiver;
        const auto area = s_safeArea;
        QMetaObject::invokeMethod(receiver, [receiver, area] {
            std::function<void(const std::array<int, 4>&)> handler;
            {
                std::lock_guard<std::mutex> lock(s_mutex);
                if (s_safeReceiver == receiver) {
                    handler = s_safeHandler;
                }
            }
            if (handler) {
                handler(area);
            }
        }, Qt::QueuedConnection);
    }
}

napi_value setSafeArea(napi_env env, napi_callback_info info) {
    size_t argc = 4;
    napi_value args[4];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    std::array<int, 4> area{};
    bool valid = argc == 4;
    for (size_t i = 0; valid && i < 4; ++i) {
        int32_t value = 0;
        valid = napi_get_value_int32(env, args[i], &value) == napi_ok && value >= 0 && value <= 4096;
        area[i] = value;
    }
    if (valid) {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (s_safeArea != area) {
            s_safeArea = area;
            postSafeArea();
        }
    }
    napi_value result;
    napi_get_undefined(env, &result);
    return result;
}

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

napi_value readArtwork(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    char key[32] = {};
    size_t length = 0;
    QByteArray artwork;
    if (argc == 1 &&
            napi_get_value_string_utf8(env, args[0], key, sizeof(key), &length) == napi_ok &&
            length < sizeof(key)) {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (s_artworkKey == QString::fromUtf8(key, static_cast<int>(length)) &&
                s_artwork.size() <= 2 * 1024 * 1024) {
            artwork = s_artwork;
        }
    }
    napi_value result;
    void* data = nullptr;
    if (napi_create_arraybuffer(env, artwork.size(), &data, &result) != napi_ok) {
        napi_throw_error(env, nullptr, "Unable to allocate media artwork");
        return nullptr;
    }
    if (!artwork.isEmpty()) {
        std::memcpy(data, artwork.constData(), artwork.size());
    }
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
            {"readArtwork", nullptr, readArtwork, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"readWindowState", nullptr, readWindowState, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"sendCommand", nullptr, sendCommand, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setKeyboardHeight", nullptr, setKeyboardHeight, nullptr, nullptr, nullptr, napi_default, nullptr}};
    napi_define_properties(env, exports, 5, properties);
    napi_property_descriptor safeProperty = {"setSafeArea", nullptr, setSafeArea, nullptr, nullptr, nullptr, napi_default, nullptr};
    napi_define_properties(env, exports, 1, &safeProperty);
    return exports;
}

napi_module s_module = {1, 0, nullptr, initialize, "mixxxohosmedia", nullptr, {0}};
__attribute__((constructor)) void registerModule() {
    napi_module_register(&s_module);
}
}

namespace mixxx::ohos {
void attachSafeAreaBridge(QObject* receiver, std::function<void(const std::array<int, 4>&)> handler) {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_safeReceiver = receiver;
    s_safeHandler = std::move(handler);
    postSafeArea();
}

void detachSafeAreaBridge(QObject* receiver) {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_safeReceiver == receiver) {
        s_safeReceiver = nullptr;
        s_safeHandler = {};
    }
}

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
        s_state = kEmptyState;
        s_artworkKey.clear();
        s_artwork.clear();
    }
}

void publishMediaState(const QByteArray& state,
        const QString& artworkKey, const QByteArray& artwork) {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_state = state;
    s_artworkKey = artworkKey;
    s_artwork = artwork;
}
}
