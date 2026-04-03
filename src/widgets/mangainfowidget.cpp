#include "mangainfowidget.h"

#include <QHBoxLayout>
#include <QResizeEvent>
#include <QVBoxLayout>

#include "anilist.h"
#include <QScrollBar>

#include "qstringlistmodel.h"
#include "ui_mangainfowidget.h"

MangaInfoWidget::MangaInfoWidget(QWidget *parent)
    : QWidget(parent), ui(new Ui::MangaInfoWidget), currentmanga()
{
    ui->setupUi(this);
    adjustUI();
}

MangaInfoWidget::~MangaInfoWidget()
{
    delete ui;
}

void MangaInfoWidget::adjustUI()
{
    ui->pushButtonReadContinue->setProperty("type", "borderless");
    ui->pushButtonReadFirst->setProperty("type", "borderless");
    ui->pushButtonReadLatest->setProperty("type", "borderless");

    ui->pushButtonReadContinue->setFixedHeight(SIZES.buttonSize);
    ui->pushButtonReadFirst->setFixedHeight(SIZES.buttonSize);
    ui->pushButtonReadLatest->setFixedHeight(SIZES.buttonSize);

    ui->toolButtonAddFavorites->setFixedSize(SIZES.buttonSizeToggleFavorite, SIZES.buttonSizeToggleFavorite);
    ui->toolButtonAddFavorites->setIconSize(
        QSize(SIZES.buttonSizeToggleFavorite * 0.8, SIZES.buttonSizeToggleFavorite * 0.8));
    ui->toolButtonDownload->setFixedSize(SIZES.buttonSizeToggleFavorite, SIZES.buttonSizeToggleFavorite);
    ui->toolButtonDownload->setIconSize(
        QSize(SIZES.buttonSizeToggleFavorite * 0.8, SIZES.buttonSizeToggleFavorite * 0.8));

    ui->labelMangaInfoCover->setScaledContents(true);

    ui->labelMangaInfoTitle->setStyleSheet("font-weight: bold;");

    // set labels bold
    ui->labelMangaInfoLabelAuthor->setProperty("type", "mangainfolabel");
    ui->labelMangaInfoLabelArtist->setProperty("type", "mangainfolabel");
    ui->labelMangaInfoLabelGenres->setProperty("type", "mangainfolabel");
    ui->labelMangaInfoLabelStaus->setProperty("type", "mangainfolabel");

    ui->labelMangaInfoLabelAuthorContent->setProperty("type", "mangainfocontent");
    ui->labelMangaInfoLabelArtistContent->setProperty("type", "mangainfocontent");
    ui->labelMangaInfoLabelGenresContent->setProperty("type", "mangainfocontent");
    ui->labelMangaInfoLabelStausContent->setProperty("type", "mangainfocontent");

    activateScroller(ui->scrollAreaMangaInfoSummary);
    activateScroller(ui->listViewChapters);
    ui->listViewChapters->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
}

inline void updateLabel(QLabel *caption, QLabel *content, const QString &text)
{
    bool hide = text.length() <= 1;
    caption->setHidden(hide);
    content->setHidden(hide);

    content->setText(text);
}

void MangaInfoWidget::setManga(QSharedPointer<MangaInfo> manga)
{
    if (!manga)
        return;

    if (currentmanga != manga)
    {
        currentmanga.clear();
        currentmanga = manga;

        QObject::connect(currentmanga.get(), &MangaInfo::updatedSignal, this, &MangaInfoWidget::updateManga);

        QObject::connect(currentmanga.get(), &MangaInfo::coverLoaded, this, &MangaInfoWidget::updateCover);
    }

    updateInfos();
    updateCover();
}

void MangaInfoWidget::updateManga(bool)
{
    qDebug() << "updated" << currentmanga->title;

    updateInfos();
}

