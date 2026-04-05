#include "downloadqueuewidget.h"

#include <QDirIterator>
#include <QMessageBox>
#include <QStorageInfo>

#include "homewidget.h"
#include "staticsettings.h"

DownloadQueueWidget::DownloadQueueWidget(QWidget *parent)
    : QWidget(parent), showingCached(false)
{
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(4);
    layout->setContentsMargins(8, 6, 8, 6);

    // Header
    auto *headerRow = new QHBoxLayout();
    backBtn = new QPushButton("< Back", this);
    backBtn->setFixedHeight(SIZES.buttonSize);
    backBtn->setProperty("type", "borderless");
    connect(backBtn, &QPushButton::clicked, this, &DownloadQueueWidget::backClicked);

    headerLabel = new QLabel("Downloads", this);
    headerLabel->setStyleSheet("font-weight: bold;");
    headerLabel->setAlignment(Qt::AlignCenter);

    headerRow->addWidget(backBtn);
    headerRow->addWidget(headerLabel, 1);
    headerRow->addSpacing(70);
    layout->addLayout(headerRow);

    // Progress bar
    activeProgress = new QProgressBar(this);
    activeProgress->setRange(0, 100);
    activeProgress->setFixedHeight(SIZES.batteryIconHeight);
    activeProgress->setTextVisible(true);
    activeProgress->hide();
    layout->addWidget(activeProgress);

    statusLabel = new QLabel("No downloads", this);
    statusLabel->setStyleSheet("color: #666;");
    statusLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(statusLabel);

    // Storage + sleep download info
    storageLabel = new QLabel(this);
    storageLabel->setStyleSheet("color: #888;");
    storageLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(storageLabel);
    updateStorageInfo();

    sleepDownloadLabel = new QLabel(this);
    sleepDownloadLabel->setStyleSheet("color: #888;");
    sleepDownloadLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(sleepDownloadLabel);

    // Job list
    jobList = new QListWidget(this);
    jobList->setStyleSheet(
        "QListWidget { border: none; }"
        "QListWidget::item { padding: 8px 6px; border-bottom: 1px solid #ddd; color: #111; }"
        "QListWidget::item:selected { background: transparent; color: #111; }"
        "QListWidget::item:checked { background: transparent; color: #111; }"
        "QListWidget::indicator { width: 28px; height: 28px; }"
        "QListWidget::indicator:unchecked { image: url(:/images/icons/checkbox-unchecked.png); }"
        "QListWidget::indicator:checked { image: url(:/images/icons/checkbox-checked.png); }");
    jobList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    jobList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    jobList->setFocusPolicy(Qt::NoFocus);
    activateScroller(jobList);
    layout->addWidget(jobList, 1);

    // Bottom buttons
    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(4);

    cancelBtn = new QPushButton("Cancel All", this);
    cancelBtn->setFixedHeight(SIZES.buttonSize);
    connect(cancelBtn, &QPushButton::clicked, this, [this]()
    {
        for (auto &j : jobs)
            if (j.state == DownloadJob::Active || j.state == DownloadJob::Queued)
                j.state = DownloadJob::Cancelled;
        activeProgress->hide();
        emit cancelRequested();
        refreshList();
    });

    clearBtn = new QPushButton("Clear", this);
    clearBtn->setFixedHeight(SIZES.buttonSize);
    connect(clearBtn, &QPushButton::clicked, this, [this]()
    {
        if (showingCached)
            return;
        jobs.erase(std::remove_if(jobs.begin(), jobs.end(),
                   [](const DownloadJob &j) {
                       return j.state == DownloadJob::Completed ||
                              j.state == DownloadJob::Failed ||
                              j.state == DownloadJob::Cancelled;
                   }), jobs.end());
        refreshList();
    });

    showCachedBtn = new QPushButton("Downloaded", this);
    showCachedBtn->setFixedHeight(SIZES.buttonSize);
    connect(showCachedBtn, &QPushButton::clicked, this, [this]()
    {
        showingCached = !showingCached;
        showCachedBtn->setText(showingCached ? "Queue" : "Downloaded");
        deleteSelectedBtn->setVisible(showingCached);
        cancelBtn->setVisible(!showingCached);
        clearBtn->setVisible(!showingCached);
        if (showingCached)
            refreshCachedList();
        else
            refreshList();
    });

    deleteSelectedBtn = new QPushButton("Delete Selected", this);
    deleteSelectedBtn->setFixedHeight(SIZES.buttonSize);
    deleteSelectedBtn->setStyleSheet("color: #900;");
    deleteSelectedBtn->hide();
    connect(deleteSelectedBtn, &QPushButton::clicked, this, [this]()
    {
        // Count selected items
        int count = 0;
        for (int i = 0; i < jobList->count(); i++)
            if (jobList->item(i)->checkState() == Qt::Checked)
                count++;

        if (count == 0)
            return;

        // Confirm deletion
        QMessageBox confirm(this);
        confirm.setWindowTitle("Delete");
        confirm.setText(QString("Delete %1 selected item%2?\n\nThis cannot be undone.")
                            .arg(count).arg(count > 1 ? "s" : ""));
        confirm.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        confirm.setDefaultButton(QMessageBox::No);
        if (confirm.exec() != QMessageBox::Yes)
            return;

        // Delete all checked items
        for (int i = jobList->count() - 1; i >= 0; i--)
        {
            auto *item = jobList->item(i);
            if (item->checkState() == Qt::Checked)
            {
                auto path = item->data(Qt::UserRole).toString();
                if (!path.isEmpty())
                {
                    QFileInfo fi(path);
                    if (fi.isDir())
                        QDir(path).removeRecursively();
                    else
                        QFile::remove(path);
                }
            }
        }
        refreshCachedList();
    });

    auto *selectAllBtn = new QPushButton("All", this);
    selectAllBtn->setFixedHeight(SIZES.buttonSize);
    selectAllBtn->hide();
    connect(selectAllBtn, &QPushButton::clicked, this, [this]()
    {
        bool allChecked = true;
        for (int i = 0; i < jobList->count(); i++)
        {
            auto *item = jobList->item(i);
            if ((item->flags() & Qt::ItemIsUserCheckable) && item->checkState() != Qt::Checked)
                allChecked = false;
        }
        for (int i = 0; i < jobList->count(); i++)
        {
            auto *item = jobList->item(i);
            if (item->flags() & Qt::ItemIsUserCheckable)
                item->setCheckState(allChecked ? Qt::Unchecked : Qt::Checked);
        }
    });

    // Store for visibility toggling
    connect(showCachedBtn, &QPushButton::clicked, this, [=]()
    {
        selectAllBtn->setVisible(showingCached);
    });

    auto *moveUpBtn = new QPushButton("Up", this);
    moveUpBtn->setFixedHeight(SIZES.buttonSize);
    moveUpBtn->setFixedWidth(SIZES.buttonSize * 2);
    connect(moveUpBtn, &QPushButton::clicked, this, [this]()
    {
        int row = jobList->currentRow();
        if (row > 0)
        {
            moveJobUp(row);
            jobList->setCurrentRow(row - 1);
        }
    });

    auto *moveDownBtn = new QPushButton("Dn", this);
    moveDownBtn->setFixedHeight(SIZES.buttonSize);
    moveDownBtn->setFixedWidth(SIZES.buttonSize * 2);
    connect(moveDownBtn, &QPushButton::clicked, this, [this]()
    {
        int row = jobList->currentRow();
        if (row >= 0 && row < jobs.size() - 1)
        {
            moveJobDown(row);
            jobList->setCurrentRow(row + 1);
        }
    });

    // Toggle visibility with queue/cached view
    connect(showCachedBtn, &QPushButton::clicked, this, [=]()
    {
        moveUpBtn->setVisible(!showingCached);
        moveDownBtn->setVisible(!showingCached);
    });

    btnRow->addWidget(moveUpBtn);
    btnRow->addWidget(moveDownBtn);
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(clearBtn);
    btnRow->addWidget(selectAllBtn);
    btnRow->addWidget(deleteSelectedBtn);
    btnRow->addWidget(showCachedBtn);
    layout->addLayout(btnRow);
}

