#include "windowadapter.h"

#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QActionGroup>
#include <QDebug>
#include <QDialog>
#include <QHeaderView>
#include <QInputMethod>
#include <QInputDialog>
#include <QFileDialog>
#include <QMessageBox>
#include <QProgressDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QMouseEvent>
#include <QPalette>
#include <QScroller>
#include <QScrollArea>
#include <QScrollerProperties>
#include <QTouchEvent>
#include <QVBoxLayout>
#include <QWindow>
#include <QtGui/private/qhighdpiscaling_p.h>
#include <QtGui/private/qwindow_p.h>
#include <qpa/qplatformwindow.h>
#include <qpa/qwindowsysteminterface.h>
#include <algorithm>
#include <cmath>

#include "platform/ohos/mediabridge.h"
#include "util/cmdlineargs.h"
#include "util/logging.h"

namespace mixxx::ohos {

WindowAdapter::WindowAdapter(QMainWindow* window, UserSettingsPointer settings)
        : QObject(window),
          m_window(window),
          m_settings(std::move(settings)),
          m_host(new QWidget(window)),
          m_sizeNotice(new QLabel(m_host)) {
    m_host->setObjectName(QStringLiteral("OhosOriginalSkinHost"));
    m_sizeNotice->setText(QStringLiteral("窗口太小，无法完整使用 Mixxx。\n请放大窗口或切换到全屏横屏。"));
    m_sizeNotice->setAlignment(Qt::AlignCenter);
    m_sizeNotice->setWordWrap(true);
    m_sizeNotice->setStyleSheet(QStringLiteral("QLabel { font-size: 28px; color: #eeeeee; }"));
    m_sizeNotice->hide();
    m_window->setCentralWidget(m_host);
    m_window->setMinimumSize(0, 0);
    m_displayMode = std::clamp(m_settings->getValue<int>(ConfigKey("[OHOS]", "DisplayMode"), 0), 0, 2);
    qApp->setProperty("ohosTouchKnobSensitivity", m_settings->getValue<double>(ConfigKey("[OHOS]", "TouchKnobSensitivity"), 0.6));
    if (auto* options = m_window->findChild<QMenu*>(QStringLiteral("MixxxOptionsMenu"))) {
        auto* display = options->addMenu(QStringLiteral("屏幕边缘与全屏"));
        auto* group = new QActionGroup(display);
        const QStringList names{QStringLiteral("自动避让挖孔和圆角"), QStringLiteral("完全全屏"), QStringLiteral("保守留边（更大操作空间）")};
        for (int i = 0; i < names.size(); ++i) {
            auto* action = display->addAction(names[i]);
            action->setCheckable(true);
            action->setChecked(m_displayMode == i);
            group->addAction(action);
            connect(action, &QAction::triggered, this, [this, i] {
                m_displayMode = i;
                m_settings->setValue(ConfigKey("[OHOS]", "DisplayMode"), i);
                m_settings->save();
                fitSkin();
                for (auto* widget : QApplication::topLevelWidgets()) {
                    if (auto* dialog = qobject_cast<QDialog*>(widget); dialog && dialog->isVisible()) {
                        scheduleDialogFit(dialog);
                    }
                }
            });
        }
        auto* touch = options->addMenu(QStringLiteral("触摸操作"));
        auto* sensitivity = touch->addMenu(QStringLiteral("旋钮滑动速度"));
        auto* speeds = new QActionGroup(sensitivity);
        for (const auto& item : {std::pair<QString, double>{QStringLiteral("精细"), 0.35}, {QStringLiteral("标准"), 0.6}, {QStringLiteral("快速"), 1.0}}) {
            auto* action = sensitivity->addAction(item.first);
            action->setCheckable(true);
            action->setChecked(std::abs(qApp->property("ohosTouchKnobSensitivity").toDouble() - item.second) < 0.01);
            speeds->addAction(action);
            connect(action, &QAction::triggered, this, [this, value = item.second] {
                qApp->setProperty("ohosTouchKnobSensitivity", value);
                m_settings->setValue(ConfigKey("[OHOS]", "TouchKnobSensitivity"), value);
                m_settings->save();
            });
        }
        auto* selection = touch->addAction(QStringLiteral("曲库触摸多选"));
        selection->setCheckable(true);
        connect(selection, &QAction::toggled, this, [](bool enabled) { qApp->setProperty("ohosTouchMultiSelect", enabled); });
        auto* hotcue = touch->addAction(QStringLiteral("Hotcue 触摸编辑（点按菜单，拖动交换）"));
        hotcue->setCheckable(true);
        connect(hotcue, &QAction::toggled, this, [](bool enabled) { qApp->setProperty("ohosTouchHotcueEdit", enabled); });
    }
    m_resizeTimer.setSingleShot(true);
    m_resizeTimer.setInterval(180);
    connect(&m_resizeTimer, &QTimer::timeout, this, &WindowAdapter::fitSkin);
    qApp->installEventFilter(this);
    attachSafeAreaBridge(this, [this](const std::array<int, 4>& area) {
        const QMargins margins(area[0], area[1], area[2], area[3]);
        if (margins != m_safeNative) {
            m_safeNative = margins;
            qInfo() << "OHOS physical safe margins" << margins;
            m_resizeTimer.start();
            for (auto* widget : QApplication::topLevelWidgets()) {
                if (auto* dialog = qobject_cast<QDialog*>(widget); dialog && dialog->isVisible()) {
                    scheduleDialogFit(dialog);
                }
            }
        }
    });
    const auto applyKeyboardHeight = [this](int height) {
        m_window->setProperty("ohosKeyboardHeight", height);
        qInfo() << "OHOS window keyboard height" << height;
        for (auto* widget : QApplication::topLevelWidgets()) {
            if (auto* dialog = qobject_cast<QDialog*>(widget); dialog && dialog->isVisible()) {
                scheduleDialogFit(dialog);
            }
        }
    };
    attachWindowBridge(this, [this, applyKeyboardHeight](int height) {
        m_window->setProperty("ohosLatestKeyboardHeight", height);
        if (height > 0) {
            applyKeyboardHeight(height);
        } else {
            QTimer::singleShot(250, this, [this, applyKeyboardHeight] {
                if (m_window->property("ohosLatestKeyboardHeight").toInt() == 0) {
                    applyKeyboardHeight(0);
                }
            });
        }
    });
    connect(this, &QObject::destroyed, [](QObject* receiver) {
        detachWindowBridge(receiver);
        detachSafeAreaBridge(receiver);
    });
    connect(QGuiApplication::inputMethod(), &QInputMethod::keyboardRectangleChanged,
            this, [this] {
                for (auto* widget : QApplication::topLevelWidgets()) {
                    if (auto* dialog = qobject_cast<QDialog*>(widget); dialog && dialog->isVisible()) {
                        scheduleDialogFit(dialog);
                    }
                }
            });
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* focus) {
        if (focus) {
            if (auto* dialog = qobject_cast<QDialog*>(focus->window())) {
                scheduleDialogFit(dialog);
            }
        }
    });
    connect(qApp, &QGuiApplication::focusWindowChanged, this, [this](QWindow* window) {
        QTimer::singleShot(0, this, &WindowAdapter::publishDialogState);
        auto* modal = QApplication::activeModalWidget();
        if (modal && window && modal->windowHandle() != window &&
                !QApplication::activePopupWidget()) {
            QTimer::singleShot(0, modal, [guarded = QPointer<QWidget>(modal)] {
                if (guarded && guarded == QApplication::activeModalWidget() &&
                        !QApplication::activePopupWidget()) {
                    guarded->activateWindow();
                }
            });
        }
    });
    for (auto* widget : QApplication::topLevelWidgets()) {
        if (auto* dialog = qobject_cast<QDialog*>(widget)) {
            prepareDialog(dialog);
        }
    }
}

