#pragma once

#include <QObject>
#include <QPointer>
#include <QSize>
#include <QTimer>

class QLabel;
class QAbstractScrollArea;
class QDialog;
class QMainWindow;
class QScreen;
class QWidget;

namespace mixxx::ohos {

class WindowAdapter final : public QObject {
  public:
    explicit WindowAdapter(QMainWindow* window);
    void setSkin(QWidget* skin);

  protected:
    bool eventFilter(QObject* object, QEvent* event) override;

  private:
    void fitSkin();
    void placeSkin();
    void enableTouchScrolling(QAbstractScrollArea* area);
    void prepareDialog(QDialog* dialog);
    void publishDialogState();
    void scheduleDialogFit(QDialog* dialog);
    void fitDialog(QDialog* dialog);

    QMainWindow* m_window;
    QWidget* m_host;
    QLabel* m_sizeNotice;
    QPointer<QWidget> m_skin;
    QPointer<QScreen> m_screen;
    QSize m_skinMinimum;
    QSize m_lastNativeSize;
    QTimer m_resizeTimer;
    qreal m_baseDpr = 1;
    qreal m_baseScreenFactor = 1;
    qreal m_fitRatio = 1;
    bool m_supported = true;
    bool m_fitting = false;
};

}
