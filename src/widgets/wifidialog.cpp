#include "wifidialog.h"

#include <QCheckBox>
#include <QProcess>

#include "clineedit.h"
#include <QApplication>
#include <QScreen>

#ifdef KOBO
#include "koboplatformfunctions.h"
#endif

#include "staticsettings.h"

WifiDialog::WifiDialog(QWidget *parent, NetworkManager *networkManager)
    : QDialog(parent), networkManager(networkManager)
{
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
}

WifiDialog::~WifiDialog()
{
    destroying = true;
    if (lastConnection.isRunning())
        lastConnection.waitForFinished();
    if (lastScan.isRunning())
        lastScan.waitForFinished();
}

void WifiDialog::openFullScreen()
{
    // Size to parent
    if (parentWidget())
    {
        resize(parentWidget()->size());
        move(parentWidget()->pos());
        setFixedSize(parentWidget()->size());
    }

    // Wait for any pending async operations to finish
    if (lastConnection.isRunning())
    {
        qDebug() << "WiFi: waiting for pending connection...";
        lastConnection.waitForFinished();
    }

    // Always rebuild UI for simplicity and to ensure fresh state
    if (layout())
    {
        QLayoutItem *child;
        while ((child = layout()->takeAt(0)) != nullptr)
        {
            if (child->widget())
                child->widget()->deleteLater();
            delete child;
        }
        delete layout();
    }

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(15, 10, 15, 10);
    mainLayout->setSpacing(10);

    // Header
    auto *headerRow = new QHBoxLayout();
    closeBtn = new QPushButton("< Back", this);
    closeBtn->setFixedHeight(SIZES.buttonSize);
    closeBtn->setProperty("type", "borderless");
    QObject::connect(closeBtn, &QPushButton::clicked, this, &WifiDialog::onCloseClicked);
    headerRow->addWidget(closeBtn);

    auto *titleLbl = new QLabel("<b>Network Settings</b>", this);
    titleLbl->setAlignment(Qt::AlignCenter);
    titleLbl->setStyleSheet("font-size: 18pt;");
    headerRow->addWidget(titleLbl, 1);

    auto *placeholder = new QWidget(this);
    placeholder->setFixedWidth(closeBtn->width());
    headerRow->addWidget(placeholder);
    mainLayout->addLayout(headerRow);

    // Master Toggle Row
    auto *toggleFrame = new QFrame(this);
    toggleFrame->setStyleSheet("QFrame { background: #f0f0f0; border-radius: 8px; border: 1px solid #ddd; }");
    auto *toggleLayout = new QHBoxLayout(toggleFrame);

    auto *wifiIcon = new QLabel(this);
    wifiIcon->setPixmap(QIcon(":/images/icons/wifi.png").pixmap(32, 32));
    wifiIcon->setStyleSheet("border: none; background: transparent;");
    toggleLayout->addWidget(wifiIcon);

    auto *toggleText = new QLabel("<b>Enable WiFi Hardware</b>", this);
    toggleText->setStyleSheet("border: none; background: transparent; font-size: 14pt;");
    toggleLayout->addWidget(toggleText, 1);

    toggleBtn = new QPushButton(this);
    toggleBtn->setFixedSize(120, SIZES.buttonSize);
    QObject::connect(toggleBtn, &QPushButton::clicked, this, &WifiDialog::onHardwareToggleClicked);
    toggleLayout->addWidget(toggleBtn);
    mainLayout->addWidget(toggleFrame);

    // Status Area
    auto *statusFrame = new QFrame(this);
    statusFrame->setStyleSheet("QFrame { background: white; border: 1px solid #eee; border-radius: 4px; }");
    auto *statusLayout = new QVBoxLayout(statusFrame);
    statusLabel = new QLabel("Initializing...", this);
    statusLabel->setAlignment(Qt::AlignCenter);
    statusLabel->setStyleSheet("border: none; color: #333; font-size: 12pt;");
    statusLayout->addWidget(statusLabel);
    mainLayout->addWidget(statusFrame);

    // Action Buttons Row
    auto *actionRow = new QHBoxLayout();
    scanBtn = new QPushButton("Scan for Networks", this);
    scanBtn->setFixedHeight(SIZES.buttonSize);
    QObject::connect(scanBtn, &QPushButton::clicked, this, &WifiDialog::onScanClicked);
    actionRow->addWidget(scanBtn, 1);

    auto *disconnectAllBtn = new QPushButton("Disconnect All", this);
    disconnectAllBtn->setFixedHeight(SIZES.buttonSize);
    QObject::connect(disconnectAllBtn, &QPushButton::clicked, this, &WifiDialog::onToggleClicked);
    actionRow->addWidget(disconnectAllBtn, 1);
    mainLayout->addLayout(actionRow);

    // Network list
    networkList = new QListWidget(this);
    networkList->setStyleSheet(
        "QListWidget { border: 1px solid #ddd; background: white; outline: none; }"
        "QListWidget::item { padding: 12px 8px; border-bottom: 1px solid #eee; }"
        "QListWidget::item:selected { background: #eee; color: black; }");
    networkList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    networkList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    networkList->setFocusPolicy(Qt::NoFocus);
    activateScroller(networkList);
    QObject::connect(networkList, &QListWidget::itemClicked, this, &WifiDialog::onNetworkSelected);
    mainLayout->addWidget(networkList, 1);

    updateStatus();
    exec();
}