void WindowAdapter::setSkin(QWidget* skin) {
    m_skin = skin;
    skin->setParent(m_host);
    skin->ensurePolished();
    m_skinMinimum = skin->minimumSize().expandedTo(skin->minimumSizeHint());
    m_skinMinimum = m_skinMinimum.expandedTo(QSize(1, 1));
    for (auto* area : skin->findChildren<QAbstractScrollArea*>()) {
        enableTouchScrolling(area);
    }
    skin->setVisible(m_supported);
    placeSkin();
    m_resizeTimer.start();
}

bool WindowAdapter::eventFilter(QObject* object, QEvent* event) {
    if (auto* menu = qobject_cast<QMenu*>(object);
            menu && (event->type() == QEvent::Show || event->type() == QEvent::Move)) {
        const auto safe = safeGeometry();
        if (!safe.isEmpty() && menu->isVisible()) {
            menu->setMaximumSize(safe.size());
            const auto position = QPoint(
                    std::clamp(menu->x(), safe.left(), std::max(safe.left(), safe.right() - menu->width() + 1)),
                    std::clamp(menu->y(), safe.top(), std::max(safe.top(), safe.bottom() - menu->height() + 1)));
            if (position != menu->pos()) {
                menu->move(position);
            }
        }
    }
    if (auto* menu = qobject_cast<QMenu*>(object);
            menu && (event->type() == QEvent::MouseButtonPress ||
                            event->type() == QEvent::MouseButtonRelease)) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        qInfo() << "OHOS menu mouse" << event->type() << "local" << mouse->position()
                << "global" << mouse->globalPosition() << "geometry" << menu->geometry()
                << "mapped" << menu->mapFromGlobal(mouse->globalPosition())
                << "action" << menu->actionAt(mouse->position().toPoint());
    }
    if (event->type() == QEvent::Polish) {
        if (auto* dialog = qobject_cast<QDialog*>(object)) {
            prepareDialog(dialog);
        }
    }
    if (event->type() == QEvent::Hide && qobject_cast<QDialog*>(object)) {
        QTimer::singleShot(0, this, [this] {
            publishDialogState();
            auto* active = QApplication::activeWindow();
            auto* modal = QApplication::activeModalWidget();
            if ((!modal || !modal->isVisible()) &&
                    (!active || active == m_window || !active->isVisible())) {
                m_window->setFocus(Qt::OtherFocusReason);
                QGuiApplication::inputMethod()->hide();
            }
        });
    }
    if (event->type() == QEvent::Show) {
        if (auto* dialog = qobject_cast<QDialog*>(object);
                dialog && !dialog->property("ohosDialogShowLogged").toBool()) {
            dialog->setProperty("ohosDialogShowLogged", true);
            qInfo() << "OHOS original dialog show" << dialog->metaObject()->className()
                    << dialog->isWindow() << dialog->isVisible() << dialog->geometry()
                    << "layout" << bool(dialog->layout());
            mixxx::Logging::flushLogFile();
        }
    }
    if (auto* dialog = qobject_cast<QDialog*>(object);
            dialog && dialog->isWindow() && dialog->isVisible() &&
            (event->type() == QEvent::Show || event->type() == QEvent::Resize ||
                    event->type() == QEvent::Move || event->type() == QEvent::LayoutRequest ||
                    event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange ||
                    event->type() == QEvent::DevicePixelRatioChange)) {
        const bool managesLayout = qobject_cast<QMessageBox*>(dialog) ||
                qobject_cast<QProgressDialog*>(dialog) || qobject_cast<QInputDialog*>(dialog) ||
                qobject_cast<QFileDialog*>(dialog);
        if (event->type() == QEvent::Resize && !dialog->property("ohosDialogFitPending").toBool() &&
                (!managesLayout || !dialog->property("ohosDialogAdapted").toBool())) {
            dialog->setProperty("ohosDialogPreferredSize", dialog->size());
        }
        scheduleDialogFit(dialog);
    }
    if (event->type() == QEvent::MouseMove) {
        auto* widget = qobject_cast<QWidget*>(object);
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (widget && QScroller::hasScroller(widget) &&
                !widget->property("ohosTouchTrackHold").toBool() &&
                mouse->source() == Qt::MouseEventSynthesizedByQt &&
                mouse->pointingDevice()->type() == QInputDevice::DeviceType::TouchScreen &&
                mouse->buttons().testFlag(Qt::LeftButton)) {
            return true;
        }
    }
    if (event->type() == QEvent::TouchBegin || event->type() == QEvent::MouseButtonPress) {
        auto* widget = qobject_cast<QWidget*>(object);
        if (auto* window = qobject_cast<QWindow*>(object);
                window && event->type() == QEvent::TouchBegin) {
            const auto* touch = static_cast<QTouchEvent*>(event);
            if (!touch->points().isEmpty()) {
                const auto& point = touch->points().front();
                qInfo() << "OHOS window touch" << window->objectName()
                        << "local" << point.position() << "global" << point.globalPosition()
                        << "window" << window->geometry() << "DPR" << window->devicePixelRatio();
            }
        }
        if (qobject_cast<QWindow*>(object) || (widget && QScroller::hasScroller(widget))) {
            const char* key = event->type() == QEvent::TouchBegin
                    ? "ohosTouchInputLogged" : "ohosMouseInputLogged";
            if (!object->property(key).toBool()) {
                object->setProperty(key, true);
                auto* input = static_cast<QPointerEvent*>(event);
                qInfo() << "OHOS scroll input" << object->metaObject()->className()
                        << event->type() << input->pointingDevice()->type();
                if (event->type() == QEvent::MouseButtonPress) {
                    qInfo() << "OHOS scroll mouse source"
                            << static_cast<QMouseEvent*>(event)->source();
                }
            }
        }
    }
    if (event->type() == QEvent::MouseButtonRelease) {
        auto* widget = qobject_cast<QWidget*>(object);
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (widget && mouse->source() == Qt::MouseEventSynthesizedByQt &&
                mouse->position().x() >= 0 && QScroller::hasScroller(widget)) {
            auto* scroller = QScroller::scroller(widget);
            if (scroller->state() == QScroller::Dragging ||
                    scroller->state() == QScroller::Scrolling) {
                const QPointF outside(-QWIDGETSIZE_MAX, -QWIDGETSIZE_MAX);
                QMouseEvent cancel(QEvent::MouseButtonRelease, outside, outside,
                        outside, mouse->button(), mouse->buttons(), mouse->modifiers(),
                        mouse->source(), mouse->pointingDevice());
                QCoreApplication::sendEvent(widget, &cancel);
                return true;
            }
        }
    }
    if (event->type() == QEvent::Show) {
        auto* area = qobject_cast<QAbstractScrollArea*>(object);
        if (area) {
            enableTouchScrolling(area);
        }
    }
    if (object == m_host && event->type() == QEvent::Resize) {
        placeSkin();
    }
    if ((object == m_window || object == m_host || object == m_skin) &&
            (event->type() == QEvent::Resize || event->type() == QEvent::Show ||
                    event->type() == QEvent::LayoutRequest ||
                    event->type() == QEvent::WindowStateChange) && !m_fitting) {
        m_resizeTimer.start();
    }
    return QObject::eventFilter(object, event);
}

