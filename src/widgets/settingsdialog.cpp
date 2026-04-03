#include "settingsdialog.h"

#include <QDir>
#include <QScrollBar>
#include <QSpinBox>
#include <QTextStream>

#include "anilist.h"
#include "updater.h"

#include "ui_settingsdialog.h"

SettingsDialog::SettingsDialog(Settings *settings, AniList *aniList, Updater *updater, QWidget *parent)
    : QDialog(parent), ui(new Ui::SettingsDialog), settings(settings),
      aniList(aniList), updater(updater), internalChange(false)
{
    ui->setupUi(this);
    adjustUI();
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);

    for (auto item : this->findChildren<QComboBox *>())
        QObject::connect(item, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
                         [this](int) { this->updateSettings(); });

    for (auto item : this->findChildren<QCheckBox *>())
        QObject::connect(item, &QCheckBox::clicked, this, &SettingsDialog::updateSettings);

    auto *scrollLayout = ui->scrollArea->widget()->layout();
    auto *vl = qobject_cast<QVBoxLayout *>(scrollLayout);

    // Helper: add a section divider line
    auto addDivider = [&]() {
        auto *line = new QFrame(this);
        line->setFrameShape(QFrame::HLine);
        line->setStyleSheet("color: #ccc; margin: 8px 0;");
        scrollLayout->addWidget(line);
    };

    // Helper: add a section header
    auto addHeader = [&](const QString &text) {
        auto *lbl = new QLabel("<b>" + text + "</b>", this);
        lbl->setStyleSheet("padding-top: 8px; padding-bottom: 2px;");
        scrollLayout->addWidget(lbl);
    };

    // ── Power ──
    addDivider();
    addHeader("Power");

    auto *suspendRow = new QHBoxLayout();
    suspendRow->setSpacing(12);
    auto *suspendLabel = new QLabel("Auto sleep after:", this);
    auto *suspendCombo = new QComboBox(this);
    suspendCombo->addItem("5 min", 5);
    suspendCombo->addItem("10 min", 10);
    suspendCombo->addItem("15 min", 15);
    suspendCombo->addItem("30 min", 30);
    suspendCombo->addItem("60 min", 60);
    suspendCombo->addItem("Never", 0);
    for (int i = 0; i < suspendCombo->count(); i++)
        if (suspendCombo->itemData(i).toInt() == settings->autoSuspendMinutes)
            suspendCombo->setCurrentIndex(i);

    connect(suspendCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, suspendCombo](int idx)
    {
        if (!internalChange)
        {
            this->settings->autoSuspendMinutes = suspendCombo->itemData(idx).toInt();
            this->settings->scheduleSerialize();
        }
    });
    suspendRow->addWidget(suspendLabel);
    suspendRow->addWidget(suspendCombo);
    if (vl) vl->addLayout(suspendRow);

    auto *wifiDisconnect = new QCheckBox("Disconnect WiFi on sleep", this);
    wifiDisconnect->setChecked(settings->wifiAutoDisconnect);
    connect(wifiDisconnect, &QCheckBox::toggled, this, [this](bool checked)
    {
        if (!internalChange)
        {
            this->settings->wifiAutoDisconnect = checked;
            this->settings->scheduleSerialize();
        }
    });
    scrollLayout->addWidget(wifiDisconnect);

#ifdef KOBO
    auto *autoBootCheck = new QCheckBox("Auto-start on boot", this);
    autoBootCheck->setChecked(settings->autoBootEnabled);
    connect(autoBootCheck, &QCheckBox::toggled, this, [this](bool checked)
    {
        if (!internalChange)
        {
            this->settings->autoBootEnabled = checked;
            Settings::writeKfmonAutoboot(checked);
            this->settings->scheduleSerialize();
        }
    });
    scrollLayout->addWidget(autoBootCheck);

    bool hasKfmon = QDir("/mnt/onboard/.adds/kfmon/config").exists();
    bool hasNm = QDir("/mnt/onboard/.adds/nm").exists();
    QString launcherInfo = "Launcher: ";
    if (hasKfmon && hasNm) launcherInfo += "KFMon + NickelMenu";
    else if (hasKfmon) launcherInfo += "KFMon";
    else if (hasNm) launcherInfo += "NickelMenu";
    else launcherInfo += "None";
    auto *launcherLabel = new QLabel(launcherInfo, this);
    launcherLabel->setStyleSheet("color: #888; padding-left: 20px;");
    scrollLayout->addWidget(launcherLabel);
