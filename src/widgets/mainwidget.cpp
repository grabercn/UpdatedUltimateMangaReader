#include "mainwidget.h"

#include <QListWidget>
#include <QPointer>
#include <QScreen>
#include <QTime>
#include <QToolButton>
#include <QVBoxLayout>

#include "updater.h"
#include "ui_mainwidget.h"

MainWidget::MainWidget(QWidget *parent)
    : QWidget(parent),
      ui(new Ui::MainWidget),
      core(new UltimateMangaReaderCore(this)),
      lastTab(MangaInfoTab),
      virtualKeyboard(new VirtualKeyboard(this)),
      errorMessageWidget(new ErrorMessageWidget(this)),
      powerButtonTimer(new QTimer(this)),
      spinner(nullptr)
{
    ui->setupUi(this);
    adjustUI();

    spinner = new SpinnerWidget(this);
    spinner->hide();

    // Download queue - full page widget
    downloadQueueWidget = new DownloadQueueWidget(this);
    ui->stackedWidget->addWidget(downloadQueueWidget);  // index = DownloadsTab (4)

    connect(downloadQueueWidget, &DownloadQueueWidget::backClicked,
            this, [this]() { readerGoBack(); });

    connect(downloadQueueWidget, &DownloadQueueWidget::cancelRequested,
            core->mangaChapterDownloadManager, &MangaChapterDownloadManager::cancelDownloads);

    connect(downloadQueueWidget, &DownloadQueueWidget::resetAniListLinksRequested,
            ui->homeWidget, &HomeWidget::resetAniListLinks);

    downloadQueueWidget->homeWidget = ui->homeWidget;

    connect(downloadQueueWidget, &DownloadQueueWidget::openMangaRequested,
            this, [this](const QString &source, const QString &title)
            {
                // Find the source and load the cached manga
                for (const auto &ms : core->mangaSources)
                {
                    if (ms->name == source)
                    {
                        core->setCurrentMangaSource(ms.get());
                        auto infoPath = CONF.mangainfodir(source, title) + "mangainfo.dat";
                        if (QFile::exists(infoPath))
                        {
                            try
                            {
                                auto info = MangaInfo::deserialize(ms.get(), infoPath);
                                if (info)
                                {
                                    core->mangaController->setCurrentManga(info);
                                    return;
                                }
                            }
                            catch (...) {}
                        }
                        break;
                    }
                }
            });

    // Add download button with badge to header bar
    activeDownloadCount = 0;
    downloadHeaderBtn = new QToolButton(this);
    downloadHeaderBtn->setIcon(QIcon(":/images/icons/download.png"));
    downloadHeaderBtn->setIconSize(QSize(SIZES.wifiIconSize, SIZES.wifiIconSize));
    downloadHeaderBtn->setMinimumSize(QSize(40, 40));
    downloadHeaderBtn->setFocusPolicy(Qt::NoFocus);
    downloadHeaderBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    ui->horizontalLayout_5->insertWidget(3, downloadHeaderBtn);
    connect(downloadHeaderBtn, &QToolButton::clicked, this,
            [this]() { setWidgetTab(DownloadsTab); });
    ui->batteryIcon->updateIcon();
    setupVirtualKeyboard();

    QObject::connect(powerButtonTimer, &QTimer::timeout, this, [this]()
    {
        qDebug() << "Long power press - shutting down";
        core->enableTimers(false);
        core->mangaController->cancelAllPreloads();
        core->settings.serialize();
        core->readingStats.stopReading();
        core->readingStats.serialize();
        if (core->aniList) core->aniList->serialize();
        core->saveHistory();
        close();
    });

    // Dialogs
    menuDialog = new MenuDialog(this);
    settingsDialog = new SettingsDialog(&core->settings, core->aniList, core->updater, this);
    updateMangaListsDialog = new UpdateMangaListsDialog(&core->settings, this);
    clearCacheDialog = new ClearCacheDialog(this);
    wifiDialog = new WifiDialog(this, core->networkManager);
    screensaverDialog = new ScreensaverDialog(this);
    downloadMangaChaptersDialog = new DownloadMangaChaptersDialog(this);
    downloadStatusDialog = new DownloadStatusDialog(this);
    aboutDialog = new AboutDialog(this);

    updateMangaListsDialog->installEventFilter(this);
    wifiDialog->installEventFilter(this);
    downloadMangaChaptersDialog->installEventFilter(this);
    downloadStatusDialog->installEventFilter(this);

    QObject::connect(menuDialog, &MenuDialog::finished,
                     [this](int b) {
                         // Ignore dismiss (0 = QDialog::Rejected from tap outside)
                         if (b >= ExitButton)
                             menuDialogButtonPressed(static_cast<MenuButton>(b));
                     });

    QObject::connect(clearCacheDialog, &MenuDialog::finished,
                     [this](int l) { core->clearDownloadCache(static_cast<ClearDownloadCacheLevel>(l)); });

    QObject::connect(updateMangaListsDialog, &UpdateMangaListsDialog::updateClicked, core,
                     &UltimateMangaReaderCore::updateMangaLists);

    // Download to app cache
    QObject::connect(downloadMangaChaptersDialog, &DownloadMangaChaptersDialog::downloadConfirmed,
                     [this](auto m, auto f, auto t)
                     {
                         bool isLN = m->mangaSource && m->mangaSource->contentType == ContentLightNovel;
                         downloadQueueWidget->addJob(m->title, m->hostname, f, t, false, isLN);
                         core->mangaChapterDownloadManager->downloadMangaChapters(m, f, t);
                         activeDownloadCount++;
                         updateDownloadBadge();
                         // Brief confirmation
                         showErrorMessage("Downloading - check Downloads page");
                     });

    // Export to Kobo device
    QObject::connect(downloadMangaChaptersDialog, &DownloadMangaChaptersDialog::exportToDeviceConfirmed,
                     [this](auto m, auto f, auto t)
                     {
                         bool isLN = m->mangaSource && m->mangaSource->contentType == ContentLightNovel;
                         downloadQueueWidget->addJob(m->title, m->hostname, f, t, true, isLN);
                         activeDownloadCount++;
                         updateDownloadBadge();

                         if (isLN)
                         {
                             showLoadingIndicator();
                             bool success = core->exportNovelAsEPUB(m, f, t);
                             hideLoadingIndicator();
                             if (success)
                             {
                                 downloadQueueWidget->jobCompleted(m->title);
                                 activeDownloadCount = qMax(0, activeDownloadCount - 1);
                                 updateDownloadBadge();
                                 showErrorMessage("Novel exported to Kobo library!");
                             }
                             else
                             {
                                 downloadQueueWidget->jobFailed(m->title, "Export failed");
                                 showErrorMessage("Export failed - download chapters first");
                             }
                         }
                         else
                         {
                             // Download images first, then export on completion
                             pendingExport = {m, f, t, true};
                             core->mangaChapterDownloadManager->downloadMangaChapters(m, f, t);
                             showErrorMessage("Downloading for export - check Downloads page");
                         }
                     });

    QObject::connect(downloadStatusDialog, &DownloadStatusDialog::abortDownloads,
                     core->mangaChapterDownloadManager, &MangaChapterDownloadManager::cancelDownloads);

    // NetworkManager
#ifdef KOBO
    core->networkManager->setDownloadSettings({koboDevice.width, koboDevice.height}, &core->settings);
#else
    core->networkManager->setDownloadSettings(this->size() * qApp->devicePixelRatio(), &core->settings);
#endif

    QPixmap wifioff(":/images/icons/no-wifi.png");
    QPixmap wifion(":/images/icons/wifi.png");
    wifiIcons[0] = QIcon(
        wifioff.scaledToHeight(SIZES.wifiIconSize * qApp->devicePixelRatio(), Qt::SmoothTransformation));
    wifiIcons[1] =
        QIcon(wifion.scaledToHeight(SIZES.wifiIconSize * qApp->devicePixelRatio(), Qt::SmoothTransformation));

    QObject::connect(core->networkManager, &NetworkManager::connectionStatusChanged,
                     [this](bool connected)
                     {
                         auto icon = connected ? wifiIcons[1] : wifiIcons[0];
                         ui->toolButtonWifiIcon->setIcon(icon);
                     });

    // Network error toasts
    QObject::connect(core->networkManager, &NetworkManager::networkError,
                     this, &MainWidget::showErrorMessage);

    // MangaChapterDownloadManager - log errors but don't spam popups
    QObject::connect(core->mangaChapterDownloadManager, &MangaChapterDownloadManager::error, this,
                     [](const QString &msg) { qDebug() << "Download error:" << msg; });

    QObject::connect(core->mangaChapterDownloadManager, &MangaChapterDownloadManager::downloadStart,
                     downloadStatusDialog, &DownloadStatusDialog::downloadStart);

    QObject::connect(core->mangaChapterDownloadManager,
                     &MangaChapterDownloadManager::downloadPagelistProgress, downloadStatusDialog,
                     &DownloadStatusDialog::downloadPagelistProgress);

    QObject::connect(core->mangaChapterDownloadManager, &MangaChapterDownloadManager::downloadPagesProgress,
                     downloadStatusDialog, &DownloadStatusDialog::downloadPagesProgress);

    QObject::connect(core->mangaChapterDownloadManager, &MangaChapterDownloadManager::downloadImagesProgress,
                     downloadStatusDialog, &DownloadStatusDialog::downloadImagesProgress);

    QObject::connect(core->mangaChapterDownloadManager, &MangaChapterDownloadManager::downloadCompleted,
                     downloadStatusDialog, &DownloadStatusDialog::downloadCompleted);

    // Wire download progress to queue widget
    QObject::connect(core->mangaChapterDownloadManager, &MangaChapterDownloadManager::downloadStart,
                     [this](const QString &title)
                     {
                         downloadQueueWidget->markJobActive(title);
                     });

    QObject::connect(core->mangaChapterDownloadManager,
                     &MangaChapterDownloadManager::downloadImagesProgress,
                     [this](int completed, int total, int)
                     {
                         downloadQueueWidget->updateActiveJob(completed, total, 0);
                     });

    QObject::connect(core->mangaChapterDownloadManager,
                     &MangaChapterDownloadManager::downloadCompleted,
                     [this]()
                     {
                         // If a pending export was waiting for download to finish, run it now
                         if (pendingExport.active && pendingExport.manga)
                         {
                             bool ok = core->exportMangaAsCBZ(pendingExport.manga,
                                                               pendingExport.fromChapter,
                                                               pendingExport.toChapter);
                             if (ok)
                             {
                                 downloadQueueWidget->jobCompleted(pendingExport.manga->title);
                                 showErrorMessage("Exported to Kobo library!");
                             }
                             else
                                 downloadQueueWidget->jobFailed(pendingExport.manga->title,
                                                                 "Export failed");
                             pendingExport = {};
                         }
                         else if (core->mangaController->currentManga)
                         {
                             downloadQueueWidget->jobCompleted(
                                 core->mangaController->currentManga->title);
                         }
                         activeDownloadCount = qMax(0, activeDownloadCount - 1);
                         updateDownloadBadge();
                     });

    // Core
    QObject::connect(core, &UltimateMangaReaderCore::error, this, &MainWidget::showErrorMessage);

    QObject::connect(core, &UltimateMangaReaderCore::timeTick, this, &MainWidget::timerTick);

    // AniList + Favorites
    ui->homeWidget->setAniList(core->aniList);
    ui->homeWidget->setFavoritesManager(core->favoritesManager);

    connect(ui->homeWidget, &HomeWidget::openHistoryRequested,
            this, [this]() { menuDialogButtonPressed(HistoryButton); });
    connect(ui->homeWidget, &HomeWidget::openAniListRequested,
            this, [this]() { menuDialogButtonPressed(AniListButton); });

    QObject::connect(core, &UltimateMangaReaderCore::activeMangaSourcesChanged, ui->homeWidget,
                     &HomeWidget::updateSourcesList);

    QObject::connect(core, &UltimateMangaReaderCore::downloadCacheCleared,
                     [this]() { ui->mangaReaderWidget->clearCache(); });

    // MangaController
    QObject::connect(core->mangaController, &MangaController::currentMangaChanged,
                     [this](auto info)
                     {
                         ui->mangaInfoWidget->setManga(info);
                         bool state = core->favoritesManager->isFavorite(info);
                         ui->mangaInfoWidget->setFavoriteButtonState(state);

                         // Sync AniList progress to local whenever a manga is opened
                         if (info && core->aniList && core->aniList->isLoggedIn())
                         {
                             try
                             {
                                 auto entry = core->aniList->findByTitle(info->title);
                                 if (entry.mediaId > 0 && entry.progress > 0)
                                 {
                                     ReadingProgress localProgress(info->hostname, info->title);
                                     int aniCh = entry.progress - 1;
                                     if (aniCh > localProgress.index.chapter)
                                     {
                                         localProgress.index.chapter = aniCh;
                                         localProgress.index.page = 0;
                                         localProgress.serialize(info->hostname, info->title);
                                         qDebug() << "Synced AniList progress for" << info->title
                                                  << "to Ch." << (aniCh + 1);
                                     }
                                 }
                             }
                             catch (...) {}
                         }

                         setWidgetTab(MangaInfoTab);
                         ui->mangaReaderWidget->clearCache();
                     });

    // Auto-track AniList progress when reading
    // Track completed chapters: when moving to ch.N, mark ch.N-1 as completed
    QObject::connect(core->mangaController, &MangaController::currentIndexChanged,
                     [this](const ReadingProgress &progress)
                     {
                         // Track reading stats
                         if (core->mangaController->currentManga)
                         {
                             auto title = core->mangaController->currentManga->title;
                             core->readingStats.pageRead(title);

                             // Detect chapter completion (page 0 of new chapter = prev chapter done)
                             static int lastChapter = -1;
                             static QString lastMangaTitle;
                             if (title != lastMangaTitle)
                             {
                                 lastChapter = -1;
                                 lastMangaTitle = title;
                             }
                             if (progress.index.chapter != lastChapter && lastChapter >= 0)
                                 core->readingStats.chapterCompleted(title);
                             lastChapter = progress.index.chapter;
                         }

                         if (core->aniList && core->aniList->isLoggedIn() &&
                             core->mangaController->currentManga)
                         {
                             try
                             {
                                 auto manga = core->mangaController->currentManga;
                                 int currentCh = progress.index.chapter;
                                 int totalCh = manga->chapters.count();

                                 // Track: current chapter index = completed chapters
                                 // (reading ch.1 means ch.0 is completed = 1 completed)
                                 // But if on ch.0 pg.0, don't track yet (just opened)
                                 int completed = currentCh;
                                 if (completed > 0 || progress.index.page > 0)
                                     core->aniList->trackReading(manga->title, completed);

                                 // Auto-complete if at the last chapter's last page
                                 if (currentCh >= totalCh - 1 && totalCh > 0)
                                 {
                                     // Check if this is truly the end
                                     bool atEnd = false;
                                     if (manga->mangaSource->contentType == ContentLightNovel)
                                         atEnd = true;  // LN: being on last chapter = done
                                     else if (progress.numPages > 0 &&
                                              progress.index.page >= progress.numPages - 1)
                                         atEnd = true;

                                     if (atEnd)
                                     {
                                         core->aniList->trackReading(manga->title, totalCh);
                                         // Mark as COMPLETED on AniList
                                         auto entry = core->aniList->findByTitle(manga->title);
                                         if (entry.mediaId > 0)
                                             core->aniList->updateStatus(entry.mediaId, 3);
                                     }
                                 }
                             }
                             catch (...)
                             {
                                 qDebug() << "AniList tracking error (non-fatal)";
                             }
                         }
                     });

    QObject::connect(core->mangaController, &MangaController::currentTextChanged,
                     [this](const QString &text, const QString &title)
                     {
                         ui->mangaReaderWidget->showText(text, title);
                     });

    QObject::connect(core->mangaController, &MangaController::completedImagePreloadSignal,
                     ui->mangaReaderWidget,
                     [this](auto path) { ui->mangaReaderWidget->addImageToCache(path, true); });

    QObject::connect(core->mangaController, &MangaController::currentIndexChanged, ui->mangaReaderWidget,
                     &MangaReaderWidget::updateCurrentIndex);

    QObject::connect(core->mangaController, &MangaController::currentImageChanged, ui->mangaReaderWidget,
                     &MangaReaderWidget::showImage);

    QObject::connect(core->mangaController, &MangaController::indexMovedOutOfBounds, this,
                     &MainWidget::readerGoBack);

    // Log controller errors but don't spam the user with popups for image failures
    QObject::connect(core->mangaController, &MangaController::error, this,
                     [this](const QString &msg)
                     {
                         qDebug() << "Reader error:" << msg;
                         // Only show non-download errors to user
                         if (!msg.contains("404") && !msg.contains("transferring") &&
                             !msg.contains("timeout", Qt::CaseInsensitive))
                             showErrorMessage(msg);
                     });

    // FavoritesManager
    QObject::connect(core->favoritesManager, &FavoritesManager::error, this, &MainWidget::showErrorMessage);

    // HomeWidget
    QObject::connect(ui->homeWidget, &HomeWidget::mangaSourceClicked, core,
                     &UltimateMangaReaderCore::setCurrentMangaSource);

    QObject::connect(ui->homeWidget, &HomeWidget::mangaClicked,
                     [this](const QString &url, const QString &title)
                     {
                         showLoadingIndicator();
                         core->setCurrentManga(url, title);
                         hideLoadingIndicator();
                     });

    QObject::connect(core, &UltimateMangaReaderCore::currentMangaSourceChanged, ui->homeWidget,
                     &HomeWidget::currentMangaSourceChanged);

    // MangaInfoWidget
    ui->mangaInfoWidget->setAniList(core->aniList);

    QObject::connect(ui->mangaInfoWidget, &MangaInfoWidget::toggleFavoriteClicked,
                     [this](auto info)
                     {
                         bool newstate = core->favoritesManager->toggleFavorite(info);
                         ui->mangaInfoWidget->setFavoriteButtonState(newstate);
                     });

    QObject::connect(ui->mangaInfoWidget, &MangaInfoWidget::readMangaClicked,
                     [this](auto index)
                     {
                         setWidgetTab(MangaReaderTab);
                         core->mangaController->setCurrentIndex(index);
                         if (core->settings.preloadEnabled)
                             QTimer::singleShot(50, core->mangaController, &MangaController::preloadNeighbours);
                     });

    QObject::connect(ui->mangaInfoWidget, &MangaInfoWidget::readMangaContinueClicked,
                     [this]() { setWidgetTab(MangaReaderTab); });

    // Continue Reading auto-jump from home page
    QObject::connect(ui->homeWidget, &HomeWidget::readMangaContinueClicked,
                     [this]() { setWidgetTab(MangaReaderTab); });

    QObject::connect(ui->mangaInfoWidget, &MangaInfoWidget::downloadMangaClicked,
                     [this]()
                     {
                         bool exportOnly = false;
                         auto manga = core->mangaController->currentManga;
                         if (manga && manga->mangaSource && !manga->chapters.isEmpty())
                             exportOnly = manga->mangaSource->isDownloadOnly(manga->chapters[0].chapterUrl);

                         downloadMangaChaptersDialog->show(manga,
                                                           core->mangaController->currentIndex.chapter,
                                                           exportOnly);
                     });

    // FavoritesWidget
    ui->favoritesWidget->favoritesManager = core->favoritesManager;
    ui->favoritesWidget->aniList = core->aniList;

    QObject::connect(ui->favoritesWidget, &FavoritesWidget::favoriteClicked,
                     [this](auto mangainfo, auto jumptoreader)
                     {
                         core->mangaController->setCurrentManga(mangainfo);
                         if (jumptoreader)
                             setWidgetTab(MangaReaderTab);
                     });

    // MangaReaderWidget
    ui->mangaReaderWidget->setSettings(&core->settings);
    core->mangaController->settings = &core->settings;

    QObject::connect(ui->mangaReaderWidget, &MangaReaderWidget::changeView, this, &MainWidget::setWidgetTab);

    QObject::connect(ui->mangaReaderWidget, &MangaReaderWidget::advancPageClicked, core->mangaController,
                     &MangaController::advanceMangaPage);

    QObject::connect(ui->mangaReaderWidget, &MangaReaderWidget::closeApp, this,
                     &MainWidget::on_pushButtonClose_clicked);

    QObject::connect(ui->mangaReaderWidget, &MangaReaderWidget::back, this, &MainWidget::readerGoBack);

    QObject::connect(ui->mangaReaderWidget, &MangaReaderWidget::frontlightchanged, this,
                     &MainWidget::setFrontLight);

    QObject::connect(ui->mangaReaderWidget, &MangaReaderWidget::gotoIndex, core->mangaController,
                     &MangaController::setCurrentIndex);

    QObject::connect(ui->mangaReaderWidget, &MangaReaderWidget::bookmarkRequested,
                     [this]()
                     {
                         if (core->mangaController->currentManga)
                         {
                             auto manga = core->mangaController->currentManga;
                             core->bookmarkManager.addBookmark(
                                 manga->title, manga->hostname,
                                 core->mangaController->currentIndex.chapter,
                                 core->mangaController->currentIndex.page);
                             showErrorMessage("Bookmarked!");
                         }
                     });

    QObject::connect(
        core->networkManager, &NetworkManager::downloadedImage, ui->mangaReaderWidget,
        qOverload<const QString &, QSharedPointer<QImage> >(&MangaReaderWidget::addImageToCache));

    // SettingsDialog
    QObject::connect(settingsDialog, &SettingsDialog::activeMangasChanged, core,
                     &UltimateMangaReaderCore::updateActiveScources);

    QObject::connect(settingsDialog, &SettingsDialog::mangaOrderMethodChanged, core,
                     &UltimateMangaReaderCore::sortMangaLists);

    QObject::connect(settingsDialog, &SettingsDialog::ditheringMethodChanged, this,
                     &MainWidget::updateDitheringMode);

    // SuspendManager

    QObject::connect(core->suspendManager, &SuspendManager::suspending, this, &MainWidget::onSuspend);
    QObject::connect(core->suspendManager, &SuspendManager::resuming, this, &MainWidget::onResume);
}

