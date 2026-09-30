#include "widget/wwidget.h"

#include <QTouchEvent>
#ifdef MIXXX_OS_OHOS
#include <QApplication>
#include <QDrag>
#include <QWindow>
#include "platform/ohos/touchcompat.h"
#endif

#include "control/controlproxy.h"
#include "moc_wwidget.cpp"
#include "util/assert.h"

#ifdef MIXXX_OS_OHOS
namespace {
class OhosTouchMouseEvent final : public QMouseEvent {
  public:
    OhosTouchMouseEvent(QEvent::Type type, const QPointF& position,
            const QPointF& global, Qt::MouseButton button, Qt::MouseButtons buttons,
            const QTouchEvent& touch)
            : QMouseEvent(type, position, position, global, button, buttons,
                      touch.modifiers(), Qt::MouseEventSynthesizedByApplication,
                      mixxx::ohos::touchConversionMouseDevice()) {
        m_dev = touch.pointingDevice();
        if (!touch.points().isEmpty()) {
            m_points = {touch.points().first()};
        }
        m_timeStamp = touch.timestamp();
    }
};
}
#endif

WWidget::WWidget(QWidget* parent, Qt::WindowFlags flags)
        : QWidget(parent, flags),
          WBaseWidget(this),
          m_activeTouchButton(Qt::NoButton),
          m_scaleFactor(1.0) {
    m_pTouchShift = new ControlProxy("[Controls]", "touch_shift");
    setAttribute(Qt::WA_StaticContents);
    // Touch events are disabled on macOS to work around the issue that Mac
    // trackpad events are not processed correctly with Qt 6. Since these events
    // aren't needed anyway, we can safely disable them. Once upstream (Qt)
    // fixes the issue, the `#ifndef` can be removed to re-enable touch events.
    // For details on both the issue and the fix, see
    // - https://bugreports.qt.io/browse/QTBUG-103935?focusedId=739905#comment-739905
    // - https://github.com/mixxxdj/mixxx/issues/11869
    // - https://github.com/mixxxdj/mixxx/pull/11870
#ifndef __APPLE__
    setAttribute(Qt::WA_AcceptTouchEvents);
#endif
    setFocusPolicy(Qt::ClickFocus);
}

WWidget::~WWidget() {
    delete m_pTouchShift;
}

bool WWidget::touchIsRightButton() {
    return m_pTouchShift->toBool();
}