#endif

    // ── Content Sources ──
    addDivider();
    addHeader("Content Sources");

    auto *iaBooksCheck = new QCheckBox("Show general books (Internet Archive)", this);
    iaBooksCheck->setChecked(settings->iaGeneralBooksEnabled);
    connect(iaBooksCheck, &QCheckBox::toggled, this, [this](bool checked)
    {
        if (!internalChange)
        {
            this->settings->iaGeneralBooksEnabled = checked;
            this->settings->scheduleSerialize();
        }
    });
    scrollLayout->addWidget(iaBooksCheck);

    auto *iaBooksNote = new QLabel("Searches all books on archive.org, not just manga/LN. Requires restart.", this);
    iaBooksNote->setWordWrap(true);
    iaBooksNote->setStyleSheet("color: #888; padding-left: 20px;");
    scrollLayout->addWidget(iaBooksNote);

#ifdef DESKTOP
    // ── Debug ──
    addDivider();
    addHeader("Debug");

    auto *offlineCheck = new QCheckBox("Offline Test Mode", this);
    offlineCheck->setChecked(settings->offlineMode);
    connect(offlineCheck, &QCheckBox::toggled, this, [this, offlineCheck](bool checked)
    {
        if (!internalChange)
        {
            this->settings->offlineMode = checked;
            this->settings->scheduleSerialize();
        }
    });
    scrollLayout->addWidget(offlineCheck);