void MangaInfoWidget::updateCover()
{
    if (!QFile::exists(currentmanga->coverPath))
    {
        ui->labelMangaInfoCover->clear();
    }
    else
    {
        QPixmap img = QPixmap::fromImage(loadQImageFast(currentmanga->coverPath));
        if (!img.isNull() && img.width() > 0 && img.height() > 0)
        {
            double r = (double)img.height() / img.width();
            if (r > 0 && r >= ((double)SIZES.coverHeight / qMax(1, SIZES.coverWidth)))
                ui->labelMangaInfoCover->setFixedSize(SIZES.coverHeight / r, SIZES.coverHeight);
            else if (r > 0)
                ui->labelMangaInfoCover->setFixedSize(SIZES.coverWidth, SIZES.coverWidth * r);
            ui->labelMangaInfoCover->setPixmap(img);
        }
    }
}

void MangaInfoWidget::updateInfos()
{
    if (currentmanga.isNull())
        return;

    QStringList list;

    // Check which chapters have downloaded images
    auto imgDir = CONF.mangaimagesdir(currentmanga->hostname, currentmanga->title);
    QSet<int> downloadedChapters;
    if (QDir(imgDir).exists())
    {
        for (const auto &f : QDir(imgDir).entryList(QDir::Files))
        {
            auto parts = f.split('_');
            if (!parts.isEmpty())
                downloadedChapters.insert(parts[0].toInt());
        }
    }

    for (int i = 0; i < currentmanga->chapters.size(); i++)
    {
        QString prefix = downloadedChapters.contains(i) ? "[DL] " : "";
        list.insert(0, QString("%1: %2%3").arg(i + 1).arg(prefix).arg(currentmanga->chapters[i].chapterTitle));
    }

    QStringListModel *model = new QStringListModel(this);
    model->setStringList(list);

    if (ui->listViewChapters->model() != nullptr)
        ui->listViewChapters->model()->deleteLater();

    ui->listViewChapters->setModel(model);

    ui->labelMangaInfoTitle->setText(currentmanga->title);

    updateLabel(ui->labelMangaInfoLabelAuthor, ui->labelMangaInfoLabelAuthorContent, currentmanga->author);
    updateLabel(ui->labelMangaInfoLabelArtist, ui->labelMangaInfoLabelArtistContent, currentmanga->artist);
    // Keep genres very compact
    QString genres = currentmanga->genres;
    if (genres.length() > 40)
        genres = genres.left(37) + "...";
    updateLabel(ui->labelMangaInfoLabelGenres, ui->labelMangaInfoLabelGenresContent, genres);
    updateLabel(ui->labelMangaInfoLabelStaus, ui->labelMangaInfoLabelStausContent, currentmanga->status);

    // Hide artist row if same as author or empty to save space
    if (currentmanga->artist.isEmpty() || currentmanga->artist == currentmanga->author)
    {
        ui->labelMangaInfoLabelArtist->hide();
        ui->labelMangaInfoLabelArtistContent->hide();
    }
    else
    {
        ui->labelMangaInfoLabelArtist->show();
        ui->labelMangaInfoLabelArtistContent->show();
    }

    // Compact summary - 2 lines max, tap to expand
    QString summary = currentmanga->summary;
    if (summary.length() > 100)
    {
        ui->labelMangaInfoLabelSummaryContent->setText(summary.left(97) + "...");
        ui->labelMangaInfoLabelSummaryContent->setCursor(Qt::PointingHandCursor);
        ui->labelMangaInfoLabelSummaryContent->installEventFilter(this);
    }
    else
    {
        ui->labelMangaInfoLabelSummaryContent->setText(summary);
    }

    // Very compact - just enough for 2 lines
    ui->scrollAreaMangaInfoSummary->setMaximumHeight(45);

    // Give cover image breathing room with reasonable max height
    ui->labelMangaInfoCover->setMaximumHeight(SIZES.coverHeight);
    ui->labelMangaInfoCover->setContentsMargins(4, 8, 4, 8);

    updateAniListTracking();

    ui->scrollAreaMangaInfoSummary->verticalScrollBar()->setValue(0);

    bool enable = currentmanga->chapters.count() > 0;

    // Scroll chapter list to current reading position
    if (enable)
    {
        ReadingProgress progress(currentmanga->hostname, currentmanga->title);
        int currentChapter = progress.index.chapter;
        // List is reversed (newest first), so convert chapter index
        int listRow = currentmanga->chapters.size() - 1 - currentChapter;
        if (listRow >= 0 && listRow < currentmanga->chapters.size())
        {
            auto idx = ui->listViewChapters->model()->index(listRow, 0);
            ui->listViewChapters->scrollTo(idx, QAbstractItemView::PositionAtCenter);
        }
    }
    else
    {
        ui->listViewChapters->verticalScrollBar()->setValue(0);
    }

    // Check if chapters are download-only (e.g. raw PDFs on IA without page images)
    bool downloadOnly = false;
    if (enable && currentmanga->mangaSource && !currentmanga->chapters.isEmpty())
    {
        downloadOnly = currentmanga->mangaSource->isDownloadOnly(
            currentmanga->chapters[0].chapterUrl);
    }

    ui->pushButtonReadContinue->setEnabled(enable && !downloadOnly);
    ui->pushButtonReadFirst->setEnabled(enable && !downloadOnly);
    ui->pushButtonReadLatest->setEnabled(enable && !downloadOnly);
    ui->toolButtonDownload->setVisible(enable);
    ui->toolButtonAddFavorites->setVisible(enable);

    isDownloadOnly = downloadOnly;

    if (downloadOnly)
    {
        ui->pushButtonReadContinue->setText("Download");
        ui->pushButtonReadContinue->setEnabled(true);
        ui->pushButtonReadFirst->hide();
        ui->pushButtonReadLatest->hide();
        auto *topFrame = ui->topButtonsFrame;
        for (auto *child : topFrame->findChildren<QFrame *>())
            if (child->frameShape() == QFrame::VLine)
                child->hide();
    }
    else
    {
        ui->pushButtonReadFirst->show();
        ui->pushButtonReadLatest->show();
        auto *topFrame = ui->topButtonsFrame;
        for (auto *child : topFrame->findChildren<QFrame *>())
            if (child->frameShape() == QFrame::VLine)
                child->show();
    }

    // Show reading progress - AniList overrides local if higher
    if (enable)
    {
        ReadingProgress progress(currentmanga->hostname, currentmanga->title);
        int localCh = progress.index.chapter;
        int localPg = progress.index.page;

        // Check AniList progress and use if higher
        if (aniList && aniList->isLoggedIn())
        {
            try
            {
                auto entry = aniList->findByTitle(currentmanga->title);
                if (entry.mediaId > 0 && entry.progress > 0)
                {
                    int aniCh = entry.progress - 1;  // AniList is 1-based completed chapters
                    if (aniCh > localCh)
                    {
                        // AniList is ahead - update local progress to match
                        localCh = aniCh;
                        localPg = 0;
                        progress.index.chapter = localCh;
                        progress.index.page = 0;
                        progress.serialize(currentmanga->hostname, currentmanga->title);
                    }
                }
            }
            catch (...) {}
        }

        if (downloadOnly)
        {
            ui->pushButtonReadContinue->setText("Download");
        }
        else if (localCh > 0 || localPg > 0)
        {
            QString text = "Continue Ch." + QString::number(localCh + 1);
            if (localPg > 0)
                text += " Pg." + QString::number(localPg + 1);
            ui->pushButtonReadContinue->setText(text);
        }
        else
        {
            ui->pushButtonReadContinue->setText("Start Reading");
        }
    }
}

