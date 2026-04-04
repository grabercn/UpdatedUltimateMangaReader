#include "homewidget.h"

#include <QApplication>
#include <QStandardItemModel>

#include "anilist.h"
#include "favorite.h"
#include "mangainfo.h"
#include "favoritesmanager.h"
#include "ui_homewidget.h"

HomeWidget::HomeWidget(QWidget *parent)
    : QWidget(parent),
      ui(new Ui::HomeWidget),
      allSources(),
      selectedSourceIndices(),
      searchResults(),
      searchActive(false),
      searchGeneration(0),
      searchListOffset(0)
{
    ui->setupUi(this);
    adjustUI();

    ui->listViewSources->setModel(new QStandardItemModel(this));
    ui->listViewSources->setSelectionMode(QAbstractItemView::NoSelection);
    ui->listViewMangas->setModel(new QStringListModel(this));

    // Alt names label between search bar and results
    altNamesLabel = new QLabel(this);
    altNamesLabel->setStyleSheet("color: #888; padding: 1px 12px;");
    altNamesLabel->setWordWrap(false);
    altNamesLabel->hide();
    // Insert into the layout between search bar and list
    auto *mainLayout = qobject_cast<QVBoxLayout *>(this->layout());
    if (mainLayout)
    {
        // Find listViewMangas index and insert before it
        for (int i = 0; i < mainLayout->count(); i++)
        {
            auto *item = mainLayout->itemAt(i);
            if (item && item->widget() == ui->listViewMangas)
            {
                mainLayout->insertWidget(i, altNamesLabel);
                break;
            }
        }
    }

    QObject::connect(ui->lineEditFilter, &CLineEdit::returnPressed, this,
                     &HomeWidget::on_pushButtonFilter_clicked);

    loadRecentSearches();
    loadAniListLocalMap();
    showRecentSearches();
}

HomeWidget::~HomeWidget()
{
    delete ui;
}

void HomeWidget::adjustUI()
{
    activateScroller(ui->listViewSources);
    activateScroller(ui->listViewMangas);

    ui->pushButtonFilter->setProperty("type", "borderless");
    ui->pushButtonFilterClear->setProperty("type", "borderless");

    ui->pushButtonFilter->setFixedHeight(SIZES.buttonSize);
    ui->pushButtonFilterClear->setFixedHeight(SIZES.buttonSize);

    ui->listViewSources->setFixedHeight(SIZES.listSourcesHeight);
    ui->listViewSources->setIconSize(QSize(SIZES.mangasourceIconSize, SIZES.mangasourceIconSize));

    ui->listViewMangas->setFocusPolicy(Qt::FocusPolicy::NoFocus);
    ui->listViewMangas->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui->listViewMangas->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
}

void HomeWidget::updateSourcesList(const QList<AbstractMangaSource *> &sources)
{
    allSources = sources;
    selectedSourceIndices.clear();

    auto model = dynamic_cast<QStandardItemModel *>(ui->listViewSources->model());
    if (!model)
        return;
    model->clear();

    for (auto ms : sources)
        model->appendRow(listViewItemfromMangaSource(ms, true));
}

void HomeWidget::currentMangaSourceChanged(AbstractMangaSource *source)
{
    Q_UNUSED(source);
}

QList<QStandardItem *> HomeWidget::listViewItemfromMangaSource(AbstractMangaSource *source, bool selected)
{
    QList<QStandardItem *> items;
    QStandardItem *item = new QStandardItem(source->name);
    item->setData(QVariant::fromValue(static_cast<void *>(source)));
    item->setIcon(QIcon(QPixmap(":/images/mangahostlogos/" + source->name.toLower() + ".png")));
    item->setSizeHint(QSize(SIZES.mangasourceItemWidth, SIZES.mangasourceItemHeight));
    item->setText(source->name);

    auto f = item->font();
    f.setUnderline(selected);
    item->setFont(f);

    if (!selected)
        item->setForeground(QBrush(Qt::gray));

    items.append(item);
    return items;
}

void HomeWidget::refreshSourceHighlights()
{
    auto model = dynamic_cast<QStandardItemModel *>(ui->listViewSources->model());
    if (!model)
        return;
    bool allSelected = selectedSourceIndices.isEmpty();

    for (int i = 0; i < model->rowCount(); i++)
    {
        QStandardItem *item = model->item(i);
        bool isSelected = allSelected || selectedSourceIndices.contains(i);
        auto f = item->font();
        f.setUnderline(isSelected);
        item->setFont(f);
        item->setForeground(isSelected ? QBrush(Qt::black) : QBrush(Qt::gray));
    }
}

