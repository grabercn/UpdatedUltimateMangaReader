#include "mainwidget.h"

#include <QListWidget>
#include <QScreen>
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
                     [this](int b) { menuDialogButtonPressed(static_cast<MenuButton>(b)); });

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

                         showLoadingIndicator();
                         bool success;
                         if (isLN)
                             success = core->exportNovelAsEPUB(m, f, t);
                         else
                         {
                             core->mangaChapterDownloadManager->downloadMangaChapters(m, f, t);
                             success = true;
                         }
                         hideLoadingIndicator();

                         if (success)
                         {
                             downloadQueueWidget->jobCompleted(m->title);
                             activeDownloadCount = qMax(0, activeDownloadCount - 1);
                             updateDownloadBadge();
                         }
                         else
                             downloadQueueWidget->jobFailed(m->title, "Export failed");

                         showErrorMessage("Exporting - check Downloads page");
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
                         // Mark the job as active
                         for (auto &j : downloadQueueWidget->findChildren<QLabel *>())
                             Q_UNUSED(j);  // just need the widget reference
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
                         if (core->mangaController->currentManga)
                             downloadQueueWidget->jobCompleted(
                                 core->mangaController->currentManga->title);
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
                                 {
                                     if (progress.index.page > 0)
                                         completed = currentCh;  // still reading this chapter
                                     else
                                         completed = currentCh;  // moved to next chapter

                                     core->aniList->trackReading(manga->title, completed);
                                 }

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
    delete ui;
}