void MangaInfoWidget::setFavoriteButtonState(bool state)
{
    ui->toolButtonAddFavorites->setChecked(state);
}

void MangaInfoWidget::on_toolButtonAddFavorites_clicked()
{
    if (!currentmanga.isNull())
        emit toggleFavoriteClicked(currentmanga);
}

void MangaInfoWidget::on_listViewChapters_clicked(const QModelIndex &index)
{
    if (currentmanga.isNull())
        return;

    int chapterIdx = static_cast<int>(currentmanga->chapters.count() - 1 - index.row());

    // Block clicks for download-only chapters
    if (currentmanga->mangaSource && chapterIdx >= 0 && chapterIdx < currentmanga->chapters.count() &&
        currentmanga->mangaSource->isDownloadOnly(currentmanga->chapters[chapterIdx].chapterUrl))
        return;

    emit readMangaClicked({chapterIdx, 0});
}

void MangaInfoWidget::on_pushButtonReadLatest_clicked()
{
    emit readMangaClicked({static_cast<int>(currentmanga->chapters.count() - 1), 0});
}

void MangaInfoWidget::on_pushButtonReadContinue_clicked()
{
    if (isDownloadOnly)
    {
        emit downloadMangaClicked();
        return;
    }
    emit readMangaContinueClicked();
}