void HomeWidget::on_listViewSources_clicked(const QModelIndex &index)
{
    int row = index.row();

    if (selectedSourceIndices.isEmpty())
    {
        // Was "all" mode -> select only this one
        selectedSourceIndices.insert(row);
    }
    else if (selectedSourceIndices.contains(row))
    {
        selectedSourceIndices.remove(row);
        // empty = all selected again
    }
    else
    {
        selectedSourceIndices.insert(row);
    }

    refreshSourceHighlights();

    if (!selectedSourceIndices.isEmpty())
    {
        int firstIdx = *selectedSourceIndices.begin();
        if (firstIdx < allSources.size())
            emit mangaSourceClicked(allSources[firstIdx]);
    }

    // Re-search if query is active
    if (searchActive && !ui->lineEditFilter->text().trimmed().isEmpty())
        on_pushButtonFilter_clicked();
}

void HomeWidget::on_pushButtonFilter_clicked()
{
    // Invalidate AniList cache when searching (navigating away from home view)
    aniListCacheValid = false;

    QString query = ui->lineEditFilter->text().trimmed();

    if (query.isEmpty())
    {
        searchActive = false;
        searchResults.clear();
        refreshMangaListView();
        return;
    }

    searchActive = true;

    // Save to recent searches (keep last 5, no duplicates)
    recentSearches.removeAll(query);
    recentSearches.prepend(query);
    while (recentSearches.size() > 5)
        recentSearches.removeLast();
    saveRecentSearches();

    doLiveSearch(query);
}

void HomeWidget::doLiveSearch(const QString &query)
{
    // Bump generation to discard stale results
    searchGeneration++;
    int gen = searchGeneration;

    searchResults.clear();

    // Show static loading overlay (no animation - e-ink friendly)
    if (!searchSpinner)
        searchSpinner = new SpinnerWidget(ui->listViewMangas);
    searchSpinner->setFixedSize(ui->listViewMangas->size());
    searchSpinner->setMessage("Searching \"" + query + "\"...");
    searchSpinner->start();
    QApplication::processEvents();

    // Which sources to search
    QList<AbstractMangaSource *> sourcesToSearch;
    if (selectedSourceIndices.isEmpty())
        sourcesToSearch = allSources;
    else
        for (int idx : selectedSourceIndices)
            if (idx < allSources.size())
                sourcesToSearch.append(allSources[idx]);

    int successCount = 0;
    int failCount = 0;

    for (auto *source : sourcesToSearch)
    {
        try
        {
            auto searchResult = source->searchManga(query, 25);
            if (searchResult.isOk())
            {
                onSourceSearchComplete(gen, source, searchResult.unwrap());
                successCount++;
            }
            else
            {
                failCount++;
                qDebug() << "Search failed for" << source->name << ":" << searchResult.unwrapErr();
            }
        }
        catch (...)
        {
            failCount++;
            qDebug() << "Search crashed for" << source->name;
        }
    }

    // Sort results by relevance to the query
    auto queryLower = query.toLower().trimmed();
    auto queryWords = queryLower.split(' ', Qt::SkipEmptyParts);

    // Normalize: strip punctuation for comparison
    auto normalize = [](const QString &s) -> QString {
        return s.toLower().trimmed()
            .remove(QRegularExpression(R"([!?.,;:'\"-])"))
            .replace("-", " ")
            .replace("  ", " ")
            .trimmed();
    };

    auto queryNorm = normalize(query);
    auto queryNormWords = queryNorm.split(' ', Qt::SkipEmptyParts);

    auto scoreResult = [&queryLower, &queryWords, &queryNorm, &queryNormWords, &normalize]
                       (const SearchResult &r) -> int
    {
        auto matchTitle = [&](const QString &raw) -> int
        {
            if (raw.isEmpty()) return 0;
            auto t = raw.toLower().trimmed();
            auto tn = normalize(raw);
            int s = 0;

            // Exact match (with and without punctuation)
            if (t == queryLower || tn == queryNorm) s = qMax(s, 100);
            // Starts with
            else if (t.startsWith(queryLower) || tn.startsWith(queryNorm)) s = qMax(s, 80);
            // Contains
            else if (t.contains(queryLower) || tn.contains(queryNorm)) s = qMax(s, 60);

            // Word overlap on normalized form
            auto words = tn.split(' ', Qt::SkipEmptyParts);
            int overlap = 0;
            for (const auto &qw : queryNormWords)
                for (const auto &w : words)
                    if (w.contains(qw) || qw.contains(w)) { overlap++; break; }

            if (!queryNormWords.isEmpty())
                s += (overlap * 40) / queryNormWords.size();

            return s;
        };

        int score = qMax(matchTitle(r.title), matchTitle(r.altTitle));

        if (r.title.length() > 50) score -= 5;

        return score;
    };

    // Score all results
    QList<QPair<int, int>> scored;  // (score, index)
    for (int i = 0; i < searchResults.size(); i++)
        scored.append({scoreResult(searchResults[i]), i});

    std::sort(scored.begin(), scored.end(),
              [](const QPair<int,int> &a, const QPair<int,int> &b)
              { return a.first > b.first; });

    QList<SearchResult> sorted;
    for (const auto &s : scored)
        sorted.append(searchResults[s.second]);
    searchResults = sorted;

    // Deduplicate - same title from different sources, keep first (highest ranked)
    QSet<QString> seen;
    QList<SearchResult> deduped;
    for (const auto &sr : searchResults)
    {
        auto key = sr.title.toLower().trimmed();
        if (!seen.contains(key))
        {
            seen.insert(key);
            deduped.append(sr);
        }
    }
    searchResults = deduped;

    // Stop spinner
    if (searchSpinner)
        searchSpinner->stop();

    refreshMangaListView();

    if (searchResults.isEmpty())
    {
        auto model = dynamic_cast<QStringListModel *>(ui->listViewMangas->model());
        if (model)
        {
            if (failCount > 0 && successCount == 0)
                model->setStringList({"No results. All sources failed to respond."});
            else
                model->setStringList({"No results found for \"" + query + "\""});
        }
    }
}