void WifiDialog::onHardwareToggleClicked()
{
    bool currentlyEnabled = networkManager->isWifiHardwareEnabled();
    bool target = !currentlyEnabled;

    if (statusLabel)
        statusLabel->setText(target ? "Enabling WiFi Hardware..." : "Disabling WiFi Hardware...");
    if (toggleBtn)
        toggleBtn->setEnabled(false);

    lastConnection = QtConcurrent::run([this, target]() {
        networkManager->setWifiHardwareEnabled(target);

        if (destroying) return;
        QMetaObject::invokeMethod(this, [this, target]() {
            if (destroying) return;
            updateStatus();
            if (toggleBtn) toggleBtn->setEnabled(true);
            if (target)
                onScanClicked(); // Auto-scan when enabling
        }, Qt::QueuedConnection);
    });
}

void WifiDialog::connect()
{
    if (statusLabel)
        statusLabel->setText("Connecting...");
    
    lastConnection = QtConcurrent::run([this]() {
        networkManager->connectWifi();
        if (destroying)
            return;
        QMetaObject::invokeMethod(this, [this]()
        {
            if (destroying)
                return;
            updateStatus();
        }, Qt::QueuedConnection);
    });
}

void WifiDialog::onToggleClicked()
{
    if (destroying) return;
    if (!networkManager->isWifiHardwareEnabled()) return;

    if (statusLabel) statusLabel->setText("Disconnecting all networks...");

    lastConnection = QtConcurrent::run([this]() {
        networkManager->disconnectWifi();
        if (destroying) return;
        QMetaObject::invokeMethod(this, [this]() {
            if (destroying) return;
            updateStatus();
            onScanClicked(); // Refresh list to show disconnection
        }, Qt::QueuedConnection);
    });
}

