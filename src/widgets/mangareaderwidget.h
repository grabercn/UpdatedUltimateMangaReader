#ifndef MANGAREADERWIDGET_H
#define MANGAREADERWIDGET_H

#include <QGesture>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTextBrowser>

#include "customgesturerecognizer.h"
#include "enums.h"
#include "gotodialog.h"
#include "imageprocessingnative.h"
#include "mangainfo.h"
#include "readingprogress.h"
#include "settings.h"
#include "sizes.h"
#include "utils.h"

namespace Ui
{
class MangaReaderWidget;
}

class MangaReaderWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MangaReaderWidget(QWidget *parent = nullptr);
    ~MangaReaderWidget();

    void showImage(const QString &path);
    void showText(const QString &text, const QString &chapterTitle);
    void setTextMode(bool textMode);
    void updateCurrentIndex(const ReadingProgress &progress);

    void setFrontLightPanelState(int lightmin, int lightmax, int light, int comflightmin, int comflightmax,
                                 int comflight);
    void setFrontLightPanelState(int light, int comflight);

    bool addImageToCache(const QString &path, bool isPreload = true);
    bool addImageToCache(const QString &path, QSharedPointer<QImage> img);
    void updateMenuBar();
    void setSettings(Settings *settings);

    void clearCache();
    int cacheSize() const { return imgcache.size(); }
    void trimCache() { if (imgcache.size() > 1) imgcache.removeLast(); }

    // Text reader page info
    int currentTextPage() const { return textCurrentPage; }
    int totalTextPages() const { return textPageOffsets.size(); }

signals:
    void changeView(WidgetTab page);
    void advancPageClicked(PageTurnDirection direction);
    void closeApp();
    void back();
    void frontlightchanged(int light, int comflight);
    void gotoIndex(const MangaIndex &mangaIndex);
    void bookmarkRequested();

private slots:
    void on_pushButtonReaderHome_clicked();
    void on_pushButtonReaderBack_clicked();
    void on_pushButtonReaderFavorites_clicked();

    void on_horizontalSliderLight_valueChanged(int value);
    void on_horizontalSliderComfLight_valueChanged(int value);

    void on_pushButtonReaderGoto_clicked();

protected:
    bool event(QEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    bool gestureEvent(QGestureEvent *event);
    bool buttonPressEvent(QKeyEvent *event);

    Ui::MangaReaderWidget *ui;

    QQueue<QPair<QSharedPointer<QImage>, QString>> imgcache;

    GotoDialog *gotodialog;

    // Text reader (paginated)
    QTextBrowser *textReader;
    QWidget *textBottomBar;
    QPushButton *textMenuBtn;
    QLabel *textPageLabel;
    QProgressBar *textProgressBar;
    bool isTextMode;
    QList<int> textPageOffsets;
    int textCurrentPage;

    void textPageForward();
    void textPageBack();
    void textGoToPage(int page);
    void paginateText();
    void updateTextBottomBar();

    Settings *settings;

    void adjustUI();

    int searchCache(const QString &path) const;

    void showMenuBar(bool show);
    void checkMem();
};

#endif  // MANGAREADERWIDGET_H