void WindowAdapter::enableTouchScrolling(QAbstractScrollArea* area) {
    if (qobject_cast<QHeaderView*>(area) || area->property("ohosTouchScrolling").toBool()) {
        return;
    }
    area->setProperty("ohosTouchScrolling", true);
    area->viewport()->setAttribute(Qt::WA_NoMousePropagation);
    if (auto* view = qobject_cast<QAbstractItemView*>(area)) {
        view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        view->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
        view->setEditTriggers(view->editTriggers() & ~QAbstractItemView::SelectedClicked);
    }
    auto* scroller = QScroller::scroller(area->viewport());
    auto properties = scroller->scrollerProperties();
    properties.setScrollMetric(QScrollerProperties::MousePressEventDelay, 0.15);
    properties.setScrollMetric(QScrollerProperties::HorizontalOvershootPolicy,
            QScrollerProperties::OvershootAlwaysOff);
    properties.setScrollMetric(QScrollerProperties::VerticalOvershootPolicy,
            QScrollerProperties::OvershootAlwaysOff);
    properties.setScrollMetric(QScrollerProperties::FrameRate, QScrollerProperties::Fps60);
    scroller->setScrollerProperties(properties);
    QScroller::grabGesture(area->viewport(), QScroller::TouchGesture);
    qInfo() << "OHOS touch scrolling enabled" << area->metaObject()->className();
}