void WifiDialog::onScanClicked()
{
#ifdef KOBO
    if (destroying || !scanBtn || !networkList) return;
    if (!networkManager->isWifiHardwareEnabled()) return;

    scanBtn->setText("Scanning...");
    scanBtn->setEnabled(false);
    networkList->clear();
    networkList->addItem("Searching for nearby networks...");

    lastScan = QtConcurrent::run([this]() {
        scanNetworks();
        if (destroying) return;
        QMetaObject::invokeMethod(this, [this]() {
            if (destroying) return;
            scanBtn->setText("Scan for Networks");
            scanBtn->setEnabled(true);

            networkList->clear();
            for (const auto &net : scannedNetworks)
            {
                // Signal bars string
                QString bars = " [";
                if (net.signal > 80) bars += "||||";
                else if (net.signal > 60) bars += "|||";
                else if (net.signal > 40) bars += "||";
                else bars += "|";
                bars += "]";

                auto *item = new QListWidgetItem(networkList);
                auto *widget = new QWidget(networkList);
                auto *layout = new QHBoxLayout(widget);
                layout->setContentsMargins(10, 5, 10, 5);

                QString labelText = "<b>" + net.ssid + "</b>";
                if (net.connected) labelText += " <small>(Connected)</small>";

                auto *labelSSID = new QLabel(labelText, widget);
                labelSSID->setStyleSheet("font-size: 14pt; border: none; background: transparent;");
                layout->addWidget(labelSSID, 1);

                if (net.secured)
                {
                    auto *lockIcon = new QLabel("🔒", widget);
                    lockIcon->setStyleSheet("border: none; background: transparent;");
                    layout->addWidget(lockIcon);
                }

                auto *labelSignal = new QLabel(bars, widget);
                labelSignal->setStyleSheet("color: #666; font-family: monospace; border: none; background: transparent;");
                layout->addWidget(labelSignal);

                item->setSizeHint(widget->sizeHint());
                item->setData(Qt::UserRole, net.ssid);
                networkList->addItem(item);
                networkList->setItemWidget(item, widget);

                if (net.connected)
                    labelSSID->setStyleSheet("font-size: 14pt; color: #006600; font-weight: bold; border: none; background: transparent;");
            }

            if (scannedNetworks.isEmpty())
                networkList->addItem("No networks found. Check your hardware or try scanning again.");
        }, Qt::QueuedConnection);
    });
#else
    networkList->clear();
    networkList->addItem("Network scanning not available on desktop");
#endif
}

void WifiDialog::onNetworkSelected(QListWidgetItem *item)
{
    if (!item)
        return;

    auto ssid = item->data(Qt::UserRole).toString();
    if (ssid.isEmpty() || ssid.startsWith("No networks") || ssid.startsWith("Searching"))
        return;

#ifdef KOBO
    // Copy data under lock, then release before showing modal dialog
    bool alreadyConnected = false;
    bool secured = false;
    {
        QMutexLocker lock(&wifiMutex);
        for (const auto &net : scannedNetworks)
        {
            if (net.ssid == ssid && net.connected)
            {
                alreadyConnected = true;
                break;
            }
        }
        if (!alreadyConnected)
        {
            for (const auto &net : scannedNetworks)
            {
                if (net.ssid == ssid)
                {
                    secured = net.secured;
                    break;
                }
            }
        }
    }

    if (alreadyConnected)
    {
        if (statusLabel) statusLabel->setText("Already connected to " + ssid);
        return;
    }

    if (secured)
    {
        // Show password dialog - full screen for e-ink touch
        QDialog pwDialog(this);
        pwDialog.setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
        pwDialog.resize(this->size());
        pwDialog.move(this->pos());

        // Install MainWidget's event filter on the password dialog
        pwDialog.installEventFilter(this->parentWidget());

        auto *pwLayout = new QVBoxLayout(&pwDialog);
        pwLayout->setContentsMargins(15, 10, 15, 10);
        pwLayout->setSpacing(10);

        auto *pwTitle = new QLabel("<b>Connect to " + ssid + "</b>", &pwDialog);
        pwTitle->setAlignment(Qt::AlignCenter);
        pwTitle->setStyleSheet("font-size: 16pt;");
        pwLayout->addWidget(pwTitle);

        pwLayout->addSpacing(20);

        auto *pwLabel = new QLabel("Enter WiFi password:", &pwDialog);
        pwLayout->addWidget(pwLabel);

        auto *pwEdit = new CLineEdit(&pwDialog);
        pwEdit->setEchoMode(QLineEdit::Password);
        pwEdit->setPlaceholderText("Tap to type...");
        pwEdit->setFixedHeight(SIZES.buttonSize);
        pwLayout->addWidget(pwEdit);

        auto *showPwCheck = new QCheckBox("Show password", &pwDialog);
        QObject::connect(showPwCheck, &QCheckBox::toggled, pwEdit, [pwEdit](bool show) {
            pwEdit->setEchoMode(show ? QLineEdit::Normal : QLineEdit::Password);
        });
        pwLayout->addWidget(showPwCheck);

        pwLayout->addSpacing(20);

        auto *pwBtnRow = new QHBoxLayout();
        auto *pwCancel = new QPushButton("Cancel", &pwDialog);
        pwCancel->setFixedHeight(SIZES.buttonSize);
        QObject::connect(pwCancel, &QPushButton::clicked, &pwDialog, &QDialog::reject);

        auto *pwConnect = new QPushButton("Connect", &pwDialog);
        pwConnect->setFixedHeight(SIZES.buttonSize);
        pwConnect->setStyleSheet("font-weight: bold; background: #eee;");
        QObject::connect(pwConnect, &QPushButton::clicked, &pwDialog, &QDialog::accept);

        pwBtnRow->addWidget(pwCancel);
        pwBtnRow->addWidget(pwConnect);
        pwLayout->addLayout(pwBtnRow);

        pwLayout->addStretch();

        if (pwDialog.exec() != QDialog::Accepted)
            return;

        auto password = pwEdit->text();
        if (password.isEmpty())
            return;

        connectToNetwork(ssid, password);
    }
    else
    {
        connectToNetwork(ssid, "");
    }
#endif
}

