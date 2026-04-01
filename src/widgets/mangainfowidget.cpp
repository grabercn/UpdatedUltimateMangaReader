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

    ui->labelMangaInfoTitle->setStyleSheet("font-size: 16pt");

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
        double r = (double)img.height() / img.width();

        if (r >= ((double)SIZES.coverHeight / SIZES.coverWidth))
            ui->labelMangaInfoCover->setFixedSize(SIZES.coverHeight / r, SIZES.coverHeight);
        else
            ui->labelMangaInfoCover->setFixedSize(SIZES.coverWidth, SIZES.coverWidth * r);

        ui->labelMangaInfoCover->setPixmap(img);
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

    // Limit cover height so it doesn't push everything down
    ui->labelMangaInfoCover->setMaximumHeight(SIZES.coverHeight);

    updateAniListTracking();

    ui->scrollAreaMangaInfoSummary->verticalScrollBar()->setValue(0);
    ui->listViewChapters->verticalScrollBar()->setValue(0);

    bool enable = currentmanga->chapters.count() > 0;

    ui->pushButtonReadContinue->setEnabled(enable);
    ui->pushButtonReadFirst->setEnabled(enable);
    ui->pushButtonReadLatest->setEnabled(enable);
    ui->toolButtonDownload->setVisible(enable);
    ui->toolButtonAddFavorites->setVisible(enable);

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

        if (localCh > 0 || localPg > 0)
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
    emit readMangaClicked({static_cast<int>(currentmanga->chapters.count() - 1 - index.row()), 0});
}

void MangaInfoWidget::on_pushButtonReadLatest_clicked()
{
    emit readMangaClicked({static_cast<int>(currentmanga->chapters.count() - 1), 0});
}

void MangaInfoWidget::on_pushButtonReadContinue_clicked()
{
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
        "padding: 1px 6px; margin: 0; }");
    aniListFrame->setObjectName("aniListFrame");
    aniListFrame->setFixedHeight(34);

    auto *row = new QHBoxLayout(aniListFrame);
    row->setSpacing(3);
    row->setContentsMargins(0, 0, 0, 0);

    aniListLabel = new QLabel("AL", aniListFrame);
    aniListLabel->setStyleSheet("font-weight: bold; font-size: 8pt; border: none; background: transparent;");
    aniListLabel->setFixedWidth(18);
    row->addWidget(aniListLabel);

    aniListStatusCombo = new QComboBox(aniListFrame);
    aniListStatusCombo->setStyleSheet("font-size: 8pt; min-height: 0; padding: 1px 2px;");
    aniListStatusCombo->addItems({"--", "Reading", "Plan", "Done", "Drop", "Pause", "Re"});
    aniListStatusCombo->setFixedHeight(28);
    row->addWidget(aniListStatusCombo);

    aniListProgressSpin = new QSpinBox(aniListFrame);
    aniListProgressSpin->setRange(0, 9999);
    aniListProgressSpin->setPrefix("Ch.");
    aniListProgressSpin->setStyleSheet("font-size: 8pt; min-height: 0; padding: 1px;");
    aniListProgressSpin->setFixedHeight(28);
    aniListProgressSpin->setFixedWidth(62);
    row->addWidget(aniListProgressSpin);

    aniListScoreCombo = new QComboBox(aniListFrame);
    aniListScoreCombo->setStyleSheet("font-size: 8pt; min-height: 0; padding: 1px;");
    aniListScoreCombo->addItem("-", 0);
    for (int i = 1; i <= 10; i++)
        aniListScoreCombo->addItem(QString::number(i), i);
    aniListScoreCombo->setFixedHeight(28);
    aniListScoreCombo->setFixedWidth(36);
    row->addWidget(aniListScoreCombo);

    aniListSyncBtn = new QPushButton("Sync", aniListFrame);
    aniListSyncBtn->setFixedHeight(28);
    aniListSyncBtn->setFixedWidth(40);
    aniListSyncBtn->setStyleSheet("font-size: 8pt; min-height: 0; padding: 1px 3px;");
    row->addWidget(aniListSyncBtn);

    connect(aniListSyncBtn, &QPushButton::clicked, this, [this]()
    {
        if (!aniList || !aniList->isLoggedIn() || currentAniListMediaId <= 0)
            return;

        int status = aniListStatusCombo->currentIndex();
        int progress = aniListProgressSpin->value();
        int score = aniListScoreCombo->currentData().toInt();

        if (status > 0)
        {
            aniList->updateProgress(currentAniListMediaId, progress, status);
            if (score > 0)
                aniList->updateScore(currentAniListMediaId, score);
            aniListLabel->setText("OK");
            QTimer::singleShot(2000, this, [this]() { aniListLabel->setText("AL:"); });
        }
    });

    // Add to summary scroll area (safe - no layout manipulation)
    auto *scrollContent = ui->scrollAreaMangaInfoSummary->widget();
    if (scrollContent && scrollContent->layout())
        scrollContent->layout()->addWidget(aniListFrame);
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
            aniListLabel->setText("AL:");
            aniListStatusCombo->setCurrentIndex(entry.status);
            aniListProgressSpin->setValue(entry.progress);
            if (entry.totalChapters > 0)
                aniListProgressSpin->setMaximum(entry.totalChapters);

            int scoreIdx = aniListScoreCombo->findData(entry.score);
            if (scoreIdx >= 0)
                aniListScoreCombo->setCurrentIndex(scoreIdx);
        }
        else
        {
            // Don't block UI with searchMediaId - just show as not tracked
            aniListLabel->setText("AL:");
            aniListStatusCombo->setCurrentIndex(0);
            aniListProgressSpin->setValue(0);
            aniListScoreCombo->setCurrentIndex(0);
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