MainWidget::~MainWidget()
{
    // Cancel all async work before destroying widgets
    core->enableTimers(false);
    core->mangaController->cancelAllPreloads();
    core->mangaChapterDownloadManager->cancelDownloads();

    // Final state save
    core->settings.serialize();
    core->readingStats.serialize();
    if (core->aniList)
        core->aniList->serialize();

    delete ui;
}

void MainWidget::adjustUI()
{
    ui->pushButtonFavorites->setProperty("type", "borderless");
    ui->pushButtonHome->setProperty("type", "borderless");

    ui->pushButtonFavorites->setFixedHeight(SIZES.buttonSize);
    ui->pushButtonHome->setFixedHeight(SIZES.buttonSize);

    // Replace Close with Back arrow on the left side
    ui->pushButtonClose->setText("\342\206\220 Back");  // ← Back
    ui->pushButtonClose->setProperty("type", "borderless");
    ui->pushButtonClose->setFixedHeight(SIZES.buttonSize);
    // Reorder: [< Back | Home | Favorites]
    auto *btnLayout = qobject_cast<QHBoxLayout *>(ui->frameButtons->layout());
    if (btnLayout)
    {
        // Remove all widgets from layout
        while (btnLayout->count() > 0)
            btnLayout->takeAt(0);

        // Re-add in new order with separators
        btnLayout->addWidget(ui->pushButtonClose);
        ui->frame_2->setStyleSheet("color: #ccc;");
        ui->frame_2->show();
        btnLayout->addWidget(ui->frame_2);
        btnLayout->addWidget(ui->pushButtonHome);
        ui->frame_3->setStyleSheet("color: #ccc;");
        ui->frame_3->show();
        btnLayout->addWidget(ui->frame_3);
        btnLayout->addWidget(ui->pushButtonFavorites);
    }

    // Reconnect Close button to go back instead of quit
    disconnect(ui->pushButtonClose, nullptr, nullptr, nullptr);
    connect(ui->pushButtonClose, &QPushButton::clicked, this, &MainWidget::readerGoBack);

    ui->toolButtonMenu->setFixedSize(QSize(SIZES.menuIconSize, SIZES.menuIconSize));
    ui->toolButtonMenu->setIconSize(QSize(SIZES.menuIconSize, SIZES.menuIconSize));
    ui->toolButtonWifiIcon->setIconSize(QSize(SIZES.wifiIconSize, SIZES.wifiIconSize));

    ui->labelTitle->setText("UMR");
    ui->labelTitle->setStyleSheet("font-weight: bold;");
    ui->labelTitle->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    ui->labelTitle->setMinimumWidth(0);


#ifdef KOBO
    koboDevice = KoboPlatformFunctions::getKoboDeviceDescriptor();
    // Use logical screen size, not physical pixels
    // Physical: 1264x1680, but Qt works in logical coords (455x605 at DPR 2.78)
    auto *screen = QApplication::primaryScreen();
    if (screen)
    {
        auto logicalSize = screen->geometry().size();
        this->setFixedSize(logicalSize);
        qDebug() << "Kobo: using logical screen size:" << logicalSize
                 << "physical:" << koboDevice.width << "x" << koboDevice.height
                 << "DPR:" << qApp->devicePixelRatio();
    }
    else
    {
        this->resize(koboDevice.width, koboDevice.height);
    }
#endif
}

void MainWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    qDebug() << "MainWidget size:" << this->size() << "geometry:" << this->geometry()
             << "screen:" << (QApplication::primaryScreen() ? QApplication::primaryScreen()->geometry() : QRect())
             << "devicePixelRatio:" << qApp->devicePixelRatio();
    core->updateActiveScources();
    updateDitheringMode();

#ifdef KOBO
    // Enable USB networking on startup if configured
    if (core->settings.usbNetworkMode)
    {
        QProcess::startDetached("sh", {"-c",
            "insmod /drivers/$(uname -r)/g_ether.ko 2>/dev/null; "
            "ifconfig usb0 192.168.2.2 netmask 255.255.255.0 up 2>/dev/null; "
            "telnetd -l /bin/sh 2>/dev/null"});
        qDebug() << "USB network mode enabled (192.168.2.2)";
    }

    // Start file server on boot if configured
    if (core->settings.ftpServerEnabled)
    {
        QProcess::startDetached("sh", {"-c",
            "busybox httpd -p 8080 -h /mnt/onboard/.adds/UltimateMangaReader/ 2>/dev/null || "
            "busybox tcpsvd 0.0.0.0 2121 busybox ftpd -w /mnt/onboard/.adds/UltimateMangaReader/ 2>/dev/null &"});
        qDebug() << "File server enabled on port 8080";
    }