void MangaInfoWidget::on_pushButtonReadFirst_clicked()
{
    emit readMangaClicked({0, 0});
}

void MangaInfoWidget::on_toolButtonDownload_clicked()
{
    if (!currentmanga.isNull())
        emit downloadMangaClicked();
}

bool MangaInfoWidget::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == ui->labelMangaInfoLabelSummaryContent &&
        event->type() == QEvent::MouseButtonRelease && !currentmanga.isNull())
    {
        summaryExpanded = !summaryExpanded;
        if (summaryExpanded)
        {
            ui->labelMangaInfoLabelSummaryContent->setText(currentmanga->summary);
            ui->scrollAreaMangaInfoSummary->setMaximumHeight(120);
        }
        else
        {
            QString s = currentmanga->summary;
            if (s.length() > 100)
                s = s.left(97) + "...";
            ui->labelMangaInfoLabelSummaryContent->setText(s);
            ui->scrollAreaMangaInfoSummary->setMaximumHeight(45);
        }
        return true;
    }
    return QWidget::eventFilter(obj, event);
}

void MangaInfoWidget::setAniList(AniList *al)
{
    aniList = al;
}

void MangaInfoWidget::setupAniListUI()
{
    if (aniListFrame)
        return;

    aniListFrame = new QFrame(this);
    aniListFrame->setStyleSheet(
        "QFrame#aniListFrame { border-top: 1px solid #ccc; background: #f5f5f5; "
        "padding: 1px 4px; margin: 0; }");
    aniListFrame->setObjectName("aniListFrame");

    auto *mainLayout = new QVBoxLayout(aniListFrame);
    mainLayout->setSpacing(2);
    mainLayout->setContentsMargins(4, 2, 4, 2);

    // Row 1: Status + Ch + Vol + Score + Sync
    auto *row1 = new QHBoxLayout();
    row1->setSpacing(3);

    aniListLabel = new QLabel("AL", aniListFrame);
    aniListLabel->setStyleSheet("font-weight: bold; border: none; background: transparent;");
    row1->addWidget(aniListLabel);

    aniListStatusCombo = new QComboBox(aniListFrame);
    aniListStatusCombo->addItems({"--", "Read", "Plan", "Done", "Drop", "Pause"});
    aniListStatusCombo->setFixedHeight(SIZES.buttonSize);
    row1->addWidget(aniListStatusCombo);

    aniListChapterCombo = new QComboBox(aniListFrame);
    aniListChapterCombo->setFixedHeight(SIZES.buttonSize);
    aniListChapterCombo->setMaxVisibleItems(8);
    row1->addWidget(aniListChapterCombo);

    aniListVolumeCombo = new QComboBox(aniListFrame);
    aniListVolumeCombo->setFixedHeight(SIZES.buttonSize);
    aniListVolumeCombo->setMaxVisibleItems(8);
    row1->addWidget(aniListVolumeCombo);

    aniListScoreCombo = new QComboBox(aniListFrame);
    aniListScoreCombo->addItem("-", 0);
    for (int i = 1; i <= 10; i++)
        aniListScoreCombo->addItem(QString::number(i), i);
    aniListScoreCombo->setFixedHeight(SIZES.buttonSize);
    row1->addWidget(aniListScoreCombo);

    aniListSyncBtn = nullptr;  // No sync button - auto-syncs on change

    mainLayout->addLayout(row1);

    // Auto-sync on ANY value change (debounced)
    auto syncFunc = [this]()
    {
        if (!aniList || !aniList->isLoggedIn() || currentAniListMediaId <= 0)
            return;

        int status = aniListStatusCombo->currentIndex();
        if (status <= 0) return;

        int chapters = aniListChapterCombo->currentData().toInt();
        int volumes = aniListVolumeCombo->currentData().toInt();
        int score = aniListScoreCombo->currentData().toInt();

        aniList->updateProgress(currentAniListMediaId, chapters, status, volumes);
        if (score > 0)
            aniList->updateScore(currentAniListMediaId, score);

        qDebug() << "AniList auto-synced: ch" << chapters << "vol" << volumes
                 << "status" << status << "score" << score;
    };

    connect(aniListStatusCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, syncFunc);
    connect(aniListChapterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, syncFunc);
    connect(aniListVolumeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, syncFunc);
    connect(aniListScoreCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, syncFunc);

    // Insert into main layout: between the cover/info section (index 1) and action buttons (index 2)
    auto *parentLayout = qobject_cast<QVBoxLayout *>(layout());
    if (parentLayout)
        parentLayout->insertWidget(2, aniListFrame);
}