#endif

    // ── AniList ──
    addDivider();
    addHeader("AniList");

    aniListStatusLabel = new QLabel("Not logged in", this);
    aniListStatusLabel->setWordWrap(true);
    aniListStatusLabel->setStyleSheet("padding: 4px 0;");
    scrollLayout->addWidget(aniListStatusLabel);

    auto *instrLabel = new QLabel(
        "To connect: visit the URL below in a browser, authorize,\n"
        "then paste the token from the URL (#access_token=...):\n\n"
        "anilist.co/api/v2/oauth/authorize?client_id=25108&response_type=token", this);
    instrLabel->setWordWrap(true);
    instrLabel->setStyleSheet("color: #555; padding: 6px; "
                              "background: #f8f8f8; border: 1px solid #eee; border-radius: 4px;");
    scrollLayout->addWidget(instrLabel);

    aniListTokenEdit = new CLineEdit(this);
    aniListTokenEdit->setPlaceholderText("Paste token here...");
    aniListTokenEdit->setFixedHeight(SIZES.buttonSize);
    aniListTokenEdit->installEventFilter(this->parentWidget());
    scrollLayout->addWidget(aniListTokenEdit);

    aniListLoginBtn = new QPushButton("Login", this);
    aniListLoginBtn->setFixedHeight(SIZES.buttonSize);
    scrollLayout->addWidget(aniListLoginBtn);

    connect(aniListLoginBtn, &QPushButton::clicked, this, [this]()
    {
        if (this->aniList && this->aniList->isLoggedIn())
        {
            this->aniList->logout();
            aniListStatusLabel->setText("Not logged in");
            aniListLoginBtn->setText("Login");
            aniListTokenEdit->show();
        }
        else if (this->aniList)
        {
            auto token = aniListTokenEdit->text().trimmed();
            if (!token.isEmpty())
            {
                aniListStatusLabel->setText("Logging in...");
                this->aniList->loginWithToken(token);
            }
        }
    });

    if (aniList)
    {
        connect(aniList, &AniList::loginStatusChanged, this, [this](bool loggedIn)
        {
            if (loggedIn)
            {
                aniListStatusLabel->setText("Logged in as: " + this->aniList->username());
                aniListLoginBtn->setText("Logout");
                aniListTokenEdit->hide();
            }
            else
            {
                aniListStatusLabel->setText("Not logged in");
                aniListLoginBtn->setText("Login");
                aniListTokenEdit->show();
            }
        });

        connect(aniList, &AniList::error, this, [this](const QString &msg)
        {
            aniListStatusLabel->setText("Error: " + msg);
        });

        if (aniList->isLoggedIn())
        {
            aniListStatusLabel->setText("Logged in as: " + aniList->username());
            aniListLoginBtn->setText("Logout");
            aniListTokenEdit->hide();
        }
    }

    // ── Updates ──
    addDivider();
    addHeader("Updates");

    auto *versionLabel = new QLabel("Version: " + Updater::currentVersion, this);
    versionLabel->setStyleSheet("color: #666; padding: 2px 0;");
    scrollLayout->addWidget(versionLabel);

    auto *updateStatusLabel = new QLabel("", this);
    updateStatusLabel->setWordWrap(true);
    updateStatusLabel->setStyleSheet("padding: 4px; color: #c00;");
    scrollLayout->addWidget(updateStatusLabel);

    auto *checkUpdateBtn = new QPushButton("Check for Updates", this);
    checkUpdateBtn->setFixedHeight(SIZES.buttonSize);
    scrollLayout->addWidget(checkUpdateBtn);

    auto *applyUpdateBtn = new QPushButton("Download & Apply Update", this);
    applyUpdateBtn->setFixedHeight(SIZES.buttonSize);
    applyUpdateBtn->hide();
    scrollLayout->addWidget(applyUpdateBtn);

    if (updater)
    {
        connect(checkUpdateBtn, &QPushButton::clicked, this, [this, updateStatusLabel, applyUpdateBtn]()
        {
            updateStatusLabel->setText("Checking...");
            this->updater->checkForUpdate();
        });

        connect(updater, &Updater::updateLog, this, [updateStatusLabel](const QString &msg)
        {
            updateStatusLabel->setText(msg);
        });

        connect(updater, &Updater::checkCompleted, this,
                [applyUpdateBtn](bool available)
        {
            applyUpdateBtn->setVisible(available);
        });

        connect(applyUpdateBtn, &QPushButton::clicked, this, [this, updateStatusLabel]()
        {
            updateStatusLabel->setText("Downloading update...");
            this->updater->downloadAndApply();
        });

        connect(updater, &Updater::error, this, [updateStatusLabel](const QString &msg)
        {
            updateStatusLabel->setText("Error: " + msg);
        });
    }
}

SettingsDialog::~SettingsDialog()
{
    delete ui;
}