void WindowAdapter::placeSkin() {
    if (m_skin) {
        m_skin->setGeometry(m_host->rect());
    }
    m_sizeNotice->setGeometry(m_host->rect());
}

void WindowAdapter::prepareDialog(QDialog* dialog) {
    if (dialog->isWindow() && !dialog->isVisible()) {
        dialog->setAttribute(Qt::WA_ContentsMarginsRespectsSafeArea, false);
        dialog->setWindowFlag(Qt::FramelessWindowHint);
    }
}

void WindowAdapter::publishDialogState() {
    auto* modal = QApplication::activeModalWidget();
    auto* window = modal ? modal->windowHandle() : nullptr;
    if (!modal || !modal->isVisible() || !window || !window->handle() ||
            QApplication::activePopupWidget()) {
        publishWindowState("{}");
        return;
    }
    const QRect rect = window->handle()->geometry();
    const int revision = m_window->property("ohosDialogRevision").toInt() + 1;
    m_window->setProperty("ohosDialogRevision", revision);
    publishWindowState(QJsonDocument(QJsonObject{
            {QStringLiteral("left"), rect.x()},
            {QStringLiteral("top"), rect.y()},
            {QStringLiteral("width"), rect.width()},
            {QStringLiteral("height"), rect.height()},
            {QStringLiteral("revision"), revision}}).toJson(QJsonDocument::Compact));
}