void DownloadQueueWidget::addJob(const QString &title, const QString &source,
                                  int from, int to, bool toDevice, bool isLN)
{
    DownloadJob job;
    job.title = title;
    job.source = source;
    job.fromChapter = from;
    job.toChapter = to;
    job.toDevice = toDevice;
    job.isLightNovel = isLN;
    job.state = DownloadJob::Queued;
    jobs.append(job);
    refreshList();
}

void DownloadQueueWidget::markJobActive(const QString &title)
{
    for (auto &j : jobs)
    {
        if (j.state == DownloadJob::Queued && j.title == title)
        {
            j.state = DownloadJob::Active;
            break;
        }
    }
    activeProgress->setValue(0);
    activeProgress->setFormat("Starting...");
    activeProgress->show();
    if (!showingCached)
        refreshList();
}

void DownloadQueueWidget::moveJobUp(int index)
{
    if (index <= 0 || index >= jobs.size())
        return;
    // Only swap two queued jobs — don't move past active/completed
    if (jobs[index].state != DownloadJob::Queued ||
        jobs[index - 1].state != DownloadJob::Queued)
        return;
    jobs.swapItemsAt(index, index - 1);
    if (!showingCached)
        refreshList();
}

void DownloadQueueWidget::moveJobDown(int index)
{
    if (index < 0 || index >= jobs.size() - 1)
        return;
    if (jobs[index].state != DownloadJob::Queued ||
        jobs[index + 1].state != DownloadJob::Queued)
        return;
    jobs.swapItemsAt(index, index + 1);
    if (!showingCached)
        refreshList();
}

