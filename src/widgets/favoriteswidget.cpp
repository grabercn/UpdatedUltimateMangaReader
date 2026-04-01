#include "favoriteswidget.h"

#include "anilist.h"
#include "ui_favoriteswidget.h"

FavoritesWidget::FavoritesWidget(QWidget *parent)
    : QWidget(parent), favoritesManager(nullptr), ui(new Ui::FavoritesWidget)
{
    ui->setupUi(this);

    adjustUI();
}

FavoritesWidget::~FavoritesWidget()
{
    delete ui;
}

void FavoritesWidget::adjustUI()
{
    ui->tableWidget->setHorizontalHeaderLabels(QStringList() << "Manga"
                                                             << "Host"
                                                             << "Status"
                                                             << "My Progress");
    ui->tableWidget->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

    activateScroller(ui->tableWidget);

    QHeaderView *verticalHeader = ui->tableWidget->verticalHeader();

    verticalHeader->setSectionResizeMode(QHeaderView::Fixed);
    verticalHeader->setDefaultSectionSize(SIZES.favoriteSectonHeight);

    QHeaderView *horizontalHeader = ui->tableWidget->horizontalHeader();
    horizontalHeader->setSectionsClickable(false);
    horizontalHeader->setFrameShape(QFrame::Box);
    horizontalHeader->setLineWidth(1);

    for (int i = 0; i < 4; i++)
        horizontalHeader->setSectionResizeMode(i, QHeaderView::Stretch);
}

void FavoritesWidget::showFavoritesList()
{
    favoritesManager->loadInfos();
    favoritesManager->updateInfos();

    // Always rebuild the table to stay in sync
    ui->tableWidget->setRowCount(0);

    for (int r = 0; r < favoritesManager->favoriteinfos.count(); r++)
    {
        if (favoritesManager->favoriteinfos[r].isNull())
            continue;

        insertRow(favoritesManager->favoriteinfos[r], ui->tableWidget->rowCount());

        // Disconnect first to avoid duplicate connections
        disconnect(favoritesManager->favoriteinfos[r].get(), &MangaInfo::updatedSignal,
                   this, &FavoritesWidget::mangaUpdated);
        disconnect(favoritesManager->favoriteinfos[r].get(), &MangaInfo::coverLoaded,
                   this, &FavoritesWidget::coverLoaded);

        connect(favoritesManager->favoriteinfos[r].get(), &MangaInfo::updatedSignal,
                this, &FavoritesWidget::mangaUpdated);
        connect(favoritesManager->favoriteinfos[r].get(), &MangaInfo::coverLoaded,
                this, &FavoritesWidget::coverLoaded);
    }
}

static QPair<QString, QString> getSmartStatus(const QSharedPointer<MangaInfo> &fav,
                                               AniList *aniList)
{
    ReadingProgress progress(fav->hostname, fav->title);
    int ch = progress.index.chapter;
    int pg = progress.index.page;
    int totalCh = fav->chapters.size();

    // Check AniList first
    QString status;
    int aniProgress = 0;
    if (aniList && aniList->isLoggedIn())
    {
        auto entry = aniList->findByTitle(fav->title);
        if (entry.mediaId > 0)
        {
            status = AniList::statusName(entry.status);
            aniProgress = entry.progress;
            // Use AniList progress if higher
            if (aniProgress > ch)
            {
                ch = aniProgress;
                pg = 0;
            }
        }
    }

    // Smart status if no AniList match
    if (status.isEmpty())
    {
        if (ch == 0 && pg <= 1)
            status = "Not started";
        else if (totalCh > 0 && ch >= totalCh - 1)
            status = "Finished";
        else
            status = "Reading";
    }

    if (fav->updated)
        status = "New chapters!";

    status += "\n" + QString::number(totalCh) + " chapters";

    QString progressStr;
    if (ch == 0 && pg <= 1)
        progressStr = "Not started";
    else
        progressStr = "Ch." + QString::number(ch + 1) + " Pg." + QString::number(pg + 1);

    return {status, progressStr};
}