void WindowAdapter::scheduleDialogFit(QDialog* dialog) {
    if (dialog->property("ohosDialogFitPending").toBool()) {
        return;
    }
    dialog->setProperty("ohosDialogFitPending", true);
    QTimer::singleShot(0, this, [this, guarded = QPointer<QDialog>(dialog)] {
        if (guarded) {
            fitDialog(guarded);
            guarded->setProperty("ohosDialogFitPending", false);
        }
    });
}

void WindowAdapter::fitDialog(QDialog* dialog) {
    if (!dialog->isVisible() || !dialog->layout()) {
        return;
    }
    if (!dialog->windowFlags().testFlag(Qt::FramelessWindowHint)) {
        dialog->setWindowFlag(Qt::FramelessWindowHint);
        dialog->show();
        scheduleDialogFit(dialog);
        return;
    }
    const bool managesLayout = qobject_cast<QMessageBox*>(dialog) ||
            qobject_cast<QProgressDialog*>(dialog) || qobject_cast<QInputDialog*>(dialog) ||
            qobject_cast<QFileDialog*>(dialog);
    if (!dialog->property("ohosDialogAdapted").toBool()) {
        dialog->setProperty("ohosDialogAdapted", true);
        dialog->setAttribute(Qt::WA_ContentsMarginsRespectsSafeArea, false);
        dialog->setProperty("ohosDialogPreferredSize", dialog->size());
        const QSize minimum = dialog->minimumSize().expandedTo(dialog->minimumSizeHint());
        auto* scroll = dialog->layout()->count() == 1
                ? qobject_cast<QScrollArea*>(dialog->layout()->itemAt(0)->widget()) : nullptr;
        if (!scroll && !managesLayout) {
            auto* content = new QWidget(dialog);
            content->setLayout(dialog->layout());
            content->setMinimumSize(minimum);
            scroll = new QScrollArea(dialog);
            scroll->setFrameShape(QFrame::NoFrame);
            scroll->setWidgetResizable(true);
            scroll->setWidget(content);
            auto* layout = new QVBoxLayout(dialog);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->addWidget(scroll);
        }
        if (!managesLayout) {
            dialog->layout()->setSizeConstraint(QLayout::SetNoConstraint);
            dialog->setMinimumSize(0, 0);
            dialog->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        }
        if (scroll) {
            enableTouchScrolling(scroll);
        }
        for (auto* area : dialog->findChildren<QAbstractScrollArea*>()) {
            enableTouchScrolling(area);
        }
        qInfo() << "OHOS original dialog scrolling enabled" << dialog->metaObject()->className()
                << "content minimum" << minimum;
    }
    dialog->setAutoFillBackground(true);
    auto palette = dialog->palette();
    auto background = palette.color(QPalette::Window);
    if (background.alpha() != 255) {
        background.setAlpha(255);
        palette.setColor(QPalette::Window, background);
        dialog->setPalette(palette);
    }
    QRect available = safeGeometry();
    if (auto* screen = m_window->screen()) {
        available = available.intersected(screen->availableGeometry());
    }
    QRect windowAvailable = available;
    const auto keyboard = QGuiApplication::inputMethod()->keyboardRectangle();
    const int keyboardHeight = m_window->property("ohosKeyboardHeight").toInt();
    if (keyboardHeight > 0) {
        const int keyboardTop = m_window->mapToGlobal(QPoint()).y() + m_window->height() -
                static_cast<int>(std::ceil(keyboardHeight / m_window->devicePixelRatio()));
        available.setBottom(std::min(available.bottom(), keyboardTop - 1));
    }
    if (QGuiApplication::inputMethod()->isVisible() && !keyboard.isEmpty()) {
        auto* focus = QApplication::focusWidget();
        if (focus && focus->window() == dialog) {
            const int keyboardTop = dialog->mapToGlobal(keyboard.topLeft().toPoint()).y();
            if (keyboardTop > available.top() && keyboardTop <= available.bottom()) {
                available.setBottom(keyboardTop - 1);
            }
        }
    }
    available.adjust(8, 8, -8, -8);
    windowAvailable.adjust(8, 8, -8, -8);
    if (windowAvailable.isEmpty()) {
        return;
    }
    QSize preferred = dialog->property("ohosDialogPreferredSize").toSize();
    if (managesLayout) {
        preferred = preferred.expandedTo(dialog->sizeHint());
        if (qobject_cast<QMessageBox*>(dialog)) {
            preferred.setWidth(std::max(320, preferred.width()));
        }
    }
    const QSize fitted = preferred.boundedTo(windowAvailable.size());
    const QPoint position(
            std::clamp(dialog->x(), windowAvailable.left(), windowAvailable.right() - fitted.width() + 1),
            std::clamp(dialog->y(), windowAvailable.top(), windowAvailable.bottom() - fitted.height() + 1));
    if (dialog->geometry() != QRect(position, fitted)) {
        dialog->setGeometry(QRect(position, fitted));
    }
    auto* scroll = dialog->layout()->count() > 0
            ? qobject_cast<QScrollArea*>(dialog->layout()->itemAt(0)->widget()) : nullptr;
    const auto margins = dialog->layout()->contentsMargins();
    const int viewportHeight = std::max(1, std::min(
            fitted.height() - margins.top() - margins.bottom(),
            available.bottom() - position.y() - margins.top() + 1));
    if (scroll) {
        scroll->setFixedHeight(viewportHeight);
        dialog->layout()->setAlignment(scroll, Qt::AlignTop);
    }
    if (dialog->property("ohosLastFittedGeometry").toRect() != dialog->geometry() ||
            dialog->property("ohosLastDialogViewportHeight").toInt() != viewportHeight) {
        dialog->setProperty("ohosLastFittedGeometry", dialog->geometry());
        dialog->setProperty("ohosLastDialogViewportHeight", viewportHeight);
        qInfo() << "OHOS original dialog fitted" << dialog->metaObject()->className()
                << dialog->geometry() << "frame" << dialog->frameGeometry()
                << "available" << windowAvailable << "viewport height" << viewportHeight;
        dialog->update();
        mixxx::Logging::flushLogFile();
    }
    const auto focus = QPointer<QWidget>(QApplication::focusWidget());
    if (focus && focus->window() == dialog) {
        QTimer::singleShot(0, dialog, [guarded = QPointer<QDialog>(dialog), focus] {
            if (guarded && focus) {
                for (auto* scroll : guarded->findChildren<QScrollArea*>()) {
                    if (scroll->widget() && scroll->widget()->isAncestorOf(focus)) {
                        scroll->ensureWidgetVisible(focus);
                    }
                }
            }
        });
    }
    publishDialogState();
}

