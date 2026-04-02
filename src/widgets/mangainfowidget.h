#ifndef MANGAINFOWIDGET_H
#define MANGAINFOWIDGET_H

#include <QComboBox>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QWidget>

#include "imageprocessingnative.h"
#include "mangaindex.h"
#include "mangainfo.h"
#include "sizes.h"
#include "utils.h"

class AniList;

namespace Ui
{
class MangaInfoWidget;
}

class MangaInfoWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MangaInfoWidget(QWidget *parent = nullptr);
    ~MangaInfoWidget();

    void setManga(QSharedPointer<MangaInfo> manga);
    void setFavoriteButtonState(bool state);
    void setAniList(AniList *aniList);

signals:
    void toggleFavoriteClicked(QSharedPointer<MangaInfo> manga);
    void readMangaClicked(const MangaIndex &index);
    void readMangaContinueClicked();
    void downloadMangaClicked();

public slots:
    void updateManga(bool newchapters);
    void updateCover();

private slots:
    void on_toolButtonAddFavorites_clicked();

    void on_listViewChapters_clicked(const QModelIndex &index);

    void on_pushButtonReadLatest_clicked();
    void on_pushButtonReadContinue_clicked();
    void on_pushButtonReadFirst_clicked();

    void on_toolButtonDownload_clicked();

private:
    Ui::MangaInfoWidget *ui;

    QSharedPointer<MangaInfo> currentmanga;
    AniList *aniList = nullptr;

    // AniList tracking UI
    QFrame *aniListFrame = nullptr;
    QLabel *aniListLabel = nullptr;
    QComboBox *aniListStatusCombo = nullptr;
    QComboBox *aniListChapterCombo = nullptr;
    QComboBox *aniListVolumeCombo = nullptr;
    QComboBox *aniListScoreCombo = nullptr;
    QPushButton *aniListSyncBtn = nullptr;
    int currentAniListMediaId = 0;

    bool eventFilter(QObject *obj, QEvent *event) override;
    void adjustUI();
    void setupAniListUI();
    void updateAniListTracking();
    bool summaryExpanded = false;
    bool isDownloadOnly = false;

    void updateInfos();
};

#endif  // MANGAINFOWIDGET_H
