#ifndef SCREENSAVERDIALOG_H
#define SCREENSAVERDIALOG_H

#include <QDialog>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLayout>

#include "enums.h"
#include "imageprocessingnative.h"
#include "mangainfo.h"
#include "staticsettings.h"

namespace Ui
{
class ScreensaverDialog;
}

class ScreensaverDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ScreensaverDialog(QWidget *parent);
    ~ScreensaverDialog();

    void showRandomScreensaver();
    void setCurrentManga(QSharedPointer<MangaInfo> manga, int chapter, int page);
    void setBatteryLevel(int level, bool charging = false);

protected:
    bool event(QEvent *event) override;

private:
    Ui::ScreensaverDialog *ui;
    QSharedPointer<MangaInfo> currentManga;
    int currentChapter = 0;
    int currentPage = 0;
    int battery = 100;
    bool charging = false;
};

#endif  // SCREENSAVERDIALOG_H
