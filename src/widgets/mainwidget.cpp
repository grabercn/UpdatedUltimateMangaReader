#include "mainwidget.h"

#include <QListWidget>
#include <QToolButton>
#include <QVBoxLayout>

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

    // Add download button to header bar
    auto *downloadBtn = new QToolButton(this);
    downloadBtn->setIcon(QIcon(":/images/icons/download.png"));
    downloadBtn->setIconSize(QSize(SIZES.wifiIconSize, SIZES.wifiIconSize));
    downloadBtn->setMinimumSize(QSize(40, 40));
    downloadBtn->setFocusPolicy(Qt::NoFocus);
    downloadBtn->setToolTip("Downloads");
    ui->horizontalLayout_5->insertWidget(3, downloadBtn);
    connect(downloadBtn, &QToolButton::clicked, this,
            [this]() { setWidgetTab(DownloadsTab); });
    ui->batteryIcon->updateIcon();
    setupVirtualKeyboard();

    QObject::connect(powerButtonTimer, &QTimer::timeout, this, &MainWidget::close);

    // Dialogs
    menuDialog = new MenuDialog(this);
    settingsDialog = new SettingsDialog(&core->settings, core->aniList, this);
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
                         setWidgetTab(DownloadsTab);
                     });

    // Export to Kobo device
    QObject::connect(downloadMangaChaptersDialog, &DownloadMangaChaptersDialog::exportToDeviceConfirmed,
                     [this](auto m, auto f, auto t)
                     {
                         bool isLN = m->mangaSource && m->mangaSource->contentType == ContentLightNovel;
                         downloadQueueWidget->addJob(m->title, m->hostname, f, t, true, isLN);

                         showLoadingIndicator();
                         bool success;
                         if (isLN)
                             success = core->exportNovelAsEPUB(m, f, t);
                         else
                         {
                             // First download chapters to cache, then export
                             core->mangaChapterDownloadManager->downloadMangaChapters(m, f, t);
                             success = true;  // CBZ export happens after download completes
                         }
                         hideLoadingIndicator();

                         if (success)
                             downloadQueueWidget->jobCompleted(m->title);
                         else
                             downloadQueueWidget->jobFailed(m->title, "Export failed");

                         setWidgetTab(DownloadsTab);
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
                     });

    // Core
    QObject::connect(core, &UltimateMangaReaderCore::error, this, &MainWidget::showErrorMessage);

    QObject::connect(core, &UltimateMangaReaderCore::timeTick, this, &MainWidget::timerTick);

    // AniList + Favorites
    ui->homeWidget->setAniList(core->aniList);
    ui->homeWidget->setFavoritesManager(core->favoritesManager);

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
                         setWidgetTab(MangaInfoTab);
                         ui->mangaReaderWidget->clearCache();
                     });

    // Auto-track AniList progress when reading
    // Track completed chapters: when moving to ch.N, mark ch.N-1 as completed
    QObject::connect(core->mangaController, &MangaController::currentIndexChanged,
                     [this](const ReadingProgress &progress)
                     {
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
                         QTimer::singleShot(50, core->mangaController, &MangaController::preloadNeighbours);
                     });

    QObject::connect(ui->mangaInfoWidget, &MangaInfoWidget::readMangaContinueClicked,
                     [this]() { setWidgetTab(MangaReaderTab); });

    QObject::connect(ui->mangaInfoWidget, &MangaInfoWidget::downloadMangaClicked,
                     [this]()
                     {
                         downloadMangaChaptersDialog->show(core->mangaController->currentManga,
                                                           core->mangaController->currentIndex.chapter);
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

    ui->labelTitle->setStyleSheet("font-size: 16pt");

#ifdef KOBO
    koboDevice = KoboPlatformFunctions::getKoboDeviceDescriptor();
    this->resize(koboDevice.width, koboDevice.height);
#endif
}

void MainWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    core->updateActiveScources();
    updateDitheringMode();

    QTimer::singleShot(500, this, &MainWidget::onResume);
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
    QTime::currentTime().msec();

    if (event->key() == POWERBUTTON)
    {
        qDebug() << "Powerkey release";
        powerButtonTimer->stop();

        if (!core->suspendManager->sleeping)
            core->suspendManager->suspend();
        else
            core->suspendManager->resume();
    }
    else if (event->key() == SLEEPCOVERBUTTON)
    {
        qDebug() << "Sleepcover opened";
        core->suspendManager->resume();

        return true;
    }

    return false;
}

bool MainWidget::buttonPressEvent(QKeyEvent *event)
{
    if (event->key() == POWERBUTTON)
    {
        qDebug() << "Powerkey press";
        powerButtonTimer->start(2000);
        return true;
    }
    else if (event->key() == SLEEPCOVERBUTTON)
    {
        qDebug() << "Sleepcover closed";
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
    // low battery guard
    if (KoboPlatformFunctions::getBatteryLevel() < 10)
        close();
#endif

    ui->batteryIcon->updateIcon();
    ui->mangaReaderWidget->updateMenuBar();
}

void MainWidget::onSuspend()
{
    core->enableTimers(false);
    wifiDialog->close();

    // Pass current reading info to sleep screen
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
    screensaverDialog->close();
    core->enableTimers(true);

    wifiDialog->connect();
    QTimer::singleShot(200, this,
                       [this]()
                       {
                           setupFrontLight();
                           if (!core->networkManager->connected)
                               wifiDialog->open();
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
            // Show history as a simple dialog with list
            QDialog dlg(this);
            dlg.setWindowFlags(Qt::Popup);
            dlg.setMaximumSize(this->size());
            dlg.resize(this->width() - 20, this->height() - 40);

            auto *layout = new QVBoxLayout(&dlg);
            auto *title = new QLabel("<b>History</b>", &dlg);
            title->setAlignment(Qt::AlignCenter);
            title->setStyleSheet("font-size: 14pt; padding: 8px;");
            layout->addWidget(title);

            auto *list = new QListWidget(&dlg);
            list->setStyleSheet("font-size: 11pt;");
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

            layout->addWidget(list);

            auto *closeBtn = new QPushButton("Close", &dlg);
            closeBtn->setFixedHeight(SIZES.buttonSize);
            connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::close);
            layout->addWidget(closeBtn);

            dlg.exec();
            break;
        }
        case AniListButton:
        {
            // Show AniList management dialog
            QDialog dlg(this);
            dlg.setWindowFlags(Qt::Popup);
            dlg.setMaximumSize(this->size());
            dlg.resize(this->width() - 20, this->height() - 40);

            auto *layout = new QVBoxLayout(&dlg);
            auto *title = new QLabel("<b>AniList</b>", &dlg);
            title->setAlignment(Qt::AlignCenter);
            title->setStyleSheet("font-size: 14pt; padding: 8px;");
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
                list->setStyleSheet("font-size: 10pt;");
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
                info->setStyleSheet("font-size: 12pt; padding: 20px;");
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

void MainWidget::on_toolButtonWifiIcon_clicked()
{
    if (!core->networkManager->checkInternetConnection())
        wifiDialog->open();
}