void HomeWidget::onSourceSearchComplete(int generation, AbstractMangaSource *source,
                                         const MangaList &results)
{
    if (generation != searchGeneration)
        return;

    for (int i = 0; i < results.titles.size(); i++)
    {
        SearchResult sr;
        sr.title = results.titles[i];
        sr.altTitle = (i < results.altTitles.size()) ? results.altTitles[i] : "";
        sr.url = results.urls[i];
        sr.sourceName = source->name;
        sr.source = source;
        sr.absoluteUrl = results.absoluteUrls;
        sr.contentType = source->contentType;
        searchResults.append(sr);
    }

    refreshMangaListView();
}

void HomeWidget::on_pushButtonFilterClear_clicked()
{
    if (ui->lineEditFilter->text() != "")
        ui->lineEditFilter->clear();

    searchResults.clear();
    searchActive = false;
    if (altNamesLabel)
        altNamesLabel->hide();
    refreshMangaListView();
}

void HomeWidget::refreshMangaListView()
{
    if (!searchActive)
    {
        refreshHomeView();
        return;
    }

    auto *oldModel = ui->listViewMangas->model();
    auto *model = new QStringListModel(this);

    QStringList displayList;
    searchListOffset = 0;

    // Show alt names in the label under search bar
    QStringList altNames;
    QSet<QString> altSeen;
    for (const auto &sr : searchResults)
    {
        if (!sr.altTitle.isEmpty() && !altSeen.contains(sr.altTitle.toLower()))
        {
            altSeen.insert(sr.altTitle.toLower());
            altNames.append(sr.altTitle);
        }
    }
    if (!altNames.isEmpty() && altNamesLabel)
    {
        QString text = "Also: " + altNames.join(", ");
        if (text.length() > 60)
            text = text.left(57) + "...";
        altNamesLabel->setText(text);
        altNamesLabel->show();
    }
    else if (altNamesLabel)
    {
        altNamesLabel->hide();
    }

    for (const auto &sr : searchResults)
    {
        QString tag = sr.contentType == ContentLightNovel ? "LN" : "M";
        QString star = "";
        if (favManager)
        {
            for (const auto &fav : favManager->favorites)
            {
                if (fav.title.toLower() == sr.title.toLower())
                {
                    star = " *";
                    break;
                }
            }
        }

        // Show alt title if it matched the query better than the main title
        QString altInfo;
        if (!sr.altTitle.isEmpty())
        {
            auto queryL = ui->lineEditFilter->text().toLower().trimmed();
            auto titleL = sr.title.toLower();
            auto altL = sr.altTitle.toLower();
            // Show alt name if: query doesn't match main title well but matches alt
            bool mainMatch = titleL.contains(queryL) || queryL.contains(titleL);
            bool altMatch = altL.contains(queryL) || queryL.contains(altL);
            if (altMatch && !mainMatch)
                altInfo = " (aka " + sr.altTitle + ")";
            else if (!sr.altTitle.isEmpty() && sr.altTitle != sr.title)
                altInfo = " / " + sr.altTitle;
        }

        displayList.append("[" + tag + "|" + sr.sourceName + "] " + sr.title + altInfo + star);
    }

    model->setStringList(displayList);
    ui->listViewMangas->setModel(model);
    if (oldModel)
        oldModel->deleteLater();

    ui->listViewMangas->verticalScrollBar()->setValue(0);
}