#endif

    // Check if we just completed an update
    {
        QFile markerFile(CONF.cacheDir + "update_complete.txt");
        if (markerFile.exists() && markerFile.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            auto lines = QString(markerFile.readAll()).trimmed();
            markerFile.close();
            // Don't delete marker yet — only after user acknowledges

            auto parts = lines.split('\n');
            auto version = parts.isEmpty() ? "" : parts.first().trimmed();
            auto notes = parts.size() > 1 ? parts.mid(1).join('\n').trimmed() : "";

            QTimer::singleShot(300, this, [this, version, notes]()
            {
                QDialog dlg(this);
                dlg.setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
                dlg.resize(this->size());
                dlg.move(this->pos());

                auto *layout = new QVBoxLayout(&dlg);
                layout->setContentsMargins(10, 8, 10, 8);
                layout->setSpacing(8);

                layout->addStretch();

                auto *titleLbl = new QLabel("<b>Update Complete!</b>", &dlg);
                titleLbl->setAlignment(Qt::AlignCenter);
                layout->addWidget(titleLbl);

                auto *versionLbl = new QLabel(
                    "You are now running v" + (version.isEmpty() ? Updater::currentVersion() : version),
                    &dlg);
                versionLbl->setAlignment(Qt::AlignCenter);
                layout->addWidget(versionLbl);

                if (!notes.isEmpty())
                {
                    auto *notesLbl = new QLabel(&dlg);
                    notesLbl->setWordWrap(true);
                    notesLbl->setStyleSheet("padding: 6px; background: #f5f5f5; border: 1px solid #ddd;");
                    QString displayNotes = notes;
                    if (displayNotes.length() > 400)
                        displayNotes = displayNotes.left(397) + "...";
                    notesLbl->setText(displayNotes);
                    layout->addWidget(notesLbl);
                }

                layout->addStretch();

                auto *okBtn = new QPushButton("OK", &dlg);
                okBtn->setFixedHeight(SIZES.buttonSize);
                okBtn->setStyleSheet("font-weight: bold;");
                connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
                layout->addWidget(okBtn);

                dlg.exec();
                // Only delete marker after user acknowledges — if app crashes or
                // reboots before OK, the dialog will show again next launch
                QFile::remove(CONF.cacheDir + "update_complete.txt");
            });
        }
    }

    // Show welcome screen on first boot
    if (WelcomeDialog::shouldShow())
    {
#ifdef KOBO
        // Set initial brightness: 50% light, full white (no warm tint)
        if (koboDevice.frontlightSettings.hasFrontLight)
        {
            int halfLight = (koboDevice.frontlightSettings.frontlightMin +
                             koboDevice.frontlightSettings.frontlightMax) / 2;
            setFrontLight(halfLight, koboDevice.frontlightSettings.naturalLightMin);
            qDebug() << "First boot: set brightness to" << halfLight;
        }
#endif
        QTimer::singleShot(200, this, [this]()
        {
            WelcomeDialog welcome(this);
            welcome.exec();
        });
    }

    // Background update check: once on first boot, then once per day
    if (core->updater->shouldAutoCheck())
    {
        QTimer::singleShot(10000, this, [this]()
        {
            auto guard = QPointer<MainWidget>(this);
            QtConcurrent::run([this, guard]() {
                if (guard.isNull()) return;
                core->updater->checkForUpdate();

                if (guard.isNull()) return;
                if (!core->updater->updateAvailable())
                    return;
                if (core->updater->isVersionSkipped(core->updater->latestFullSha()))
                    return;

                if (guard.isNull()) return;
                QMetaObject::invokeMethod(this, [this, guard]() {
                    if (guard.isNull()) return;
                    QDialog dlg(this);
                    dlg.setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
                    dlg.resize(this->size());
                    dlg.move(this->pos());

                    auto *layout = new QVBoxLayout(&dlg);
                    layout->setContentsMargins(10, 8, 10, 8);
                    layout->setSpacing(8);

                    layout->addStretch();

                    auto *titleLbl = new QLabel("<b>Update Available</b>", &dlg);
                    titleLbl->setAlignment(Qt::AlignCenter);
                    layout->addWidget(titleLbl);

                    auto *versionLbl = new QLabel(
                        QString("v%1  ->  v%2")
                            .arg(Updater::currentVersion(), core->updater->latestVersion()),
                        &dlg);
                    versionLbl->setAlignment(Qt::AlignCenter);
                    layout->addWidget(versionLbl);

                    QString notes = core->updater->latestNotes();
                    if (notes.length() > 300)
                        notes = notes.left(297) + "...";
                    auto *notesLbl = new QLabel(notes, &dlg);
                    notesLbl->setWordWrap(true);
                    notesLbl->setStyleSheet("color: #555; padding: 6px;");
                    layout->addWidget(notesLbl);

                    layout->addStretch();

                    auto *btnRow = new QHBoxLayout();
                    btnRow->setSpacing(8);

                    auto *skipBtn = new QPushButton("Skip", &dlg);
                    skipBtn->setFixedHeight(SIZES.buttonSize);
                    connect(skipBtn, &QPushButton::clicked, &dlg, [this, &dlg]() {
                        core->updater->skipVersion(core->updater->latestFullSha());
                        dlg.reject();
                    });

                    auto *laterBtn = new QPushButton("Later", &dlg);
                    laterBtn->setFixedHeight(SIZES.buttonSize);
                    connect(laterBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

                    auto *updateBtn = new QPushButton("Update", &dlg);
                    updateBtn->setFixedHeight(SIZES.buttonSize);
                    updateBtn->setStyleSheet("font-weight: bold;");
                    connect(updateBtn, &QPushButton::clicked, &dlg,
                            [this, &dlg, updateBtn, skipBtn, laterBtn, notesLbl]() {
                        updateBtn->setEnabled(false);
                        skipBtn->setEnabled(false);
                        laterBtn->setEnabled(false);
                        notesLbl->setText("Downloading update...\nDo not close the app or remove power.");
#ifdef KOBO
                        QtConcurrent::run([this]() {
                            core->updater->downloadAndApply();
                        });
#else
                        notesLbl->setText("Rebuild from source:\ngit pull && build-win.bat");
#endif
                    });

                    btnRow->addWidget(skipBtn);
                    btnRow->addWidget(laterBtn);
                    btnRow->addWidget(updateBtn);
                    layout->addLayout(btnRow);

                    dlg.exec();
                }, Qt::QueuedConnection);
            });
        });
    }

    // Apply saved frontlight immediately on boot (don't wait for delayed onResume)
#ifdef KOBO
    if (!WelcomeDialog::shouldShow() && koboDevice.frontlightSettings.hasFrontLight)
        setupFrontLight();
#endif

    QTimer::singleShot(500, this, &MainWidget::onResume);

    // Debug screenshot timer
    if (core->settings.debugScreenshots && !screenshotTimer)
    {
        screenshotTimer = new QTimer(this);
        connect(screenshotTimer, &QTimer::timeout, this, &MainWidget::takeDebugScreenshot);
        screenshotTimer->start(30000);  // every 30 seconds
        qDebug() << "Debug screenshots enabled";
    }
}