void WifiDialog::onCloseClicked()
{
    close();
}

void WifiDialog::connectToNetwork(const QString &ssid, const QString &password)
{
#ifdef KOBO
    if (statusLabel) statusLabel->setText("Connecting to " + ssid + "...");

    lastConnection = QtConcurrent::run([this, ssid, password]() {
        // Copy wifiInterface under lock for thread safety
        QString iface;
        {
            QMutexLocker lock(&wifiMutex);
            iface = wifiInterface;
        }

        // Add network via wpa_cli
        QProcess proc;
        proc.start("wpa_cli", {"-i", iface, "add_network"});
        proc.waitForFinished(5000);
        auto netId = proc.readAllStandardOutput().trimmed().split('\n').last().trimmed();

        // Validate netId is a number
        bool isNum = false;
        netId.toInt(&isNum);
        if (!isNum)
        {
            qDebug() << "WiFi: add_network failed, got:" << netId;
            if (destroying) return;
            QMetaObject::invokeMethod(this, [this]() {
                if (destroying) return;
                if (statusLabel) statusLabel->setText("Error: wpa_supplicant failed to add network");
            }, Qt::QueuedConnection);
            return;
        }
        qDebug() << "WiFi: adding network" << ssid << "as id" << netId;

        // Set SSID
        QProcess::execute("wpa_cli", {"-i", iface, "set_network", netId,
                          "ssid", QString("\"%1\"").arg(ssid)});

        if (password.isEmpty())
        {
            QProcess::execute("wpa_cli", {"-i", iface, "set_network", netId, "key_mgmt", "NONE"});
        }
        else
        {
            QProcess::execute("wpa_cli", {"-i", iface, "set_network", netId,
                              "psk", QString("\"%1\"").arg(password)});
        }

        QProcess::execute("wpa_cli", {"-i", iface, "enable_network", netId});
        QProcess::execute("wpa_cli", {"-i", iface, "select_network", netId});
        QProcess::execute("wpa_cli", {"-i", iface, "save_config"});

        // Wait for DHCP and connection
        QProcess::execute("sh", {"-c", "udhcpc -i " + iface + " -t 5 -T 3 -n 2>/dev/null &"});
        QThread::sleep(5);
        networkManager->checkInternetConnection();

        if (destroying) return;
        QMetaObject::invokeMethod(this, [this, ssid]() {
            if (destroying) return;
            updateStatus();
            if (statusLabel)
            {
                if (networkManager->connected)
                    statusLabel->setText("Connected to " + ssid);
                else
                    statusLabel->setText("Connection failed. Check password.");
            }
            onScanClicked(); // Update list
        }, Qt::QueuedConnection);
    });
#else
    Q_UNUSED(ssid); Q_UNUSED(password);
#endif
}

void WifiDialog::updateStatus()
{
    bool hwEnabled = networkManager->isWifiHardwareEnabled();

    if (toggleBtn)
    {
        toggleBtn->setText(hwEnabled ? "Turn OFF" : "Turn ON");
        toggleBtn->setStyleSheet(hwEnabled ? "background: #eee; border: 1px solid #ccc; border-radius: 4px;" 
                                          : "background: #ddd; font-weight: bold; border: 1px solid #bbb; border-radius: 4px;");
    }

    if (!hwEnabled)
    {
        if (statusLabel) statusLabel->setText("WiFi Hardware: DISABLED");
        if (scanBtn) scanBtn->setEnabled(false);
        if (networkList)
        {
            networkList->setEnabled(false);
            networkList->clear();
            networkList->addItem("Enable WiFi hardware above to scan for networks.");
        }
        return;
    }

    if (scanBtn) scanBtn->setEnabled(true);
    if (networkList) networkList->setEnabled(true);

    if (networkManager->connected)
    {
        if (statusLabel) statusLabel->setText("Status: Connected to Internet");
    }
    else
    {
        if (statusLabel) statusLabel->setText("Status: WiFi Hardware Enabled");
    }
}