void HomeWidget::on_listViewMangas_clicked(const QModelIndex &index)
{
    if (!index.isValid())
        return;

    // Handle search results
    if (searchActive)
    {
        int idx = index.row() - searchListOffset;
        if (idx < 0 || idx >= searchResults.size() || !index.model())
            return;

        const auto &sr = searchResults[idx];
        if (!sr.source)
            return;

        QString mangaUrl = sr.url;
        if (!sr.absoluteUrl)
            mangaUrl.prepend(sr.source->baseUrl);

        // If we have a pending AniList link, create it now
        if (!pendingLinkAniListTitle.isEmpty())
        {
            auto infoPath = CONF.mangainfodir(sr.source->name, sr.title);
            aniListLocalMap[pendingLinkAniListTitle] = {sr.source->name, sr.title, infoPath};
            saveAniListLocalMap();
            qDebug() << "Manual AniList link:" << pendingLinkAniListTitle << "->" << sr.source->name << sr.title;
            pendingLinkAniListTitle.clear();
        }

        emit mangaSourceClicked(sr.source);
        emit mangaClicked(mangaUrl, sr.title);
        return;
    }

    // Handle home view items by type
    int itemType = index.data(Qt::UserRole).toInt();

    switch (itemType)
    {
        case HeaderItem:
            break;

        case AniListItem:
        {
            auto title = index.data(Qt::UserRole + 1).toString();
            if (title == "__open_anilist__")
            {
                emit openAniListRequested();
            }
            else if (!title.isEmpty())
            {
                // Try direct open from local cache first
                if (aniListLocalMap.contains(title))
                {
                    auto &match = aniListLocalMap[title];
                    for (auto *ms : allSources)
                    {
                        if (ms->name == match.source)
                        {
                            emit mangaSourceClicked(ms);
                            emit mangaClicked(match.infoPath, match.dirName);
                            return;
                        }
                    }
                }
                // Not linked - search and set pending link
                pendingLinkAniListTitle = title;
                ui->lineEditFilter->setText(title);
                on_pushButtonFilter_clicked();
            }
            break;
        }

        case SearchItem:
        {
            auto query = index.data(Qt::UserRole + 1).toString();
            if (query == "__open_history__")
            {
                emit openHistoryRequested();
            }
            else if (!query.isEmpty())
            {
                ui->lineEditFilter->setText(query);
                on_pushButtonFilter_clicked();
            }
            break;
        }

        case DownloadedItem:
        {
            auto source = index.data(Qt::UserRole + 1).toString();
            auto title = index.data(Qt::UserRole + 2).toString();
            bool autoContinue = index.data(Qt::UserRole + 3).toBool();
            if (!source.isEmpty() && !title.isEmpty())
            {
                for (auto *ms : allSources)
                {
                    if (ms->name == source)
                    {
                        auto infoPath = CONF.mangainfodir(source, title) + "mangainfo.dat";
                        if (QFile::exists(infoPath))
                        {
                            emit mangaSourceClicked(ms);
                            emit mangaClicked(infoPath, title);

                            // Auto-continue: jump straight to reading after a short delay
                            if (autoContinue)
                            {
                                QTimer::singleShot(500, this, [this]() {
                                    emit readMangaContinueClicked();
                                });
                            }
                        }
                        break;
                    }
                }
            }
            break;
        }
    }
}

void HomeWidget::setFavoritesManager(FavoritesManager *fm)
{
    favManager = fm;
    // Refresh home view when favorites change so stars update immediately
    // The favorites widget emits favoritesCleared but we need toggleFavorite too
    // We'll just refresh on tab switch back to home
}

void HomeWidget::setAniList(AniList *al)
{
    aniList = al;
    if (aniList)
    {
        connect(aniList, &AniList::mangaListUpdated, this, [this]()
        {
            buildAniListLocalMap();
            refreshHomeView();
        });
        connect(aniList, &AniList::loginStatusChanged, this, [this](bool)
        {
            buildAniListLocalMap();
            refreshHomeView();
        });

        // Auto-refresh AniList every 30 minutes (battery friendly)
        aniListRefreshTimer = new QTimer(this);
        aniListRefreshTimer->setInterval(30 * 60 * 1000);
        connect(aniListRefreshTimer, &QTimer::timeout, this, [this]()
        {
            if (aniList && aniList->isLoggedIn())
            {
                qDebug() << "Auto-refreshing AniList...";
                aniList->fetchMangaList();
            }
        });
        aniListRefreshTimer->start();
    }
}

bool HomeWidget::isOffline() const
{
    return allSources.isEmpty();
}

void HomeWidget::pauseTimers()
{
    if (aniListRefreshTimer)
        aniListRefreshTimer->stop();
    if (bgMatchTimer)
        bgMatchTimer->stop();
}

void HomeWidget::resumeTimers()
{
    if (aniListRefreshTimer && aniList && aniList->isLoggedIn())
        aniListRefreshTimer->start();
}