void DownloadQueueWidget::updateActiveJob(int completedPages, int totalPages, int currentChapter)
{
    for (auto &j : jobs)
    {
        if (j.state == DownloadJob::Active)
        {
            j.completedPages = completedPages;
            j.totalPages = totalPages;
            j.currentChapter = currentChapter;
            break;
        }
    }

    if (totalPages > 0)
    {
        int pct = completedPages * 100 / totalPages;
        activeProgress->setValue(pct);
        activeProgress->setFormat(QString("%1% (%2/%3)").arg(pct).arg(completedPages).arg(totalPages));
        activeProgress->show();
    }

    if (!showingCached)
        refreshList();
}

void DownloadQueueWidget::jobCompleted(const QString &title)
{
    for (auto &j : jobs)
    {
        if ((j.state == DownloadJob::Active || j.state == DownloadJob::Queued) &&
            j.title == title)
        {
            j.state = DownloadJob::Completed;
            break;
        }
    }
    activeProgress->hide();
    if (!showingCached)
        refreshList();
}

void DownloadQueueWidget::jobFailed(const QString &title, const QString &error)
{
    for (auto &j : jobs)
    {
        if ((j.state == DownloadJob::Active || j.state == DownloadJob::Queued) &&
            j.title == title)
        {
            j.state = DownloadJob::Failed;
            j.errorMsg = error;
            break;
        }
    }
    activeProgress->hide();
    if (!showingCached)
        refreshList();
}

void DownloadQueueWidget::jobCancelled(const QString &title)
{
    for (auto &j : jobs)
    {
        if ((j.state == DownloadJob::Active || j.state == DownloadJob::Queued) &&
            (title.isEmpty() || j.title == title))
        {
            j.state = DownloadJob::Cancelled;
        }
    }
    activeProgress->hide();
    if (!showingCached)
        refreshList();
}

void DownloadQueueWidget::refreshList()
{
    jobList->clear();

    int activeCount = 0, queuedCount = 0, doneCount = 0;

    for (const auto &j : jobs)
    {
        QString state;
        switch (j.state)
        {
            case DownloadJob::Queued: state = "Queued"; queuedCount++; break;
            case DownloadJob::Active: state = "Downloading"; activeCount++; break;
            case DownloadJob::Completed: state = "Done"; doneCount++; break;
            case DownloadJob::Failed: state = "Failed"; doneCount++; break;
            case DownloadJob::Cancelled: state = "Cancelled"; doneCount++; break;
        }

        QString dest = j.toDevice ? "Device" : "App";
        QString text = j.title + "\n  " + state + " | Ch." +
                       QString::number(j.fromChapter + 1) + "-" +
                       QString::number(j.toChapter + 1) + " | " + dest;

        if (j.state == DownloadJob::Active && j.totalPages > 0)
            text += " | " + QString::number(j.completedPages) + "/" + QString::number(j.totalPages);
        if (j.state == DownloadJob::Failed && !j.errorMsg.isEmpty())
            text += "\n  " + j.errorMsg;

        auto *item = new QListWidgetItem(text, jobList);
        if (j.state == DownloadJob::Completed)
            item->setForeground(QColor(80, 80, 80));
        else if (j.state == DownloadJob::Failed)
            item->setForeground(QColor(160, 40, 40));
    }

    if (jobs.isEmpty())
    {
        statusLabel->setText("No downloads");
        activeProgress->hide();
    }
    else
        statusLabel->setText(QString("%1 active, %2 queued, %3 done")
                                 .arg(activeCount).arg(queuedCount).arg(doneCount));

    headerLabel->setText(QString("Downloads (%1)").arg(jobs.size()));
    updateStorageInfo();
}