void MangaInfoWidget::updateAniListTracking()
{
    setupAniListUI();

    if (!aniListFrame)
        return;

    if (!aniList || !aniList->isLoggedIn() || currentmanga.isNull())
    {
        aniListFrame->hide();
        return;
    }

    aniListFrame->show();
    currentAniListMediaId = 0;

    try
    {
        auto entry = aniList->findByTitle(currentmanga->title);

        if (entry.mediaId > 0)
        {
            currentAniListMediaId = entry.mediaId;
            aniListLabel->setText("AL");

            // Block signals while populating to prevent auto-sync spam
            aniListStatusCombo->blockSignals(true);
            aniListChapterCombo->blockSignals(true);
            aniListVolumeCombo->blockSignals(true);
            aniListScoreCombo->blockSignals(true);

            aniListStatusCombo->setCurrentIndex(entry.status);

            // Populate chapter dropdown
            aniListChapterCombo->clear();
            int maxCh = entry.totalChapters > 0 ? entry.totalChapters : qMax(entry.progress + 50, 500);
            for (int i = 0; i <= maxCh; i++)
                aniListChapterCombo->addItem("Ch. " + QString::number(i), i);
            aniListChapterCombo->setCurrentIndex(entry.progress);

            // Populate volume dropdown
            aniListVolumeCombo->clear();
            int maxVol = entry.totalVolumes > 0 ? entry.totalVolumes : qMax(entry.progressVolumes + 20, 100);
            for (int i = 0; i <= maxVol; i++)
                aniListVolumeCombo->addItem("Vol. " + QString::number(i), i);
            aniListVolumeCombo->setCurrentIndex(entry.progressVolumes);

            int scoreIdx = aniListScoreCombo->findData(entry.score);
            if (scoreIdx >= 0)
                aniListScoreCombo->setCurrentIndex(scoreIdx);

            // Unblock signals - now user changes will trigger auto-sync
            aniListStatusCombo->blockSignals(false);
            aniListChapterCombo->blockSignals(false);
            aniListVolumeCombo->blockSignals(false);
            aniListScoreCombo->blockSignals(false);
        }
        else
        {
            // No AniList match found - hide the bar entirely
            aniListFrame->hide();
            return;
        }
    }
    catch (...)
    {
        qDebug() << "AniList tracking update failed (non-fatal)";
        aniListFrame->hide();
    }

    if (aniListSyncBtn)
        aniListSyncBtn->setEnabled(currentAniListMediaId > 0);
}