void WifiDialog::scanNetworks()
{
    QMutexLocker lock(&wifiMutex);
    scannedNetworks.clear();

#ifdef KOBO
    // Ensure WiFi hardware is on
    try { KoboPlatformFunctions::enableWiFiConnection(); }
    catch (...) {}

    // Wait for WiFi to fully initialize
    QThread::sleep(2);

    // Detect WiFi interface (usually wlan0 or eth0 on some Kobos)
    {
        QProcess ifProc;
        ifProc.start("sh", {"-c", "ls /sys/class/net/ | grep -E 'wlan|eth' | head -1"});
        ifProc.waitForFinished(3000);
        auto detected = ifProc.readAllStandardOutput().trimmed();
        if (!detected.isEmpty())
            wifiInterface = detected;
    }

    // Ensure wpa_supplicant is running
    {
        QProcess checkWpa;
        checkWpa.start("sh", {"-c", "pidof wpa_supplicant"});
        checkWpa.waitForFinished(3000);
        if (checkWpa.readAllStandardOutput().trimmed().isEmpty())
        {
            qDebug() << "wpa_supplicant not running, starting it...";
            QProcess::execute("sh", {"-c",
                "wpa_supplicant -B -i " + wifiInterface + " -c /etc/wpa_supplicant/wpa_supplicant.conf "
                "-D nl80211,wext 2>/dev/null || "
                "wpa_supplicant -B -i " + wifiInterface + " -c /etc/wpa_supplicant.conf "
                "-D nl80211,wext 2>/dev/null"});
            QThread::sleep(2);
        }
    }

    // Trigger scan
    {
        QProcess scanProc;
        scanProc.start("wpa_cli", {"-i", wifiInterface, "scan"});
        scanProc.waitForFinished(5000);
    }
    QThread::sleep(3);

    // Get results
    QProcess proc;
    proc.start("wpa_cli", {"-i", wifiInterface, "scan_results"});
    proc.waitForFinished(5000);
    auto output = proc.readAllStandardOutput();

    // Get current network
    QProcess statusProc;
    statusProc.start("wpa_cli", {"-i", wifiInterface, "status"});
    statusProc.waitForFinished(5000);
    auto statusOutput = statusProc.readAllStandardOutput();
    QString currentSSID;
    for (const auto &line : statusOutput.split('\n'))
    {
        if (line.startsWith("ssid="))
            currentSSID = QString(line.mid(5)).trimmed();
    }

    // Parse wpa_cli scan results
    for (const auto &line : output.split('\n'))
    {
        auto parts = QString(line).split('\t');
        if (parts.size() < 5)
            continue;
        if (parts[0].startsWith("bssid"))
            continue;

        NetworkInfo net;
        net.ssid = parts[4].trimmed();
        if (net.ssid.isEmpty())
            continue;

        int dbm = parts[2].toInt();
        net.signal = qBound(0, 2 * (dbm + 100), 100);
        net.secured = parts[3].contains("WPA") || parts[3].contains("WEP");
        net.connected = (net.ssid == currentSSID && networkManager->connected);

        bool found = false;
        for (auto &existing : scannedNetworks)
        {
            if (existing.ssid == net.ssid)
            {
                if (net.signal > existing.signal)
                    existing.signal = net.signal;
                if (net.connected)
                    existing.connected = true;
                found = true;
                break;
            }
        }
        if (!found)
            scannedNetworks.append(net);
    }

    std::sort(scannedNetworks.begin(), scannedNetworks.end(),
              [](const NetworkInfo &a, const NetworkInfo &b) {
                  if (a.connected != b.connected) return a.connected;
                  return a.signal > b.signal;
              });
#endif
}