void MainWidget::takeDebugScreenshot()
{
    auto dir = CONF.cacheDir + "screenshots/";
    QDir().mkpath(dir);

    auto pixmap = this->grab();
    auto filename = dir + QString("screenshot_%1_%2.png")
                             .arg(screenshotIndex++, 4, 10, QChar('0'))
                             .arg(QDateTime::currentDateTime().toString("hhmmss"));

    pixmap.save(filename, "PNG");

    // Keep max 50 screenshots, remove oldest
    QDir screenshotDir(dir);
    auto files = screenshotDir.entryList({"screenshot_*.png"}, QDir::Files, QDir::Name);
    while (files.size() > 50)
    {
        QFile::remove(dir + files.first());
        files.removeFirst();
    }

    // Generate index.html for HTTP browsing
    QFile indexFile(dir + "index.html");
    if (indexFile.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QTextStream out(&indexFile);
        out << "<html><head><title>Screenshots</title>"
            << "<meta http-equiv='refresh' content='30'>"
            << "<style>img{max-width:100%;border:1px solid #ccc;margin:4px}</style>"
            << "</head><body><h3>Debug Screenshots</h3>";
        // Show newest first
        auto allFiles = screenshotDir.entryList({"screenshot_*.png"}, QDir::Files, QDir::Time);
        for (const auto &f : allFiles)
            out << "<a href='" << f << "'><img src='" << f << "' width='300'></a> ";
        out << "</body></html>";
        indexFile.close();
    }
}

bool MainWidget::event(QEvent *event)
{
    if (event->type() == QEvent::KeyPress)
        return buttonPressEvent(static_cast<QKeyEvent *>(event));
    else if (event->type() == QEvent::KeyRelease)
        return buttonReleaseEvent(static_cast<QKeyEvent *>(event));
    return QWidget::event(event);
}

bool MainWidget::buttonReleaseEvent(QKeyEvent *event)
{
    if (event->key() == POWERBUTTON)
    {
        qDebug() << "Power button release";
        powerButtonTimer->stop();

        // Toggle sleep/wake
        if (core->suspendManager->sleeping)
            core->suspendManager->resume();
        else
            core->suspendManager->suspend();

        return true;
    }
    else if (event->key() == SLEEPCOVERBUTTON)
    {
        // Debounce: ignore rapid sleep cover events (noisy magnetic sensor)
        static QElapsedTimer lastCoverEvent;
        if (lastCoverEvent.isValid() && lastCoverEvent.elapsed() < 2000)
            return true;
        lastCoverEvent.restart();

        qDebug() << "Sleep cover opened - resuming";
        if (core->suspendManager->sleeping)
            core->suspendManager->resume();
        return true;
    }

    return false;
}

bool MainWidget::buttonPressEvent(QKeyEvent *event)
{
    if (event->key() == POWERBUTTON)
    {
        // Long press (2s) = shutdown
        powerButtonTimer->start(2000);
        return true;
    }
    else if (event->key() == SLEEPCOVERBUTTON)
    {
        // Debounce: ignore rapid sleep cover events
        static QElapsedTimer lastCoverEvent;
        if (lastCoverEvent.isValid() && lastCoverEvent.elapsed() < 2000)
            return true;
        lastCoverEvent.restart();

        qDebug() << "Sleep cover closed - suspending";
        if (!core->suspendManager->sleeping)
            core->suspendManager->suspend();
        return true;
    }
    // Page buttons scroll lists when not in reader
    else if (event->key() == Qt::Key_PageDown || event->key() == Qt::Key_PageUp ||
             event->key() == Qt::Key_Left || event->key() == Qt::Key_Right)
    {
        // Find the largest visible scrollable area (check active dialog first, then stack)
        QAbstractScrollArea *scrollArea = nullptr;
        QWidget *searchRoot = nullptr;

        // Check if a modal dialog is active
        auto *activeWindow = QApplication::activeWindow();
        if (activeWindow && activeWindow != this)
            searchRoot = activeWindow;
        else
            searchRoot = ui->stackedWidget->currentWidget();

        if (searchRoot)
        {
            int maxHeight = 0;
            for (auto *sa : searchRoot->findChildren<QAbstractScrollArea *>())
            {
                if (sa->isVisible() && sa->height() > maxHeight)
                {
                    maxHeight = sa->height();
                    scrollArea = sa;
                }
            }
        }

        if (scrollArea && scrollArea->verticalScrollBar())
        {
            auto *sb = scrollArea->verticalScrollBar();
            int step = scrollArea->viewport()->height() * 0.8;  // 80% page scroll
            bool down = (event->key() == Qt::Key_PageDown || event->key() == Qt::Key_Right);
            sb->setValue(sb->value() + (down ? step : -step));
            return true;
        }
    }

    return false;
}

void MainWidget::timerTick()
{
#ifdef KOBO
    int bat = KoboPlatformFunctions::getBatteryLevel();
    if (bat < 10)
    {
        qDebug() << "Low battery:" << bat << "%, saving and suspending";
        // Save everything before low-battery shutdown
        core->settings.serialize();
        core->readingStats.stopReading();
        core->readingStats.serialize();
        if (core->aniList) core->aniList->serialize();
        core->saveHistory();

        if (bat < 5)
            close();  // critical - shutdown
        else
            core->suspendManager->suspend();  // low - just sleep
        return;
    }
#endif

    // Only update battery icon every 5 ticks (5 min) to reduce screen redraws
    static int batteryCounter = 0;
    if (++batteryCounter >= 5)
    {
        batteryCounter = 0;
        ui->batteryIcon->updateIcon();
    }

    // Only update menu bar clock if it's visible (avoid unnecessary redraws)
    if (ui->mangaReaderWidget->isVisible() &&
        ui->mangaReaderWidget->findChild<QFrame *>("readerNavigationBar") &&
        ui->mangaReaderWidget->findChild<QFrame *>("readerNavigationBar")->isVisible())
    {
        ui->mangaReaderWidget->updateMenuBar();
    }

    // Bedtime auto-amber: check every tick (1 min) if we're in bedtime window
#ifdef KOBO
    {
        static bool wasBedtime = false;
        static int savedComflight = -1;

        if (core->settings.bedtimeEnabled && koboDevice.frontlightSettings.hasFrontLight)
        {
            int nowMinutes = QTime::currentTime().hour() * 60 + QTime::currentTime().minute();
            int start = core->settings.bedtimeStartMinutes;
            int end = core->settings.bedtimeEndMinutes;

            // Handle wrap-around midnight (e.g. 22:00 to 07:00)
            bool isBedtime;
            if (start < end)
                isBedtime = (nowMinutes >= start && nowMinutes < end);
            else if (start > end)
                isBedtime = (nowMinutes >= start || nowMinutes < end);
            else
                isBedtime = false;  // start == end means disabled

            if (isBedtime && !wasBedtime)
            {
                // Entering bedtime: save user's comfort value, push max amber to hardware only
                savedComflight = core->settings.comflightValue;
                int maxAmber = koboDevice.frontlightSettings.naturalLightMax;
                int comfHw = koboDevice.frontlightSettings.naturalLightInverted
                                 ? 0 : maxAmber;
                KoboPlatformFunctions::setFrontlightLevel(core->settings.lightValue, comfHw);
                qDebug() << "Bedtime: amber set to max" << maxAmber;
            }
            else if (!isBedtime && wasBedtime)
            {
                // Exiting bedtime: restore user's saved comfort light
                if (savedComflight >= 0)
                    core->settings.comflightValue = savedComflight;
                savedComflight = -1;
                setupFrontLight();
                qDebug() << "Bedtime ended: restored comfort light to" << core->settings.comflightValue;
            }
            wasBedtime = isBedtime;
        }
        else if (wasBedtime)
        {
            // Feature was disabled mid-bedtime: restore original comfort light
            if (savedComflight >= 0)
                core->settings.comflightValue = savedComflight;
            savedComflight = -1;
            wasBedtime = false;
            if (koboDevice.frontlightSettings.hasFrontLight)
                setupFrontLight();
        }
    }
#endif

    // Periodic state save every 10 minutes (was 5 - reduces flash writes)
    static int saveCounter = 0;
    if (++saveCounter >= 10)
    {
        saveCounter = 0;
        core->readingStats.serialize();
        core->settings.serialize();
    }
}