void SettingsDialog::adjustUI()
{
    // Title
    ui->labelTitle->setStyleSheet("font-weight: bold; padding: 8px 0;");

    // Replace Ok with sticky bottom bar
    ui->pushButtonOk->hide();

    // Create bottom bar outside scroll area
    auto *bottomBar = new QWidget(this);
    bottomBar->setFixedHeight(46);
    bottomBar->setStyleSheet("QWidget { background: #fafafa; border-top: 1px solid #ccc; }");
    auto *barLayout = new QHBoxLayout(bottomBar);
    barLayout->setContentsMargins(12, 4, 12, 4);
    barLayout->setSpacing(10);

    auto *backBtn = new QPushButton("< Back", bottomBar);
    backBtn->setFixedHeight(36);
    backBtn->setStyleSheet("");
    connect(backBtn, &QPushButton::clicked, this, [this]()
    {
        // Discard: reload saved settings
        settings->deserialize();
        close();
    });

    auto *saveBtn = new QPushButton("Save", bottomBar);
    saveBtn->setFixedHeight(36);
    saveBtn->setStyleSheet("font-weight: bold;");
    connect(saveBtn, &QPushButton::clicked, this, [this]()
    {
        settings->serialize();
        close();
    });

    barLayout->addWidget(backBtn);
    barLayout->addStretch();
    barLayout->addWidget(saveBtn);

    // Add bottom bar to the main dialog layout (below scroll area)
    this->layout()->addWidget(bottomBar);

    activateScroller(ui->scrollArea);
    ui->scrollArea->setStyleSheet("QScrollArea { border: none; }");

    // Style all section labels as headers
    ui->label->setStyleSheet("font-weight: bold; padding-top: 6px;");
    ui->label->setText("Sources");
    ui->label_2->setStyleSheet("font-weight: bold; padding-top: 6px;");
    ui->label_2->setText("Page Navigation");

    // Remove verbose descriptions, make labels cleaner
    ui->label_8->setText("Double pages:");
    ui->label_7->setText("Dithering:");
    ui->label_6->setText("Sort by:");
    ui->label_3->setText("Tap:");
    ui->label_4->setText("Buttons:");
    ui->label_5->setText("Swipe:");

    ui->checkBoxTrim->setText("Trim white margins");
    ui->checkBoxManhwaMode->setText("Manhwa mode (vertical scroll)");
    ui->checkBoxHideErrorMessages->setText("Hide error messages");

    // Color mode checkbox - add after dithering
    auto *displayLabel = new QLabel("<b>Display</b>", this);
    displayLabel->setStyleSheet("font-weight: bold; padding-top: 6px;");
    auto *colorCheck = new QCheckBox("Color mode (disable greyscale)", this);
    colorCheck->setChecked(settings->colorMode);
    connect(colorCheck, &QCheckBox::toggled, this, [this](bool checked)
    {
        if (!internalChange)
        {
            this->settings->colorMode = checked;
            this->settings->scheduleSerialize();
        }
    });
    // Insert into scroll area layout
    auto *sLayout = ui->scrollArea->widget()->layout();
    // Find the dithering combo and insert after it
    if (sLayout)
    {
        // Add at the position after checkBoxHideErrorMessages
        auto *vl = qobject_cast<QVBoxLayout *>(sLayout);
        if (vl)
        {
            int idx = -1;
            for (int i = 0; i < vl->count(); i++)
            {
                auto *item = vl->itemAt(i);
                if (item && item->widget() == ui->checkBoxHideErrorMessages)
                { idx = i + 1; break; }
            }
            if (idx >= 0)
            {
                vl->insertWidget(idx, displayLabel);
                vl->insertWidget(idx + 1, colorCheck);

                // Preload settings
                auto *preloadLabel = new QLabel("<b>Preloading</b>", this);
                preloadLabel->setStyleSheet("font-weight: bold; padding-top: 6px;");
                vl->insertWidget(idx + 2, preloadLabel);

                auto *preloadCheck = new QCheckBox("Enable page preloading", this);
                preloadCheck->setChecked(settings->preloadEnabled);
                connect(preloadCheck, &QCheckBox::toggled, this, [this](bool c)
                { if (!internalChange) { settings->preloadEnabled = c; settings->scheduleSerialize(); } });
                vl->insertWidget(idx + 3, preloadCheck);

                auto *pagesRow = new QHBoxLayout();
                pagesRow->addWidget(new QLabel("Pages ahead:", this));
                auto *pagesSpin = new QSpinBox(this);
                pagesSpin->setRange(1, 10);
                pagesSpin->setValue(settings->preloadPages);
                pagesSpin->setFixedHeight(36);
                connect(pagesSpin, QOverload<int>::of(&QSpinBox::valueChanged),
                        this, [this](int v) { if (!internalChange) { settings->preloadPages = v; settings->scheduleSerialize(); } });
                pagesRow->addWidget(pagesSpin);
                pagesRow->addWidget(new QLabel("Ch ahead:", this));
                auto *chSpin = new QSpinBox(this);
                chSpin->setRange(0, 5);
                chSpin->setValue(settings->preloadChapters);
                chSpin->setFixedHeight(36);
                connect(chSpin, QOverload<int>::of(&QSpinBox::valueChanged),
                        this, [this](int v) { if (!internalChange) { settings->preloadChapters = v; settings->scheduleSerialize(); } });
                pagesRow->addWidget(chSpin);
                vl->insertLayout(idx + 4, pagesRow);
            }
        }
    }

    // Set all combo boxes to touch-friendly height
    for (auto *combo : this->findChildren<QComboBox *>())
        combo->setMinimumHeight(36);

    // Set all checkboxes to touch-friendly size
    for (auto *check : this->findChildren<QCheckBox *>())
        check->setMinimumHeight(36);
}