void HomeWidget::refreshHomeView()
{
    if (searchActive)
        return;

    // Use QStandardItemModel so we can store item types
    auto *oldModel = ui->listViewMangas->model();
    auto *model = new QStandardItemModel(this);

    auto addHeader = [&](const QString &text)
    {
        auto *item = new QStandardItem(text);
        item->setData(HeaderItem, Qt::UserRole);
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        item->setForeground(QColor(100, 100, 100));
        auto f = item->font();
        f.setBold(true);
        f.setPointSize(9);
        item->setFont(f);
        model->appendRow(item);
    };

    // Downloaded manga
    QDir cacheDir(CONF.cacheDir);
    struct DownloadedEntry { QString title; QString source; QString path;
                             int chapters; int progressCh; int progressPg; };
    QList<DownloadedEntry> downloaded;

    for (const auto &sourceDir : cacheDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
    {
        if (sourceDir == "mangalists")
            continue;
        QDir source(CONF.cacheDir + sourceDir);
        for (const auto &mangaDir : source.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        {
            auto mangaPath = source.absolutePath() + "/" + mangaDir;
            auto imgDir = mangaPath + "/images";
            if (!QDir(imgDir).exists() || QDir(imgDir).entryList(QDir::Files).isEmpty())
                continue;

            DownloadedEntry de;
            de.title = mangaDir;
            de.source = sourceDir;
            de.path = mangaPath;
            de.progressCh = 0;
            de.progressPg = 0;

            // Count chapters
            QSet<int> chNums;
            for (const auto &f : QDir(imgDir).entryList(QDir::Files))
            {
                auto parts = f.split('_');
                if (!parts.isEmpty())
                    chNums.insert(parts[0].toInt());
            }
            de.chapters = chNums.size();

            // Read progress
            auto progressPath = mangaPath + "/progress.dat";
            if (QFile::exists(progressPath))
            {
                QFile pf(progressPath);
                if (pf.open(QIODevice::ReadOnly))
                {
                    QDataStream in(&pf);
                    in >> de.progressCh >> de.progressPg;
                    pf.close();
                }
            }

            downloaded.append(de);
        }
    }

    // Split downloaded into: actively reading vs just cached
    QList<DownloadedEntry> activelyReading;
    QList<DownloadedEntry> justCached;
    for (const auto &d : downloaded)
    {
        if (d.progressCh > 0 || d.progressPg > 0)
            activelyReading.append(d);
        else
            justCached.append(d);
    }

    // === CONTINUE READING (only items with actual progress) ===
    if (!activelyReading.isEmpty())
    {
        addHeader("CONTINUE READING");
        for (const auto &d : activelyReading)
        {
            QString text = "  " + d.title + "  Ch." +
                           QString::number(d.progressCh + 1) + " Pg." +
                           QString::number(d.progressPg + 1);

            auto *item = new QStandardItem(text);
            item->setData(DownloadedItem, Qt::UserRole);
            item->setData(d.source, Qt::UserRole + 1);
            item->setData(d.title, Qt::UserRole + 2);
            item->setData(true, Qt::UserRole + 3);  // auto-continue flag
            model->appendRow(item);
        }
    }

    // === RECENT SEARCHES (top 3) ===
    if (!recentSearches.isEmpty())
    {
        addHeader("RECENT SEARCHES");
        int shown = qMin(3, recentSearches.size());
        for (int i = 0; i < shown; i++)
        {
            auto *item = new QStandardItem("  " + recentSearches[i]);
            item->setData(SearchItem, Qt::UserRole);
            item->setData(recentSearches[i], Qt::UserRole + 1);
            model->appendRow(item);
        }
        {
            auto *seeAll = new QStandardItem("  View history...");
            seeAll->setData(SearchItem, Qt::UserRole);
            seeAll->setData("__open_history__", Qt::UserRole + 1);
            auto f = seeAll->font();
            f.setItalic(true);
            seeAll->setFont(f);
            model->appendRow(seeAll);
        }
    }

    // === READING ON ANILIST (top 3) ===
    if (aniList && aniList->isLoggedIn())
    {
        if (!aniListCacheValid)
        {
            cachedAniListReading.clear();
            auto reading = aniList->entriesByStatus(1);
            for (const auto &e : reading)
            {
                QString info = e.title + "  Ch." + QString::number(e.progress);
                if (e.totalChapters > 0) info += "/" + QString::number(e.totalChapters);
                cachedAniListReading.append(info);
            }
            aniListCacheValid = true;
        }

        if (!cachedAniListReading.isEmpty())
        {
            addHeader("READING ON ANILIST");
            auto reading = aniList->entriesByStatus(1);
            int shown = qMin(3, reading.size());
            for (int i = 0; i < shown; i++)
            {
                const auto &e = reading[i];
                QString text = "  " + e.title + "  Ch." + QString::number(e.progress);
                if (e.totalChapters > 0) text += "/" + QString::number(e.totalChapters);

                auto *item = new QStandardItem(text);
                item->setData(AniListItem, Qt::UserRole);
                item->setData(e.title, Qt::UserRole + 1);
                model->appendRow(item);
            }
            {
                auto *seeAll = new QStandardItem("  View all on AniList...");
                seeAll->setData(AniListItem, Qt::UserRole);
                seeAll->setData("__open_anilist__", Qt::UserRole + 1);
                auto f = seeAll->font();
                f.setItalic(true);
                seeAll->setFont(f);
                model->appendRow(seeAll);
            }
        }
    }

    // === FOR YOU (AniList Planning list + cached unread) ===
    {
        QSet<QString> shownTitles;
        for (const auto &d : activelyReading)
            shownTitles.insert(d.title.toLower());
        if (favManager)
            for (const auto &fav : favManager->favorites)
                shownTitles.insert(fav.title.toLower());
        // Also exclude anything in AniList Reading
        if (aniList && aniList->isLoggedIn())
            for (const auto &e : aniList->entriesByStatus(1))
                shownTitles.insert(e.title.toLower());

        bool headerAdded = false;

        // AniList Planning entries (manga user wants to read)
        if (aniList && aniList->isLoggedIn())
        {
            auto planning = aniList->entriesByStatus(2);  // Planning
            int shown = 0;
            for (const auto &e : planning)
            {
                if (shownTitles.contains(e.title.toLower()))
                    continue;
                if (!headerAdded) { addHeader("FOR YOU"); headerAdded = true; }

                QString text = "  " + e.title;
                if (e.totalChapters > 0) text += "  (" + QString::number(e.totalChapters) + " ch)";

                auto *item = new QStandardItem(text);
                item->setData(AniListItem, Qt::UserRole);
                item->setData(e.title, Qt::UserRole + 1);
                model->appendRow(item);

                shownTitles.insert(e.title.toLower());
                if (++shown >= 5) break;
            }
        }

        // Cached but unread content
        for (const auto &d : justCached)
        {
            if (shownTitles.contains(d.title.toLower()))
                continue;
            if (!headerAdded) { addHeader("FOR YOU"); headerAdded = true; }

            QString text = "  " + d.title + "  (" + QString::number(d.chapters) + " ch)";

            auto *item = new QStandardItem(text);
            item->setData(DownloadedItem, Qt::UserRole);
            item->setData(d.source, Qt::UserRole + 1);
            item->setData(d.title, Qt::UserRole + 2);
            model->appendRow(item);

            shownTitles.insert(d.title.toLower());
        }
    }

    if (model->rowCount() == 0)
    {
        auto *item = new QStandardItem("Search for manga or light novels above");
        item->setData(HeaderItem, Qt::UserRole);
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        model->appendRow(item);
    }

    // Safely swap model - disconnect signals first
    auto *prev = ui->listViewMangas->model();
    ui->listViewMangas->setModel(model);
    if (prev && prev != model)
        prev->deleteLater();

    ui->listViewMangas->verticalScrollBar()->setValue(0);
}

void HomeWidget::showRecentSearches()
{
    if (searchActive || recentSearches.isEmpty())
        return;

    // Use QStandardItemModel so items have types and are clickable
    auto *oldModel = ui->listViewMangas->model();
    auto *model = new QStandardItemModel(this);

    auto *header = new QStandardItem("--- Recent Searches ---");
    header->setData(HeaderItem, Qt::UserRole);
    header->setSelectable(false);
    model->appendRow(header);

    for (const auto &s : recentSearches)
    {
        auto *item = new QStandardItem(s);
        item->setData(SearchItem, Qt::UserRole);
        item->setData(s, Qt::UserRole + 1);
        model->appendRow(item);
    }

    ui->listViewMangas->setModel(model);
    if (oldModel)
        oldModel->deleteLater();
}

void HomeWidget::showOfflineReads()
{
    auto model = dynamic_cast<QStringListModel *>(ui->listViewMangas->model());
    if (!model)
        return;
    QStringList display;

    // Scan cache directories for downloaded manga
    QDir cacheDir(CONF.cacheDir);
    for (const auto &sourceDir : cacheDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
    {
        QDir source(CONF.cacheDir + sourceDir);
        for (const auto &mangaDir : source.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        {
            auto infoPath = source.absolutePath() + "/" + mangaDir + "/mangainfo.dat";
            if (QFile::exists(infoPath))
                display.append("[Offline] " + mangaDir);
        }
    }

    if (display.isEmpty())
        display.append("No offline content available.");

    model->setStringList(display);
}

static QString normalizeName(const QString &s)
{
    return s.toLower().trimmed()
        .remove(QRegularExpression(R"([!?.,;:'\"\-])"))
        .replace("-", " ").replace("_", " ")
        .replace(QRegularExpression(R"(\s+)"), " ").trimmed();
}

static QString cleanTitle(const QString &t)
{
    // Strip common suffixes and noise for better matching
    auto s = t.toLower().trimmed();
    s.remove(QRegularExpression(R"(\(manga\)|\(light novel\)|\(ln\)|\(wn\))", QRegularExpression::CaseInsensitiveOption));
    s.remove(QRegularExpression(R"(\bseason\s*\d+|\bpart\s*\d+|\bvol\.?\s*\d+)", QRegularExpression::CaseInsensitiveOption));
    s.remove(QRegularExpression(R"([^\w\s])"));
    s = s.simplified();
    return s;
}

bool HomeWidget::tryMatch(const AniListEntry &entry)
{
    if (aniListLocalMap.contains(entry.title))
        return true;

    auto normTitle = normalizeName(entry.title);
    auto normRomaji = normalizeName(entry.titleRomaji);
    auto cleanEn = cleanTitle(entry.title);
    auto cleanRo = cleanTitle(entry.titleRomaji);

    // Priority 1: Check favorites first
    if (favManager)
    {
        for (const auto &fav : favManager->favorites)
        {
            auto favNorm = normalizeName(fav.title);
            auto favClean = cleanTitle(fav.title);
            if (favNorm == normTitle || favNorm == normRomaji ||
                favClean == cleanEn || (!cleanRo.isEmpty() && favClean == cleanRo) ||
                (!normTitle.isEmpty() && normTitle.length() >= 4 &&
                 (favNorm.contains(normTitle) || normTitle.contains(favNorm))))
            {
                auto infoPath = CONF.mangainfodir(fav.hostname, fav.title);
                aniListLocalMap[entry.title] = {fav.hostname, fav.title, infoPath};
                qDebug() << "AniList linked via favorite:" << entry.title << "->" << fav.title;
                return true;
            }
        }
    }

    // Track best match if we find multiple candidates
    struct Match { int score; CachedDir dir; };
    QList<Match> candidates;

    for (const auto &c : cachedDirsForMatching)
    {
        auto cleanDir = cleanTitle(c.dirName);

        // Exact normalized match (highest priority)
        if (c.normName == normTitle || (!normRomaji.isEmpty() && c.normName == normRomaji))
        {
            candidates.append({100, c});
            continue;
        }

        // Clean title exact match
        if (cleanDir == cleanEn || (!cleanRo.isEmpty() && cleanDir == cleanRo))
        {
            candidates.append({95, c});
            continue;
        }

        // Substring match (both directions)
        if ((!normTitle.isEmpty() && normTitle.length() >= 4 &&
             (c.normName.contains(normTitle) || normTitle.contains(c.normName))) ||
            (!normRomaji.isEmpty() && normRomaji.length() >= 4 &&
             (c.normName.contains(normRomaji) || normRomaji.contains(c.normName))))
        {
            candidates.append({80, c});
            continue;
        }

        // Word overlap >= 40% (lowered from 50% for more matches)
        auto dirWords = cleanDir.split(' ', Qt::SkipEmptyParts).toSet();
        auto titleWords = cleanEn.split(' ', Qt::SkipEmptyParts).toSet();
        if (dirWords.size() >= 2 && titleWords.size() >= 2)
        {
            auto common = dirWords & titleWords;
            int minSz = qMin(dirWords.size(), titleWords.size());
            if (minSz > 0)
            {
                int pct = common.size() * 100 / minSz;
                if (pct >= 40)
                    candidates.append({pct, c});
            }
        }

        // Romaji word overlap
        if (!cleanRo.isEmpty())
        {
            auto romajiWords = cleanRo.split(' ', Qt::SkipEmptyParts).toSet();
            if (dirWords.size() >= 2 && romajiWords.size() >= 2)
            {
                auto common = dirWords & romajiWords;
                int minSz = qMin(dirWords.size(), romajiWords.size());
                if (minSz > 0)
                {
                    int pct = common.size() * 100 / minSz;
                    if (pct >= 40)
                        candidates.append({pct, c});
                }
            }
        }
    }

    if (candidates.isEmpty())
        return false;

    // Sort by score, pick best
    std::sort(candidates.begin(), candidates.end(),
              [](const Match &a, const Match &b) { return a.score > b.score; });

    // Store ALL matches for multi-source support
    for (const auto &m : candidates)
        aniListAllLinks.insert(entry.title, {m.dir.source, m.dir.dirName, m.dir.path});

    // Pick best match with actual chapters for the primary link
    for (const auto &m : candidates)
    {
        try
        {
            auto info = MangaInfo::deserialize(nullptr, m.dir.path + "/mangainfo.dat");
            if (!info.isNull() && info->chapters.count() > 0)
            {
                aniListLocalMap[entry.title] = {m.dir.source, m.dir.dirName, m.dir.path};
                return true;
            }
        }
        catch (...) {}
    }

    // Fallback: use best match even if 0 chapters
    aniListLocalMap[entry.title] = {candidates.first().dir.source,
                                    candidates.first().dir.dirName,
                                    candidates.first().dir.path};
    return true;
}

void HomeWidget::buildAniListLocalMap()
{
    // Preserve manually-created links, remove stale auto-links
    aniListAllLinks.clear();
    QMap<QString, LocalMangaMatch> preserved;
    for (auto it = aniListLocalMap.begin(); it != aniListLocalMap.end(); ++it)
    {
        // Keep if the cached info file still exists
        auto checkPath = it.value().infoPath;
        if (!checkPath.endsWith("mangainfo.dat"))
            checkPath += "/mangainfo.dat";
        if (QFile::exists(checkPath))
            preserved[it.key()] = it.value();
    }
    aniListLocalMap = preserved;

    if (!aniList || !aniList->isLoggedIn())
        return;

    // Build cached directories list (done once)
    cachedDirsForMatching.clear();
    QDir cacheDir(CONF.cacheDir);
    for (const auto &sourceDir : cacheDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
    {
        if (sourceDir == "mangalists") continue;
        QDir source(CONF.cacheDir + sourceDir);
        for (const auto &mangaDir : source.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        {
            auto infoPath = source.absolutePath() + "/" + mangaDir + "/mangainfo.dat";
            if (QFile::exists(infoPath))
                cachedDirsForMatching.append({sourceDir, mangaDir, normalizeName(mangaDir), infoPath});
        }
    }

    if (cachedDirsForMatching.isEmpty())
        return;

    // Phase 1: Match Reading entries immediately (fast, priority)
    auto reading = aniList->entriesByStatus(1);
    for (const auto &e : reading)
        tryMatch(e);

    saveAniListLocalMap();

    // Phase 2: Queue remaining entries for background matching
    startBackgroundMatching();
}

void HomeWidget::startBackgroundMatching()
{
    pendingMatchEntries.clear();

    // Queue all non-Reading entries
    int statuses[] = {2, 5, 3, 4, 6};  // Planning, Paused, Completed, Dropped, Repeating
    for (int s : statuses)
    {
        auto entries = aniList->entriesByStatus(s);
        for (const auto &e : entries)
        {
            if (!aniListLocalMap.contains(e.title))
                pendingMatchEntries.append(e);
        }
    }

    if (pendingMatchEntries.isEmpty())
        return;

    // Process in small batches via timer to not block UI
    if (!bgMatchTimer)
    {
        bgMatchTimer = new QTimer(this);
        bgMatchTimer->setInterval(100);  // 100ms between batches
        connect(bgMatchTimer, &QTimer::timeout, this, &HomeWidget::matchNextBatch);
    }
    bgMatchTimer->start();
}

void HomeWidget::matchNextBatch()
{
    if (pendingMatchEntries.isEmpty())
    {
        if (bgMatchTimer)
            bgMatchTimer->stop();
        saveAniListLocalMap();
        return;
    }

    // Process 5 entries per batch
    int batch = qMin(5, pendingMatchEntries.size());
    for (int i = 0; i < batch; i++)
    {
        auto entry = pendingMatchEntries.takeFirst();
        tryMatch(entry);
    }
}

void HomeWidget::saveAniListLocalMap()
{
    QFile file(CONF.cacheDir + "anilist_links.dat");
    if (!file.open(QIODevice::WriteOnly))
        return;
    QDataStream out(&file);
    out << (int)aniListLocalMap.size();
    for (auto it = aniListLocalMap.begin(); it != aniListLocalMap.end(); ++it)
        out << it.key() << it.value().source << it.value().dirName << it.value().infoPath;
    file.close();
}

void HomeWidget::loadAniListLocalMap()
{
    QFile file(CONF.cacheDir + "anilist_links.dat");
    if (!file.open(QIODevice::ReadOnly))
        return;
    try
    {
        QDataStream in(&file);
        int count;
        in >> count;
        count = qBound(0, count, 5000);
        for (int i = 0; i < count && !in.atEnd(); i++)
        {
            QString key, source, dirName, path;
            in >> key >> source >> dirName >> path;
            if (in.status() == QDataStream::Ok && !key.isEmpty())
                aniListLocalMap[key] = {source, dirName, path};
        }
    }
    catch (...) {}
    file.close();
}

void HomeWidget::clearAniListLocalMap()
{
    aniListLocalMap.clear();
    QFile::remove(CONF.cacheDir + "anilist_links.dat");
}

void HomeWidget::resetAniListLinks()
{
    clearAniListLocalMap();
    buildAniListLocalMap();
    refreshHomeView();
}

QStringList HomeWidget::aniListLinkDescriptions() const
{
    QStringList result;
    for (auto it = aniListLocalMap.begin(); it != aniListLocalMap.end(); ++it)
        result.append(it.key() + " -> " + it.value().dirName);
    return result;
}

void HomeWidget::saveRecentSearches()
{
    QFile file(CONF.cacheDir + "recentsearches.dat");
    if (!file.open(QIODevice::WriteOnly))
        return;
    QDataStream out(&file);
    out << recentSearches;
    file.close();
}

void HomeWidget::loadRecentSearches()
{
    QFile file(CONF.cacheDir + "recentsearches.dat");
    if (!file.open(QIODevice::ReadOnly))
        return;
    QDataStream in(&file);
    in >> recentSearches;
    file.close();
}