void MainWidget::onSuspend()
{
    qDebug() << "Suspending...";

    // AGGRESSIVE SLEEP: stop everything to maximize battery life
    core->enableTimers(false);
    core->mangaController->cancelAllPreloads();
    core->mangaChapterDownloadManager->cancelDownloads();
    wifiDialog->close();

    // Stop ALL timers
    if (screenshotTimer)
        screenshotTimer->stop();
    ui->homeWidget->pauseTimers();

    // Trim image cache to just 3 pages (current + 1 neighbor each side)
    // Keeps instant resume while freeing most RAM
    while (ui->mangaReaderWidget->cacheSize() > 3)
        ui->mangaReaderWidget->trimCache();

    bool keepWifiForDownloads = core->settings.downloadWhileSleeping &&
                                core->mangaChapterDownloadManager->hasActiveDownloads();

    if (!keepWifiForDownloads)
    {
        // Kill all network activity
        core->networkManager->networkAccessManager()->setNetworkAccessible(
            QNetworkAccessManager::NotAccessible);

#ifdef KOBO
        // Kill ALL background processes aggressively
        QProcess::execute("sh", {"-c",
            "killall -9 httpd ftpd tcpsvd telnetd dhcpcd 2>/dev/null"});
#endif
    }
    else
    {
        qDebug() << "Active downloads - keeping WiFi alive during sleep";
    }

    // Save all state before sleeping
    core->settings.serialize();
    core->readingStats.stopReading();
    core->readingStats.serialize();
    if (core->aniList)
        core->aniList->serialize();
    core->saveHistory();

    // Set sleep screen info
    if (core->mangaController->currentManga)
    {
        screensaverDialog->setCurrentManga(
            core->mangaController->currentManga,
            core->mangaController->currentIndex.chapter,
            core->mangaController->currentIndex.page);
    }

    int bat = 100;
    bool isCharging = false;
#ifdef KOBO
    bat = KoboPlatformFunctions::getBatteryLevel();
    isCharging = KoboPlatformFunctions::isBatteryCharging();
#endif
    screensaverDialog->setBatteryLevel(bat, isCharging);

    screensaverDialog->showRandomScreensaver();

    disableFrontLight();

    // WiFi already killed aggressively above - just update state
    core->networkManager->connected = false;
}

void MainWidget::onResume()
{
    qDebug() << "Resuming...";

    screensaverDialog->close();

    // Force full screen repaint after screensaver closes
    this->repaint();
    qApp->processEvents();

    // === Phase 1: Immediate - restore core app state ===
    core->enableTimers(true);

    // Re-enable network stack
    core->networkManager->networkAccessManager()->setNetworkAccessible(
        QNetworkAccessManager::Accessible);

    // Restart debug timers
    if (screenshotTimer && core->settings.debugScreenshots)
        screenshotTimer->start(30000);
    ui->homeWidget->resumeTimers();

    // Update battery icon immediately
    ui->batteryIcon->updateIcon();

    // Resume reading stats if user was in reader
    if (ui->stackedWidget->currentIndex() == MangaReaderTab &&
        core->mangaController->currentManga)
    {
        core->readingStats.startReading(core->mangaController->currentManga->title);
    }

    // Refresh home page data on next visit
    ui->homeWidget->aniListCacheValid = false;

    // === Phase 2: Delayed 1s - restore hardware ===
    static bool firstResume = true;
    QTimer::singleShot(1000, this, [this]()
    {
        // Restore frontlight
        if (firstResume)
        {
            firstResume = false;
            // First resume: set hardware + sync sliders from saved settings
            setupFrontLight();
        }
        else
        {
            // Subsequent resumes: restore from settings, repeat after 500ms
            // to handle hardware that needs a delayed re-apply
            setupFrontLight();
            QTimer::singleShot(500, this, [this]() { setupFrontLight(); });
        }

#ifdef KOBO
        // Critical battery check
        int bat = KoboPlatformFunctions::getBatteryLevel();
        if (bat < 5)
        {
            qDebug() << "Critical battery:" << bat << "%, shutting down";
            core->settings.serialize();
            core->readingStats.serialize();
            if (core->aniList) core->aniList->serialize();
            close();
            return;
        }
#endif
    });

    // === Phase 3: Delayed 3s - reconnect network silently ===
    QTimer::singleShot(3000, this, [this]()
    {
#ifdef KOBO
        // Reconnect WiFi in background - no popup
        QtConcurrent::run([this]() {
            core->networkManager->connectWifi();

            QMetaObject::invokeMethod(this, [this]() {
                if (core->networkManager->connected)
                {
                    qDebug() << "WiFi reconnected after resume";

                    // Restart file server if enabled
                    if (core->settings.ftpServerEnabled)
                    {
                        QProcess::startDetached("sh", {"-c",
                            "busybox httpd -p 8080 -h /mnt/onboard/.adds/UltimateMangaReader/ 2>/dev/null || "
                            "busybox tcpsvd 0.0.0.0 2121 busybox ftpd -w /mnt/onboard/.adds/UltimateMangaReader/ 2>/dev/null &"});
                    }

                    // Restart USB network if enabled
                    if (core->settings.usbNetworkMode)
                    {
                        QProcess::startDetached("sh", {"-c",
                            "insmod /drivers/$(uname -r)/g_ether.ko 2>/dev/null; "
                            "ifconfig usb0 192.168.2.2 netmask 255.255.255.0 up 2>/dev/null; "
                            "telnetd -l /bin/sh 2>/dev/null"});
                    }

                    // Sync AniList
                    if (core->aniList && core->aniList->isLoggedIn())
                        QTimer::singleShot(1000, core->aniList, &AniList::syncOfflineChanges);
                }
                else
                {
                    // Silent notification - not a full-screen popup
                    showErrorMessage("WiFi not available");
                }

                // Update WiFi icon
                auto icon = core->networkManager->connected ? wifiIcons[1] : wifiIcons[0];
                ui->toolButtonWifiIcon->setIcon(icon);
            }, Qt::QueuedConnection);
        });
#else
        // Desktop: always connected
        core->networkManager->connected = true;
#endif
    });
}

void MainWidget::setupVirtualKeyboard()
{
    virtualKeyboard->hide();
    ui->verticalLayoutKeyboardContainer->insertWidget(0, virtualKeyboard);

    ui->homeWidget->installEventFilter(this);
}

void MainWidget::enableVirtualKeyboard(bool enabled)
{
#ifdef KOBO
    if (enabled)
        virtualKeyboard->show();
    else
        virtualKeyboard->hide();
#else
    Q_UNUSED(enabled)
#endif
}

