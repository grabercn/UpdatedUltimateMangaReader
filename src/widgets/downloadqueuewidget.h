#ifndef DOWNLOADQUEUEWIDGET_H
#define DOWNLOADQUEUEWIDGET_H

#include <QLabel>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

#include "enums.h"
#include "sizes.h"
#include "utils.h"

struct DownloadJob
{
    QString title;
    QString source;
    int fromChapter = 0;
    int toChapter = 0;
    int currentChapter = 0;
    int totalPages = 0;
    int completedPages = 0;
    bool toDevice = false;
    bool isLightNovel = false;
    enum State { Queued, Active, Completed, Failed, Cancelled } state = Queued;
    QString errorMsg;
};

struct CachedManga
{
    QString source;
    QString title;
    QString path;
    int chapters;
    int pages;
    qint64 sizeMB;
    int progressChapter;
    int progressPage;
};

class DownloadQueueWidget : public QWidget
{
    Q_OBJECT

public:
    explicit DownloadQueueWidget(QWidget *parent = nullptr);

    void addJob(const QString &title, const QString &source, int from, int to,
                bool toDevice, bool isLN);
    void updateActiveJob(int completedPages, int totalPages, int currentChapter);
    void jobCompleted(const QString &title);
    void jobFailed(const QString &title, const QString &error);

    void setAniListLinkInfo(int count, qint64 sizeBytes);
    class HomeWidget *homeWidget = nullptr;

signals:
    void backClicked();
    void cancelRequested();
    void openMangaRequested(const QString &source, const QString &title);
    void resetAniListLinksRequested();
    void clearAllCacheRequested();

private:
    QLabel *headerLabel;
    QListWidget *jobList;
    QPushButton *backBtn;
    QPushButton *cancelBtn;
    QPushButton *clearBtn;
    QPushButton *showCachedBtn;
    QPushButton *deleteSelectedBtn;
    QLabel *statusLabel;
    QProgressBar *activeProgress;
    bool showingCached;

    QList<DownloadJob> jobs;

    void refreshList();
    void refreshCachedList();
    QList<CachedManga> scanCachedManga();
};

#endif  // DOWNLOADQUEUEWIDGET_H