void WindowAdapter::fitSkin() {
    auto* window = m_window->windowHandle();
    if (!m_skin || !m_window->isVisible() || !window || !window->handle()) {
        return;
    }
    auto* screen = window->screen();
    if (!screen) {
        return;
    }
    if (m_screen != screen) {
        m_screen = screen;
        m_baseDpr = window->devicePixelRatio();
        m_baseScreenFactor = QHighDpiScaling::factor(screen) /
                CmdlineArgs::Instance().getScaleFactor();
        m_fitRatio = 1;
    }
    const QSize nativeSize = window->handle()->geometry().size();
    const auto safe = safeMarginsNative();
    const QSize contentSize(std::max(1, nativeSize.width() - safe.left() - safe.right()),
            std::max(1, nativeSize.height() - safe.top() - safe.bottom()));
    const int menuHeight = m_window->menuBar()->isVisible()
            ? m_window->menuBar()->height() : 0;
    m_skinMinimum = m_skin->minimumSize().expandedTo(m_skin->minimumSizeHint())
                            .expandedTo(QSize(1, 1));
    const qreal ratio = std::min({qreal(1),
            contentSize.width() / (m_baseDpr * (m_skinMinimum.width() + 2)),
            contentSize.height() /
                    (m_baseDpr * (m_skinMinimum.height() + menuHeight + 2))});
    const bool supported = ratio >= 0.5;
    const qreal targetRatio = supported ? ratio : 0.5;
    m_fitting = true;
    if (std::abs(targetRatio - m_fitRatio) > 0.005) {
        m_fitRatio = targetRatio;
        QHighDpiScaling::setScreenFactor(screen, m_baseScreenFactor * m_fitRatio);
        const auto windows = QGuiApplication::allWindows();
        for (auto* item : windows) {
            if (item->screen() == screen && item->handle()) {
                QWindowPrivate::get(item)->updateDevicePixelRatio();
                if (item->isTopLevel()) {
                    QWindowSystemInterface::handleGeometryChange(item,
                            item->handle()->geometry());
                }
            }
        }
    }
    applySafeMargins();
    if (m_lastNativeSize != contentSize) {
        m_lastNativeSize = contentSize;
        qInfo() << "OHOS original skin scale" << m_fitRatio
                << "native window" << nativeSize << "skin minimum" << m_skinMinimum
                << "base DPR" << m_baseDpr << "usable native size" << contentSize << "display mode" << m_displayMode;
    }
    if (m_supported != supported) {
        m_supported = supported;
        m_skin->setVisible(supported);
        m_sizeNotice->setVisible(!supported);
        qInfo() << "OHOS original skin window supported" << supported;
    }
    placeSkin();
    m_fitting = false;
}

QMargins WindowAdapter::safeMarginsNative() const {
    if (m_displayMode == 1) {
        return {};
    }
    const int extra = m_displayMode == 2 ? int(std::ceil(12 * m_baseDpr)) : 0;
    return m_safeNative + QMargins(extra, extra, extra, extra);
}

void WindowAdapter::applySafeMargins() {
    const auto native = safeMarginsNative();
    const auto dpr = m_window->devicePixelRatio();
    const QMargins margins(int(std::ceil(native.left() / dpr)), int(std::ceil(native.top() / dpr)),
            int(std::ceil(native.right() / dpr)), int(std::ceil(native.bottom() / dpr)));
    if (m_window->contentsMargins() != margins) {
        m_window->setContentsMargins(margins);
    }
}

QRect WindowAdapter::safeGeometry() const {
    const auto native = safeMarginsNative();
    const auto dpr = m_window->devicePixelRatio();
    return QRect(m_window->mapToGlobal(QPoint()), m_window->size()).marginsRemoved(
            QMargins(int(std::ceil(native.left() / dpr)), int(std::ceil(native.top() / dpr)),
                    int(std::ceil(native.right() / dpr)), int(std::ceil(native.bottom() / dpr))));
}

}
