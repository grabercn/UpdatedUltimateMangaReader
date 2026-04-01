#ifndef HOMEWIDGET_H
#define HOMEWIDGET_H

#include <QLabel>
#include <QScrollBar>
#include <QSet>
#include <QStandardItemModel>
#include <QStringListModel>

#include "abstractmangasource.h"
#include "clineedit.h"
#include "spinnerwidget.h"
#include "sizes.h"
#include "staticsettings.h"

class AniList;
struct AniListEntry;

namespace Ui
{
class HomeWidget;
}

struct SearchResult
{
    QString title;
    QString altTitle;  // romaji/japanese alternate name
    QString url;
    QString sourceName;
    AbstractMangaSource *source;
    bool absoluteUrl;
    ContentType contentType;
};

class HomeWidget : public QWidget
{
    Q_OBJECT

public:
    explicit HomeWidget(QWidget *parent = nullptr);
    ~HomeWidget();

    void updateSourcesList(const QList<AbstractMangaSource *> &sources);
    void currentMangaSourceChanged(AbstractMangaSource *source);
    void setAniList(AniList *aniList);
    void setFavoritesManager(class FavoritesManager *fm);
    void refreshHomeView();

    // AniList link management
    int aniListLinkCount() const { return aniListLocalMap.size(); }
    QStringList aniListLinkDescriptions() const;
    void resetAniListLinks();

signals:
    void mangaSourceClicked(AbstractMangaSource *source);
    void mangaClicked(const QString &mangaurl, const QString &mangatitle);
    void favoritesCleared();
    void openHistoryRequested();
    void openAniListRequested();

private slots:
    void on_listViewSources_clicked(const QModelIndex &index);
    void on_pushButtonFilter_clicked();
    void on_pushButtonFilterClear_clicked();
    void on_listViewMangas_clicked(const QModelIndex &index);

private:
    Ui::HomeWidget *ui;

    QList<AbstractMangaSource *> allSources;
    QSet<int> selectedSourceIndices;

    QList<SearchResult> searchResults;
    bool searchActive;
    int searchGeneration;
    int searchListOffset;  // number of header lines before results

    void doLiveSearch(const QString &query);
    void onSourceSearchComplete(int generation, AbstractMangaSource *source,
                                 const MangaList &results);
    void refreshMangaListView();
    void refreshSourceHighlights();
    void showRecentSearches();
    void showOfflineReads();
    bool isOffline() const;
    void adjustUI();

    QStringList recentSearches;
    void saveRecentSearches();
    void loadRecentSearches();

    AniList *aniList = nullptr;

    // Cached AniList -> local manga mapping
    struct LocalMangaMatch
    {
        QString source;
        QString dirName;
        QString infoPath;
    };
    QMap<QString, LocalMangaMatch> aniListLocalMap;  // anilist title -> local match
    void buildAniListLocalMap();
    void saveAniListLocalMap();
    void loadAniListLocalMap();
    void clearAniListLocalMap();

    // Background matching
    struct CachedDir { QString source; QString dirName; QString normName; QString path; };
    QList<CachedDir> cachedDirsForMatching;
    QList<AniListEntry> pendingMatchEntries;
    QTimer *bgMatchTimer = nullptr;
    void startBackgroundMatching();
    void matchNextBatch();
    bool tryMatch(const AniListEntry &entry);
    SpinnerWidget *searchSpinner = nullptr;
    QLabel *altNamesLabel = nullptr;
    class FavoritesManager *favManager = nullptr;

    // Home view item types stored in Qt::UserRole
    enum HomeItemType { HeaderItem = 0, SearchItem, DownloadedItem, AniListItem };
    // Cached AniList reading list (only refresh on navigate away/back)
    QStringList cachedAniListReading;
    bool aniListCacheValid = false;

    QList<QStandardItem *> listViewItemfromMangaSource(AbstractMangaSource *source, bool selected);
};

#endif  // HOMEWIDGET_H
