#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

#include "clineedit.h"

#include "enums.h"
#include "settings.h"
#include "sizes.h"
#include "utils.h"

class AniList;

namespace Ui
{
class SettingsDialog;
}

class SettingsDialog : public QDialog
{
    Q_OBJECT
    Q_ENUM(AdvancePageGestureDirection)
    Q_ENUM(AdvancePageHWButton)

public:
    explicit SettingsDialog(Settings *settings, AniList *aniList = nullptr, QWidget *parent = nullptr);
    ~SettingsDialog();

    void open() override;

signals:
    void activeMangasChanged();
    void mangaOrderMethodChanged();
    void ditheringMethodChanged();

private slots:
    void on_pushButtonOk_clicked();

private:
    Ui::SettingsDialog *ui;
    Settings *settings;
    AniList *aniList;
    CLineEdit *aniListTokenEdit;
    QLabel *aniListStatusLabel;
    QPushButton *aniListLoginBtn;
    bool internalChange;

    void resetUI();
    void adjustUI();

    void updateSettings();
    void updateActiveMangasSettings(const QString &name, bool enabled);

    void setupSourcesList();
};

#endif  // SETTINGSDIALOG_H