void FavoritesWidget::updateStatus(int row)
{
    if (row >= favoritesManager->favoriteinfos.size())
        return;

    auto &fav = favoritesManager->favoriteinfos.at(row);
    auto [status, progress] = getSmartStatus(fav, aniList);

    ui->tableWidget->item(row, 2)->setText(status);
    ui->tableWidget->item(row, 3)->setText(progress);
}

void FavoritesWidget::insertRow(const QSharedPointer<MangaInfo> &fav, int row)
{
    if (fav.isNull())
        return;

    QWidget *titlewidget = makeIconTextWidget(fav->coverThumbnailPath(), fav->title);

    ui->tableWidget->insertRow(row);

    QTableWidgetItem *hostwidget = new QTableWidgetItem(fav->hostname);
    hostwidget->setTextAlignment(Qt::AlignCenter);

    auto [statusStr, progressStr] = getSmartStatus(fav, aniList);

    QTableWidgetItem *chapters = new QTableWidgetItem(statusStr);
    chapters->setTextAlignment(Qt::AlignCenter);

    QTableWidgetItem *progressitem = new QTableWidgetItem(progressStr);
    progressitem->setTextAlignment(Qt::AlignCenter);

    ui->tableWidget->setCellWidget(row, 0, titlewidget);
    ui->tableWidget->setItem(row, 1, hostwidget);
    ui->tableWidget->setItem(row, 2, chapters);
    ui->tableWidget->setItem(row, 3, progressitem);
}

void FavoritesWidget::moveFavoriteToFront(int i)
{
    favoritesManager->moveFavoriteToFront(i);

    ui->tableWidget->removeRow(i);
    insertRow(favoritesManager->favoriteinfos.at(0), 0);

    ui->tableWidget->scrollToTop();
}

void FavoritesWidget::mangaUpdated(bool updated)
{
    if (updated)
    {
        MangaInfo *mi = static_cast<MangaInfo *>(sender());

        int i = favoritesManager->findFavorite(mi->title);

        if (i == -1)
            return;

        moveFavoriteToFront(i);
    }
}

void FavoritesWidget::coverLoaded()
{
    MangaInfo *mi = static_cast<MangaInfo *>(sender());

    int i = favoritesManager->findFavorite(mi->title);

    if (i == -1)
        return;

    QWidget *titlewidget = makeIconTextWidget(favoritesManager->favoriteinfos.at(i)->coverThumbnailPath(),
                                              favoritesManager->favoriteinfos.at(i)->title);
    ui->tableWidget->setCellWidget(i, 0, titlewidget);
}

QWidget *FavoritesWidget::makeIconTextWidget(const QString &path, const QString &text)
{
    QWidget *widget = new QWidget();

    QLabel *textlabel = new QLabel(text, widget);

    auto img = QPixmap::fromImage(loadQImageFast(path));
    QLabel *iconlabel = new QLabel(widget);
    iconlabel->setScaledContents(true);
    iconlabel->setFixedSize(img.width() / qApp->devicePixelRatio(), img.height() / qApp->devicePixelRatio());
    iconlabel->setPixmap(img);

    QVBoxLayout *vlayout = new QVBoxLayout(widget);
    vlayout->setAlignment(Qt::AlignCenter);
    QHBoxLayout *hlayout = new QHBoxLayout();
    hlayout->addWidget(iconlabel);
    hlayout->setAlignment(Qt::AlignCenter);

    vlayout->addLayout(hlayout);
    vlayout->addWidget(textlabel);
    vlayout->setContentsMargins(0, 0, 0, 0);
    vlayout->setSpacing(2);
    widget->setLayout(vlayout);

    widget->setProperty("ptext", text);

    return widget;
}

void FavoritesWidget::on_tableWidget_cellClicked(int row, int column)
{
    moveFavoriteToFront(row);

    emit favoriteClicked(favoritesManager->favoriteinfos.first(), column >= 2);
}
