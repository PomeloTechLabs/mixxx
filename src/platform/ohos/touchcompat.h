#pragma once

#include <QPointingDevice>

namespace mixxx::ohos {

inline const QPointingDevice* touchConversionMouseDevice() {
    static const QPointingDevice device(QStringLiteral("Mixxx touch conversion"), -17,
            QInputDevice::DeviceType::Mouse, QPointingDevice::PointerType::Generic,
            QInputDevice::Capability::Position, 1, 3);
    return &device;
}

}