bool WWidget::event(QEvent* e) {
    if (e->type() == QEvent::ToolTip) {
        updateTooltip();
    } if (e->type() == QEvent::FontChange) {
        const QFont& fonti = font();
        // Change the new font on the fly by casting away its constancy
        // using setFont() here, would results into a recursive loop
        // resetting the font to the original css values.
        // Only scale pixel size fonts, point size fonts are scaled by the OS
        if (fonti.pixelSize() > 0) {
            const_cast<QFont&>(fonti).setPixelSize(
                    static_cast<int>(fonti.pixelSize() * m_scaleFactor));
        }
    } else if (isEnabled()) {
        // With Qt6 on Windows this touch -> mouse translation is apparently not
        // required anymore, QMouseEvents are received correctly.
        // If enabled we receive both QTouch and QMouse events, see
        // https://github.com/mixxxdj/mixxx/issues/15546
        // TODO Test with other OS, maybe we can drop it entirely.
#ifndef __WINDOWS__
        switch(e->type()) {
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd:
#ifdef MIXXX_OS_OHOS
        case QEvent::TouchCancel:
#endif
        {
            QTouchEvent* touchEvent = dynamic_cast<QTouchEvent*>(e);
            if (touchEvent == nullptr
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
                    || touchEvent->device() == nullptr ||
                    touchEvent->device()->type() != QTouchDevice::TouchScreen
#endif
            ) {
                break;
            }

            // fake a mouse event!
            QEvent::Type eventType = QEvent::None;
            switch (touchEvent->type()) {
            case QEvent::TouchBegin:
                eventType = QEvent::MouseButtonPress;
                if (touchIsRightButton()) {
                    // touch is right click
                    m_activeTouchButton = Qt::RightButton;
                } else {
                    m_activeTouchButton = Qt::LeftButton;
                }
                break;
            case QEvent::TouchUpdate:
                eventType = QEvent::MouseMove;
                break;
            case QEvent::TouchEnd:
#ifdef MIXXX_OS_OHOS
            case QEvent::TouchCancel:
#endif
                eventType = QEvent::MouseButtonRelease;
                break;
            default:
                DEBUG_ASSERT(false);
                break;
            }

#ifdef MIXXX_OS_OHOS
            if (e->type() == QEvent::TouchCancel && inherits("WHotcueButton")) {
                setProperty("ohosTouchEditMoved", true);
                setProperty("ohosTouchEditing", false);
            }
            if (e->type() == QEvent::TouchCancel &&
                    qApp->property("ohosTouchWidgetDrag").toBool()) {
                QDrag::cancel();
                setProperty("ohosTouchEditing", false);
                m_activeTouchButton = Qt::NoButton;
                touchEvent->accept();
                return true;
            }
            if (touchEvent->points().isEmpty() && e->type() != QEvent::TouchCancel) {
                break;
            }
            const auto position = touchEvent->points().isEmpty()
                    ? property("ohosLastTouchPosition").toPointF()
                    : touchEvent->points().first().position();
            const auto global = touchEvent->points().isEmpty()
                    ? mapToGlobal(position)
                    : touchEvent->points().first().globalPosition();
            setProperty("ohosLastTouchPosition", position);
            const bool resettable = inherits("WKnob") || inherits("WKnobComposed") ||
                    inherits("WSliderComposed");
            if (resettable && eventType == QEvent::MouseButtonPress && m_activeTouchButton == Qt::LeftButton) {
                const auto previous = property("ohosTouchTapTimestamp").toULongLong();
                if (previous > 0 && touchEvent->timestamp() >= previous &&
                        touchEvent->timestamp() - previous <= quint64(QApplication::doubleClickInterval()) &&
                        (global - property("ohosTouchTapPosition").toPointF()).manhattanLength() <= QApplication::startDragDistance()) {
                    eventType = QEvent::MouseButtonDblClick;
                }
                setProperty("ohosTouchTapTimestamp", qulonglong(0));
            } else if (resettable && e->type() == QEvent::TouchEnd && !touchEvent->points().isEmpty()) {
                const auto& point = touchEvent->points().first();
                if (touchEvent->timestamp() - point.pressTimestamp() <= quint64(QApplication::doubleClickInterval()) &&
                        (point.globalPosition() - point.globalPressPosition()).manhattanLength() <= QApplication::startDragDistance()) {
                    setProperty("ohosTouchTapTimestamp", qulonglong(touchEvent->timestamp()));
                    setProperty("ohosTouchTapPosition", global);
                }
            } else if (resettable && e->type() == QEvent::TouchCancel) {
                setProperty("ohosTouchTapTimestamp", qulonglong(0));
            }
            const bool released = eventType == QEvent::MouseButtonRelease;
            OhosTouchMouseEvent mouseEvent(eventType, position, global,
                    eventType == QEvent::MouseMove ? Qt::NoButton : m_activeTouchButton,
                    released ? Qt::NoButton : m_activeTouchButton,
                    *touchEvent);
            if (eventType != QEvent::MouseMove && inherits("WHotcueButton")) {
                qInfo() << "OHOS hotcue touch" << eventType << touchEvent->pointingDevice()->type()
                        << qApp->property("ohosTouchHotcueEdit").toBool();
            }
            if (qApp->property("ohosTouchWidgetDrag").toBool() && window()->windowHandle()) {
                if (released) {
                    OhosTouchMouseEvent finalMove(QEvent::MouseMove, position, global,
                            Qt::NoButton, m_activeTouchButton, *touchEvent);
                    QCoreApplication::sendEvent(window()->windowHandle(), &finalMove);
                }
                QCoreApplication::sendEvent(window()->windowHandle(), &mouseEvent);
            } else {
                QWidget::event(&mouseEvent);
            }
            if (released) {
                m_activeTouchButton = Qt::NoButton;
            }
            touchEvent->setAccepted(true);
            return true;
#else
            const QTouchEvent::TouchPoint& touchPoint =
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
                    touchEvent->points()
#else
                    touchEvent->touchPoints()
#endif
                            .first();
            QMouseEvent mouseEvent(eventType,
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
                    touchPoint.position(),
                    touchPoint.position(),
                    touchPoint.globalPosition(),
#else
                    touchPoint.pos(),
                    touchPoint.pos(),
                    touchPoint.screenPos(),
#endif
                    m_activeTouchButton, // Button that causes the event
                    Qt::NoButton,        // Not used, so no need to fake a proper value.
                    touchEvent->modifiers(),
                    Qt::MouseEventSynthesizedByApplication);

            return QWidget::event(&mouseEvent);
#endif
        }
        default:
            break;
        }
#endif
    }

    return QWidget::event(e);
}