QList<CachedManga> DownloadQueueWidget::scanCachedManga()
{
    QList<CachedManga> result;
    QDir cacheDir(CONF.cacheDir);

    for (const auto &sourceDir : cacheDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
    {
        if (sourceDir == "mangalists")
            continue;

        QDir source(CONF.cacheDir + sourceDir);
        for (const auto &mangaDir : source.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        {
            auto mangaPath = source.absolutePath() + "/" + mangaDir;
            if (!QFile::exists(mangaPath + "/mangainfo.dat"))
                continue;

            CachedManga cm;
            cm.source = sourceDir;
            cm.title = mangaDir;
            cm.path = mangaPath;
            cm.chapters = 0;
            cm.sizeMB = 0;
            cm.pages = 0;

            // Count chapters, pages, and size from images dir
            QDir imgDir(mangaPath + "/images");
            if (imgDir.exists())
            {
                QSet<int> chNums;
                auto files = imgDir.entryList(QDir::Files);
                cm.pages = files.size();
                for (const auto &f : files)
                {
                    auto parts = f.split('_');
                    if (!parts.isEmpty())
                        chNums.insert(parts[0].toInt());
                    cm.sizeMB += QFileInfo(imgDir.absoluteFilePath(f)).size();
                }
                cm.chapters = chNums.size();
                cm.sizeMB /= (1024 * 1024);
            }

            // Read progress if available
            cm.progressChapter = 0;
            cm.progressPage = 0;
            auto progressPath = mangaPath + "/progress.dat";
            if (QFile::exists(progressPath))
            {
                QFile pf(progressPath);
                if (pf.open(QIODevice::ReadOnly))
                {
                    QDataStream in(&pf);
                    int ch = 0, pg = 0, numCh = 0, numPg = 0;
                    in >> ch >> pg >> numCh >> numPg;
                    cm.progressChapter = ch;
                    cm.progressPage = pg;
                    pf.close();
                }
            }

            result.append(cm);
        }
    }

    return result;
}

void DownloadQueueWidget::refreshCachedList()
{
    jobList->clear();

    auto cached = scanCachedManga();
    qint64 totalSize = 0;

    for (const auto &cm : cached)
    {
        totalSize += cm.sizeMB;
        bool hasImages = (cm.chapters > 0 && cm.pages > 0);
        bool isCachedListing = (cm.chapters == 0 && cm.pages == 0);

        QString text = cm.title;
        if (hasImages)
        {
            text += "\n  " + cm.source +
                    " | " + QString::number(cm.chapters) + " ch" +
                    " | " + QString::number(cm.pages) + " pg" +
                    " | " + QString::number(cm.sizeMB) + " MB";

            if (cm.progressChapter > 0 || cm.progressPage > 0)
                text += "\n  Reading: Ch." + QString::number(cm.progressChapter + 1) +
                        " Pg." + QString::number(cm.progressPage + 1);
        }
        else
        {
            text += "\n  " + cm.source + " | Cached listing";
        }

        auto *item = new QListWidgetItem(text, jobList);
        item->setData(Qt::UserRole, cm.path);
        item->setData(Qt::UserRole + 1, cm.source);
        item->setData(Qt::UserRole + 2, cm.title);

        if (isCachedListing)
            item->setForeground(QColor(140, 140, 140));

        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
    }

    // Device exports section
    QDir exportDir(CONF.exportDir());
    if (exportDir.exists())
    {
        auto exports = exportDir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
        if (!exports.isEmpty())
        {
            auto *header = new QListWidgetItem("-- Exported to Device --", jobList);
            header->setForeground(QColor(80, 80, 80));
            header->setFlags(header->flags() & ~Qt::ItemIsUserCheckable);

            for (const auto &f : exports)
            {
                auto fullPath = exportDir.absoluteFilePath(f);
                QFileInfo fi(fullPath);
                qint64 sz = fi.isDir() ? 0 : fi.size() / (1024 * 1024);
                if (fi.isDir())
                {
                    QDirIterator it(fullPath, QDir::Files, QDirIterator::Subdirectories);
                    while (it.hasNext()) { it.next(); sz += it.fileInfo().size(); }
                    sz /= (1024 * 1024);
                }

                auto *item = new QListWidgetItem(f + " (" + QString::number(sz) + " MB)", jobList);
                item->setData(Qt::UserRole, fullPath);
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(Qt::Unchecked);
            }
        }
    }

    // AniList cached links section
    if (homeWidget)
    {
        int linkCount = homeWidget->aniListLinkCount();
        if (linkCount > 0)
        {
            // Calculate approximate cache file size
            QFileInfo linkFile(CONF.cacheDir + "anilist_links.dat");
            qint64 linkSize = linkFile.exists() ? linkFile.size() / 1024 : 0;

            auto *linkHeader = new QListWidgetItem(
                "-- AniList Links (" + QString::number(linkCount) + " entries, " +
                QString::number(linkSize) + " KB) --", jobList);
            linkHeader->setForeground(QColor(80, 80, 80));
            linkHeader->setFlags(linkHeader->flags() & ~Qt::ItemIsUserCheckable);

            // Show individual links
            for (const auto &desc : homeWidget->aniListLinkDescriptions())
            {
                auto *item = new QListWidgetItem("  " + desc, jobList);
                item->setForeground(QColor(120, 120, 120));
                item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
            }

            // Reset button as a list item
            auto *resetItem = new QListWidgetItem("  Clear all links (will re-match)", jobList);
            resetItem->setData(Qt::UserRole, "__reset_anilist_links__");
            resetItem->setForeground(QColor(160, 60, 60));
            auto f = resetItem->font();
            f.setItalic(true);
            resetItem->setFont(f);
            resetItem->setFlags(resetItem->flags() & ~Qt::ItemIsUserCheckable);
        }
    }

    if (cached.isEmpty() && (!homeWidget || homeWidget->aniListLinkCount() == 0))
        jobList->addItem("No downloaded content");

    headerLabel->setText(QString("Downloaded (%1)").arg(cached.size()));
    statusLabel->setText(QString("%1 items, %2 MB").arg(cached.size()).arg(totalSize));
    updateStorageInfo();

    // Track check states to distinguish checkbox click from text click
    disconnect(jobList, &QListWidget::itemChanged, nullptr, nullptr);
    connect(jobList, &QListWidget::itemChanged, this, [this](QListWidgetItem *)
    {
        // Checkbox was toggled - do nothing else, just let the check state change
    });

    // Handle clicks
    disconnect(jobList, &QListWidget::itemDoubleClicked, nullptr, nullptr);
    disconnect(jobList, &QListWidget::itemClicked, nullptr, nullptr);

    // Single click for reset links action
    connect(jobList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item)
    {
        if (!showingCached || !item)
            return;

        if (item->data(Qt::UserRole).toString() == "__reset_anilist_links__")
        {
            emit resetAniListLinksRequested();
            refreshCachedList();
        }
    });

    // Double-click to open manga
    connect(jobList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item)
    {
        if (!showingCached || !item)
            return;

        auto source = item->data(Qt::UserRole + 1).toString();
        auto title = item->data(Qt::UserRole + 2).toString();

        if (!source.isEmpty() && !title.isEmpty())
            emit openMangaRequested(source, title);
    });
}

void DownloadQueueWidget::updateStorageInfo()
{
    // Calculate app cache size
    qint64 appBytes = 0;
    QDirIterator it(CONF.cacheDir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        it.next();
        appBytes += it.fileInfo().size();
    }

    // Get free space on device
    QStorageInfo storage(CONF.cacheDir);
    qint64 freeBytes = storage.bytesAvailable();

    auto formatSize = [](qint64 bytes) -> QString {
        if (bytes >= 1024LL * 1024 * 1024)
            return QString::number(bytes / (1024.0 * 1024 * 1024), 'f', 1) + " GB";
        return QString::number(bytes / (1024.0 * 1024), 'f', 0) + " MB";
    };

    storageLabel->setText("App data: " + formatSize(appBytes) +
                          "  |  Free: " + formatSize(freeBytes));
}

void DownloadQueueWidget::updateSleepDownloadStatus(bool enabled)
{
    if (sleepDownloadLabel)
    {
        if (enabled)
            sleepDownloadLabel->setText("Sleep downloads: ON (change in Settings > Power)");
        else
            sleepDownloadLabel->setText("Sleep downloads: OFF (change in Settings > Power)");
    }
}