void MainWidget::disableFrontLight()
{
#ifdef KOBO
    KoboPlatformFunctions::setFrontlightLevel(0, 0);
#endif
}

void MainWidget::updateDitheringMode()
{
#ifdef KOBO
    if (core->settings.ditheringMode == NoDithering)
        KoboPlatformFunctions::enableDithering(false, false);
    else
        KoboPlatformFunctions::enableDithering(true, false);
#endif
}

void MainWidget::setupFrontLight()
{
    setFrontLight(core->settings.lightValue, core->settings.comflightValue);

#ifdef KOBO
    ui->mangaReaderWidget->setFrontLightPanelState(
        koboDevice.frontlightSettings.frontlightMin, koboDevice.frontlightSettings.frontlightMax,
        core->settings.lightValue, koboDevice.frontlightSettings.naturalLightMin,
        koboDevice.frontlightSettings.naturalLightMax, core->settings.comflightValue);
#endif
}

void MainWidget::setFrontLight(int light, int comflight)
{
#ifdef KOBO
    if (!koboDevice.frontlightSettings.hasFrontLight)
        return;

    if (koboDevice.frontlightSettings.naturalLightInverted)
        comflight = koboDevice.frontlightSettings.naturalLightMax - comflight;
    KoboPlatformFunctions::setFrontlightLevel(light, comflight);
#endif

    if (core->settings.lightValue != light || core->settings.comflightValue != comflight)
    {
        core->settings.lightValue = light;
        core->settings.comflightValue = comflight;

        core->settings.scheduleSerialize();
    }
}

void MainWidget::showErrorMessage(const QString &message)
{
    if (core->settings.hideErrorMessages)
        return;

    errorMessageWidget->showError(message);
    qDebug() << "Error occured:" << message;
}

void MainWidget::on_pushButtonHome_clicked()
{
    setWidgetTab(HomeTab);
}

void MainWidget::on_pushButtonFavorites_clicked()
{
    setWidgetTab(FavoritesTab);
}

void MainWidget::on_pushButtonClose_clicked()
{
    // No-op: back navigation is handled by the manual connection
    // in adjustUI (line: connect pushButtonClose -> readerGoBack).
    // This slot exists only to satisfy Qt's auto-connect naming convention.
}

void MainWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);

    errorMessageWidget->setFixedWidth(this->width());

#ifdef KOBO
    core->networkManager->setDownloadSettings({koboDevice.width, koboDevice.height}, &core->settings);
#else
    core->networkManager->setDownloadSettings(this->size() * qApp->devicePixelRatio(), &core->settings);
#endif
}

void MainWidget::setWidgetTab(WidgetTab tab)
{
    enableVirtualKeyboard(false);
    core->mangaController->cancelAllPreloads();
    core->readingStats.stopReading();

    int currentIdx = ui->stackedWidget->currentIndex();
    if (tab == currentIdx)
        return;

    // Push current tab to history (but don't push duplicates)
    auto currentTab = static_cast<WidgetTab>(currentIdx);
    if (tabHistory.isEmpty() || tabHistory.last() != currentTab)
        tabHistory.append(currentTab);
    // Keep history bounded
    while (tabHistory.size() > 10)
        tabHistory.removeFirst();

    switch (tab)
    {
        case HomeTab:
            ui->navigationBar->setVisible(true);
            ui->frameHeader->setVisible(true);
            lastTab = HomeTab;
            // Force fresh AniList data on every home return
            ui->homeWidget->aniListCacheValid = false;
            ui->homeWidget->refreshHomeView();
            break;
        case FavoritesTab:
            ui->favoritesWidget->showFavoritesList();
            ui->navigationBar->setVisible(true);
            ui->frameHeader->setVisible(true);
            lastTab = FavoritesTab;
            break;
        case MangaInfoTab:
            ui->navigationBar->setVisible(true);
            ui->frameHeader->setVisible(false);
            lastTab = MangaInfoTab;
            break;
        case MangaReaderTab:
            ui->navigationBar->setVisible(false);
            ui->frameHeader->setVisible(false);
            if (core->mangaController->currentManga)
                core->readingStats.startReading(core->mangaController->currentManga->title);
            break;
        case DownloadsTab:
            ui->navigationBar->setVisible(false);
            ui->frameHeader->setVisible(false);
            downloadQueueWidget->updateSleepDownloadStatus(core->settings.downloadWhileSleeping);
            break;
    }
    ui->frameHeader->repaint();

    ui->batteryIcon->updateIcon();
    ui->stackedWidget->setCurrentIndex(tab);

    QWidget *w = ui->stackedWidget->currentWidget();
    if (w)
        w->setFocus();
}

void MainWidget::readerGoBack()
{
    // Debounce - prevent double-fire from multiple signal connections
    static bool navigating = false;
    if (navigating) return;
    navigating = true;
    QTimer::singleShot(300, [](){ navigating = false; });

    auto current = static_cast<WidgetTab>(ui->stackedWidget->currentIndex());

    switch (current)
    {
        case MangaReaderTab:
            // Reader -> Manga Info (if manga loaded) or Home
            if (core->mangaController->currentManga)
            {
                core->readingStats.stopReading();
                setWidgetTab(MangaInfoTab);
            }
            else
                setWidgetTab(HomeTab);
            break;

        case MangaInfoTab:
        case FavoritesTab:
        case DownloadsTab:
            // All go back to Home
            setWidgetTab(HomeTab);
            break;

        case HomeTab:
        default:
            // Already at home, do nothing
            break;
    }
}

bool MainWidget::eventFilter(QObject *, QEvent *event)
{
    if (event->type() == QEvent::RequestSoftwareInputPanel + 1000)
    {
        enableVirtualKeyboard(true);
        return true;
    }
    else if (event->type() == QEvent::CloseSoftwareInputPanel + 1000)
    {
        enableVirtualKeyboard(false);
        return true;
    }

    if (event->type() == QEvent::KeyPress)
        return buttonPressEvent(static_cast<QKeyEvent *>(event));
    else if (event->type() == QEvent::KeyRelease)
        return buttonReleaseEvent(static_cast<QKeyEvent *>(event));

    return false;
}

void MainWidget::on_toolButtonMenu_clicked()
{
    menuDialog->move(this->mapToGlobal(QPoint{0, 0}));
    menuDialog->open();
}

