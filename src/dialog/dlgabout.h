#pragma once

#include <QDialog>

#include "dialog/ui_dlgaboutdlg.h"
#include "preferences/usersettings.h"

class DlgAbout : public QDialog, public Ui::DlgAboutDlg {
    Q_OBJECT
  public:
    explicit DlgAbout(UserSettingsPointer settings = {});
};