void SettingsDialog::open()
{
    resetUI();
    // Full screen over parent
    if (parentWidget())
    {
        setGeometry(parentWidget()->geometry());
        setFixedSize(parentWidget()->size());
    }
    QDialog::open();
}

void SettingsDialog::resetUI()
{
    internalChange = true;
    ui->checkBoxHideErrorMessages->setChecked(settings->hideErrorMessages);

    ui->comboBoxDoublePageMode->setCurrentIndex(settings->doublePageMode);
    ui->checkBoxTrim->setChecked(settings->trimPages);
    ui->checkBoxManhwaMode->setChecked(settings->manhwaMode);

    ui->comboBoxDithering->setCurrentIndex(settings->ditheringMode / 2);

    ui->comboBoxTab->setCurrentIndex(settings->tabAdvance);
    ui->comboBoxSwipe->setCurrentIndex(settings->swipeAdvance);
    ui->comboBoxHWButton->setCurrentIndex(settings->buttonAdvance);

    ui->comboBoxMangaOrder->setCurrentIndex(settings->mangaOrder);

    setupSourcesList();
    internalChange = false;
}

void SettingsDialog::updateActiveMangasSettings(const QString &name, bool enabled)
{
    if (internalChange)
        return;

    settings->enabledMangaSources[name] = enabled;
    settings->scheduleSerialize();

    emit activeMangasChanged();
}

void SettingsDialog::updateSettings()
{
    if (internalChange)
        return;

    settings->hideErrorMessages = ui->checkBoxHideErrorMessages->isChecked();

    settings->doublePageMode = static_cast<DoublePageMode>(ui->comboBoxDoublePageMode->currentIndex());
    settings->trimPages = ui->checkBoxTrim->isChecked();
    settings->manhwaMode = ui->checkBoxManhwaMode->isChecked();

    auto oldDitheringMode = settings->ditheringMode;

    settings->ditheringMode = static_cast<DitheringMode>(ui->comboBoxDithering->currentIndex() * 2);

    if (oldDitheringMode != settings->ditheringMode)
        emit ditheringMethodChanged();

    settings->tabAdvance = static_cast<AdvancePageGestureDirection>(ui->comboBoxTab->currentIndex());
    settings->swipeAdvance = static_cast<AdvancePageGestureDirection>(ui->comboBoxSwipe->currentIndex());
    settings->buttonAdvance = static_cast<AdvancePageHWButton>(ui->comboBoxHWButton->currentIndex());

    auto oldMangaOrder = settings->mangaOrder;
    settings->mangaOrder = static_cast<MangaOrderMethod>(ui->comboBoxMangaOrder->currentIndex());

    if (oldMangaOrder != settings->mangaOrder)
        emit mangaOrderMethodChanged();

    settings->scheduleSerialize();
}

void SettingsDialog::setupSourcesList()
{
    auto layout = ui->frameMangaScources->layout();

    QLayoutItem *item;
    while ((item = layout->takeAt(0)) != nullptr)
    {
        delete item->widget();
        delete item;
    }

    for (const auto &ms : settings->enabledMangaSources.keys())
    {
        bool enabled = settings->enabledMangaSources[ms];

        QCheckBox *checkbox = new QCheckBox(ms, ui->frameMangaScources);
        checkbox->setChecked(enabled);
        QObject::connect(checkbox, &QCheckBox::clicked, this,
                         [this, checkbox, ms]() { updateActiveMangasSettings(ms, checkbox->isChecked()); });

        layout->addWidget(checkbox);
    }
}

void SettingsDialog::on_pushButtonOk_clicked()
{
    close();
}