void MainWidget::menuDialogButtonPressed(MenuButton button)
{
    switch (button)
    {
        case ExitButton:
        {
            // Stop everything immediately
            core->enableTimers(false);
            core->mangaController->cancelAllPreloads();
            core->mangaChapterDownloadManager->cancelDownloads();

            // Stop screenshot timer
            if (screenshotTimer)
                screenshotTimer->stop();

            // Kill all pending network requests
            core->networkManager->networkAccessManager()->setNetworkAccessible(
                QNetworkAccessManager::NotAccessible);

            // Note: autoboot preference is preserved across exits.
            // KFMon's on_failure=nickel handles crash recovery.

            // Save state
            core->settings.serialize();
            core->readingStats.serialize();
            if (core->aniList)
                core->aniList->serialize();

#ifdef KOBO
            // Restart Nickel from scratch and restore framebuffer
            QProcess::execute("sh", {"-c",
                "/mnt/onboard/.adds/UltimateMangaReader/fbdepth -d 32 2>/dev/null;"
                "LIBC_FATAL_STDERR_=1 /usr/local/Kobo/nickel -platform kobo -skipFontLoad &"});
#endif

            // Force quit - don't wait for pending events
            qApp->quit();
            break;
        }
        case SettingsButton:
            settingsDialog->installEventFilter(this);
            settingsDialog->open();
            break;
        case ClearDownloadsButton:
            clearCacheDialog->open();
            break;
        case UpdateMangaListsButton:
            updateMangaListsDialog->open();
            break;
        case AboutButton:
            aboutDialog->open();
            break;
        case HistoryButton:
        {
            QDialog dlg(this);
            dlg.setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
            dlg.resize(this->size()); dlg.move(this->pos());
            dlg.installEventFilter(this);  // Page buttons scroll via eventFilter

            auto *layout = new QVBoxLayout(&dlg);
            layout->setContentsMargins(10, 8, 10, 8);

            auto *titleLbl = new QLabel("<b>History & Stats</b>", &dlg);
            titleLbl->setAlignment(Qt::AlignCenter);
            titleLbl->setStyleSheet("font-weight: bold; padding: 4px;");
            layout->addWidget(titleLbl);

            // Stats summary
            auto &stats = core->readingStats;
            QString statsText = QString(
                "<div style='padding:6px; background:#f8f8f8; border:1px solid #ddd; border-radius:4px;'>"
                "<b>Reading Stats</b><br>"
                "Chapters: %1 | Pages: %2 | Time: %3 min<br>"
                "Streak: %4 days (best: %5) | Top: %6"
                "</div>")
                .arg(stats.totalChaptersRead()).arg(stats.totalPagesRead())
                .arg(stats.totalMinutesRead()).arg(stats.currentStreak())
                .arg(stats.longestStreak())
                .arg(stats.mostReadManga().isEmpty() ? "N/A" : stats.mostReadManga());
            auto *statsLabel = new QLabel(statsText, &dlg);
            statsLabel->setWordWrap(true);
            layout->addWidget(statsLabel);

            // Bookmarks section
            auto &bm = core->bookmarkManager;
            if (!bm.allBookmarks().isEmpty())
            {
                auto *bmHeader = new QLabel("<b>Bookmarks</b>", &dlg);
                bmHeader->setStyleSheet("font-weight: bold; padding-top: 4px;");
                layout->addWidget(bmHeader);

                auto *bmList = new QListWidget(&dlg);
                bmList->setStyleSheet("");
                bmList->setMaximumHeight(120);
                activateScroller(bmList);

                for (int i = 0; i < bm.allBookmarks().size(); i++)
                {
                    const auto &b = bm.allBookmarks()[i];
                    auto text = b.mangaTitle + " Ch." + QString::number(b.chapter + 1) +
                                " Pg." + QString::number(b.page + 1);
                    if (!b.note.isEmpty())
                        text += " - " + b.note;
                    auto *item = new QListWidgetItem(text, bmList);
                    item->setData(Qt::UserRole, i);
                }
                layout->addWidget(bmList);
            }

            // History list
            auto *histHeader = new QLabel("<b>Browsing History</b>", &dlg);
            histHeader->setStyleSheet("font-weight: bold; padding-top: 4px;");
            layout->addWidget(histHeader);

            auto *list = new QListWidget(&dlg);
            list->setStyleSheet("");
            activateScroller(list);

            for (const auto &h : core->browsingHistory)
            {
                auto text = h.title + "  [" + h.sourceName + "]";
                if (h.timestamp.isValid())
                    text += "  " + h.timestamp.toString("MM/dd hh:mm");
                list->addItem(text);
            }
            if (core->browsingHistory.isEmpty())
                list->addItem("No history yet");

            layout->addWidget(list, 1);

            auto *closeBtn = new QPushButton("Close", &dlg);
            closeBtn->setFixedHeight(SIZES.buttonSize);
            connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::close);
            layout->addWidget(closeBtn);

            dlg.exec();
            break;
        }
        case AniListButton:
        {
            // Show AniList management dialog - full screen like history
            QDialog dlg(this);
            dlg.setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
            dlg.resize(this->size()); dlg.move(this->pos());
            dlg.installEventFilter(this);  // Page buttons scroll via eventFilter

            auto *layout = new QVBoxLayout(&dlg);
            layout->setContentsMargins(10, 8, 10, 8);

            auto *title = new QLabel("<b>AniList</b>", &dlg);
            title->setAlignment(Qt::AlignCenter);
            title->setStyleSheet("font-weight: bold; padding: 4px;");
            layout->addWidget(title);

            if (core->aniList && core->aniList->isLoggedIn())
            {
                auto *status = new QLabel("Logged in as: " + core->aniList->username(), &dlg);
                status->setAlignment(Qt::AlignCenter);
                layout->addWidget(status);

                auto *refreshBtn = new QPushButton("Refresh List", &dlg);
                refreshBtn->setFixedHeight(SIZES.buttonSize);
                connect(refreshBtn, &QPushButton::clicked, &dlg, [this, &dlg]()
                {
                    core->aniList->fetchMangaList();
                    dlg.close();
                });
                layout->addWidget(refreshBtn);

                auto *list = new QListWidget(&dlg);
                list->setStyleSheet("");
                activateScroller(list);

                int statuses[] = {1, 2, 5, 3, 4, 6};
                for (int s : statuses)
                {
                    auto entries = core->aniList->entriesByStatus(s);
                    if (entries.isEmpty())
                        continue;

                    auto *header = new QListWidgetItem("-- " + AniList::statusName(s) +
                                  " (" + QString::number(entries.size()) + ") --");
                    header->setForeground(QColor(100, 100, 100));
                    list->addItem(header);

                    for (const auto &e : entries)
                    {
                        QString info = "  " + e.title + "  Ch." + QString::number(e.progress);
                        if (e.totalChapters > 0)
                            info += "/" + QString::number(e.totalChapters);
                        if (e.score > 0)
                            info += "  Score:" + QString::number(e.score);
                        auto *item = new QListWidgetItem(info);
                        item->setData(Qt::UserRole, e.title);
                        list->addItem(item);
                    }
                }

                // Click entry to open directly or search
                connect(list, &QListWidget::itemClicked, &dlg, [this, &dlg](QListWidgetItem *item)
                {
                    auto title = item->data(Qt::UserRole).toString();
                    if (title.isEmpty())
                        return;

                    dlg.close();

                    // Try to find cached manga info locally first
                    for (const auto &ms : core->mangaSources)
                    {
                        auto infoDir = CONF.mangasourcedir(ms->name);
                        QDir dir(infoDir);
                        for (const auto &mangaDir : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
                        {
                            auto infoPath = infoDir + mangaDir + "/mangainfo.dat";
                            if (!QFile::exists(infoPath))
                                continue;
                            // Check if the cached title matches
                            auto clean = mangaDir.toLower().replace("-", " ");
                            if (clean.contains(title.toLower()) || title.toLower().contains(clean))
                            {
                                try
                                {
                                    auto info = MangaInfo::deserialize(ms.get(), infoPath);
                                    if (info && !info->title.isEmpty())
                                    {
                                        core->setCurrentMangaSource(ms.get());
                                        core->mangaController->setCurrentManga(info);
                                        return;
                                    }
                                }
                                catch (...) {}
                            }
                        }
                    }

                    // Not found locally - search for it
                    setWidgetTab(HomeTab);
                    auto *lineEdit = ui->homeWidget->findChild<QLineEdit *>("lineEditFilter");
                    if (lineEdit)
                    {
                        lineEdit->setText(title);
                        QMetaObject::invokeMethod(ui->homeWidget, "on_pushButtonFilter_clicked");
                    }
                });

                layout->addWidget(list, 1);

                auto *logoutBtn = new QPushButton("Logout", &dlg);
                logoutBtn->setFixedHeight(SIZES.buttonSize);
                connect(logoutBtn, &QPushButton::clicked, &dlg, [this, &dlg]()
                {
                    core->aniList->logout();
                    dlg.close();
                });
                layout->addWidget(logoutBtn);
            }
            else
            {
                auto *info = new QLabel("Not logged in.\nGo to Settings to connect.", &dlg);
                info->setAlignment(Qt::AlignCenter);
                info->setStyleSheet("padding: 12px;");
                layout->addWidget(info);
            }

            auto *closeBtn = new QPushButton("Close", &dlg);
            closeBtn->setFixedHeight(SIZES.buttonSize);
            connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::close);
            layout->addWidget(closeBtn);

            dlg.exec();
            break;
        }
    }
}

void MainWidget::showLoadingIndicator()
{
    spinner->start();
    QApplication::processEvents();
}

void MainWidget::hideLoadingIndicator()
{
    spinner->stop();
}

void MainWidget::updateDownloadBadge()
{
    if (activeDownloadCount > 0)
        downloadHeaderBtn->setText(QString::number(activeDownloadCount));
    else
        downloadHeaderBtn->setText("");
}

void MainWidget::on_toolButtonWifiIcon_clicked()
{
    wifiDialog->openFullScreen();
}