void MainWidget::adjustUI()
{
    ui->pushButtonFavorites->setProperty("type", "borderless");
    ui->pushButtonHome->setProperty("type", "borderless");

    ui->pushButtonFavorites->setFixedHeight(SIZES.buttonSize);
    ui->pushButtonHome->setFixedHeight(SIZES.buttonSize);

    // Replace Close with Back arrow on the left side
    ui->pushButtonClose->setText("< Back");
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

    // Add battery percentage label next to battery icon in the top bar
    if (ui->batteryIcon->percentLabel)
    {
        auto *topBar = ui->batteryIcon->parentWidget();
        if (topBar && topBar->layout())
        {
            auto *topLayout = qobject_cast<QHBoxLayout *>(topBar->layout());
            if (topLayout)
            {
                // Find battery icon index and insert label after it
                for (int i = 0; i < topLayout->count(); i++)
                {
                    if (topLayout->itemAt(i)->widget() == ui->batteryIcon)
                    {
                        topLayout->insertWidget(i + 1, ui->batteryIcon->percentLabel);
                        break;
                    }
                }
            }
        }
    }

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
#endif

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

    // Always check for updates on startup (after welcome dialog, give SSL time to init)
    {
        QTimer::singleShot(8000, this, [this]()
        {
            core->updater->checkForUpdate();

            if (core->updater->updateAvailable() &&
                !core->updater->isVersionSkipped(core->updater->latestFullSha()))
            {
                // Show update splash dialog
                QDialog updateDlg(this);
                updateDlg.setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
                updateDlg.resize(this->size()); updateDlg.move(this->pos());

                auto *layout = new QVBoxLayout(&updateDlg);
                layout->setContentsMargins(10, 8, 10, 8);
                layout->setSpacing(6);

                auto *titleLbl = new QLabel("<b>Update Available</b>", &updateDlg);
                titleLbl->setAlignment(Qt::AlignCenter);
                layout->addWidget(titleLbl);

                auto *versionLbl = new QLabel(
                    QString("v%1  ->  v%2  (%3)")
                        .arg(Updater::currentVersion(), core->updater->latestVersion(),
                             core->updater->latestDate()),
                    &updateDlg);
                versionLbl->setAlignment(Qt::AlignCenter);
                layout->addWidget(versionLbl);

                auto *notesLbl = new QLabel(&updateDlg);
                notesLbl->setWordWrap(true);
                notesLbl->setAlignment(Qt::AlignTop | Qt::AlignLeft);
                notesLbl->setStyleSheet(
                    "padding: 8px; background: #f8f8f8; "
                    "border: 1px solid #ddd;");
                QString notes = core->updater->latestNotes();
                if (notes.length() > 500)
                    notes = notes.left(497) + "...";
                notesLbl->setText("<b>What's new:</b><br>" + notes.toHtmlEscaped().replace("\n", "<br>"));
                layout->addWidget(notesLbl, 1);

                auto *btnRow = new QHBoxLayout();
                btnRow->setSpacing(6);

                auto *skipBtn = new QPushButton("Skip This Version", &updateDlg);
                skipBtn->setFixedHeight(SIZES.buttonSize);
                connect(skipBtn, &QPushButton::clicked, &updateDlg, [this, &updateDlg]()
                {
                    core->updater->skipVersion(core->updater->latestFullSha());
                    updateDlg.reject();
                });

                auto *updateBtn = new QPushButton("Update Now", &updateDlg);
                updateBtn->setFixedHeight(SIZES.buttonSize);
                updateBtn->setStyleSheet("font-weight: bold;");
                connect(updateBtn, &QPushButton::clicked, &updateDlg, [this, &updateDlg, updateBtn, skipBtn, notesLbl]()
                {
                    updateBtn->setEnabled(false);
                    skipBtn->setEnabled(false);
                    notesLbl->setText("<p style='text-align:center; font-size:12pt;'>"
                                     "Downloading update...<br>Please wait.</p>");
#ifdef KOBO
                    core->updater->downloadAndApply();
#else
                    notesLbl->setText("<p style='text-align:center; font-size:11pt;'>"
                                     "On desktop, please rebuild from source:<br>"
                                     "<b>git pull && build-win.bat</b></p>");
                    updateBtn->setEnabled(true);
                    skipBtn->setEnabled(true);
#endif
                });

                btnRow->addWidget(skipBtn);
                btnRow->addWidget(updateBtn);
                layout->addLayout(btnRow);

                updateDlg.exec();
            }
        });
    }

    QTimer::singleShot(500, this, &MainWidget::onResume);

    // Debug screenshot timer
    if (core->settings.debugScreenshots && !screenshotTimer)
    {
        screenshotTimer = new QTimer(this);
        connect(screenshotTimer, &QTimer::timeout, this, &MainWidget::takeDebugScreenshot);
        screenshotTimer->start(10000);  // every 10 seconds
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
        qDebug() << "Sleep cover closed - suspending";
        if (!core->suspendManager->sleeping)
            core->suspendManager->suspend();
        return true;
    }
    // Page buttons scroll lists when not in reader
    else if (event->key() == Qt::Key_PageDown || event->key() == Qt::Key_PageUp ||
             event->key() == Qt::Key_Left || event->key() == Qt::Key_Right)
    {
        // Find the currently visible scrollable list
        QAbstractScrollArea *scrollArea = nullptr;
        auto *stack = ui->stackedWidget;
        auto *current = stack->currentWidget();
        if (current)
            scrollArea = current->findChild<QAbstractScrollArea *>();

        if (scrollArea && scrollArea->verticalScrollBar())
        {
            auto *sb = scrollArea->verticalScrollBar();
            int step = scrollArea->viewport()->height();
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

    ui->batteryIcon->updateIcon();
    ui->mangaReaderWidget->updateMenuBar();

    // Periodic state save (every tick = every 60s)
    static int saveCounter = 0;
    if (++saveCounter >= 5)  // every 5 minutes
    {
        saveCounter = 0;
        core->readingStats.serialize();
        core->settings.serialize();
    }
}

void MainWidget::onSuspend()
{
    qDebug() << "Suspending...";

    // Stop all background activity
    core->enableTimers(false);
    core->mangaController->cancelAllPreloads();
    core->mangaChapterDownloadManager->cancelDownloads();
    wifiDialog->close();

    // Stop debug screenshot timer
    if (screenshotTimer)
        screenshotTimer->stop();

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
#ifdef KOBO
    bat = KoboPlatformFunctions::getBatteryLevel();
#endif
    screensaverDialog->setBatteryLevel(bat);

    screensaverDialog->showRandomScreensaver();

    disableFrontLight();

    if (core->settings.wifiAutoDisconnect)
        core->networkManager->disconnectWifi();
}

void MainWidget::onResume()
{
    qDebug() << "Resuming...";

    screensaverDialog->close();
    core->enableTimers(true);

    // Restart debug screenshot timer
    if (screenshotTimer && core->settings.debugScreenshots)
        screenshotTimer->start(10000);

    // Reconnect WiFi (silently, no dialog)
    wifiDialog->connect();
    QTimer::singleShot(500, this, [this]()
    {
        setupFrontLight();

#ifdef KOBO
        // Check battery on wake - shutdown if critically low
        int bat = KoboPlatformFunctions::getBatteryLevel();
        if (bat < 5)
        {
            qDebug() << "Critical battery on resume:" << bat << "%, shutting down";
            core->settings.serialize();
            core->readingStats.serialize();
            if (core->aniList) core->aniList->serialize();
            close();
            return;
        }
#endif

        // Sync AniList if online (don't auto-open WiFi dialog - let user tap WiFi icon)
        if (core->networkManager->connected && core->aniList && core->aniList->isLoggedIn())
            QTimer::singleShot(2000, core->aniList, &AniList::syncOfflineChanges);

        // Update battery icon
        ui->batteryIcon->updateIcon();
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
    // Now used as Back button - handled by readerGoBack connection in adjustUI
    readerGoBack();
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
            // Refresh home view to update stars etc
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
    // Pop from history, skip MangaInfoTab if no manga is loaded
    while (!tabHistory.isEmpty())
    {
        auto tab = tabHistory.takeLast();
        if (tab == MangaInfoTab && (!core->mangaController->currentManga))
            continue;  // skip blank info page
        setWidgetTab(tab);
        return;
    }
    // Fallback to home
    setWidgetTab(HomeTab);
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
            core->enableTimers(false);
            core->mangaController->cancelAllPreloads();
            core->mangaChapterDownloadManager->cancelDownloads();
            core->settings.serialize();
            if (core->aniList)
                core->aniList->serialize();
            close();
            break;
        case SettingsButton:
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

            auto *layout = new QVBoxLayout(&dlg);
            layout->setContentsMargins(10, 8, 10, 8);

            auto *titleLbl = new QLabel("<b>History & Stats</b>", &dlg);
            titleLbl->setAlignment(Qt::AlignCenter);
            titleLbl->setStyleSheet("font-weight: bold; padding: 4px;");
            layout->addWidget(titleLbl);

            // Stats summary
            auto &stats = core->readingStats;
            QString statsText = QString(
                "<div style='font-size:10pt; padding:6px; background:#f8f8f8; border:1px solid #ddd; border-radius:4px;'>"
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

            auto *layout = new QVBoxLayout(&dlg);
            layout->setContentsMargins(10, 8, 10, 8);

            auto *title = new QLabel("<b>AniList</b>", &dlg);
            title->setAlignment(Qt::AlignCenter);
            title->setStyleSheet("font-weight: bold; padding: 4px;");
            layout->addWidget(title);

            if (core->aniList->isLoggedIn())
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
