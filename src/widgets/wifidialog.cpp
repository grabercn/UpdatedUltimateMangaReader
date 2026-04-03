#include "wifidialog.h"

#include <QProcess>
#include <QInputDialog>
#include <QMessageBox>
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
}

void WifiDialog::openFullScreen()
{
    // Size to parent
    if (parentWidget())
    {
        resize(parentWidget()->size());
        move(parentWidget()->pos());
    }

    // Build UI fresh each time (in case screen size changed)
    // Clear existing layout
    if (layout())
    {
        QLayoutItem *item;
        while ((item = layout()->takeAt(0)))
        {
            delete item->widget();
            delete item;
        }
        delete layout();
    }

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 6, 8, 6);
    mainLayout->setSpacing(4);

    // Header
    auto *headerRow = new QHBoxLayout();
    auto *titleLbl = new QLabel("<b>WiFi</b>", this);
    headerRow->addWidget(titleLbl);
    headerRow->addStretch();

    closeBtn = new QPushButton("Close", this);
    closeBtn->setFixedHeight(SIZES.buttonSize);
    QObject::connect(closeBtn, &QPushButton::clicked, this, &WifiDialog::onCloseClicked);
    headerRow->addWidget(closeBtn);
    mainLayout->addLayout(headerRow);

    // Status section
    auto *statusFrame = new QFrame(this);
    statusFrame->setStyleSheet("QFrame { background: #f5f5f5; border: 1px solid #ddd; padding: 6px; }");
    auto *statusLayout = new QHBoxLayout(statusFrame);
    statusLayout->setContentsMargins(6, 4, 6, 4);

    statusIcon = new QLabel(this);
    statusIcon->setFixedWidth(20);
    statusLayout->addWidget(statusIcon);

    statusLabel = new QLabel("Checking...", this);
    statusLabel->setWordWrap(true);
    statusLayout->addWidget(statusLabel, 1);

    toggleBtn = new QPushButton("Connect", this);
    toggleBtn->setFixedHeight(SIZES.buttonSize);
    QObject::connect(toggleBtn, &QPushButton::clicked, this, &WifiDialog::onToggleClicked);
    statusLayout->addWidget(toggleBtn);

    mainLayout->addWidget(statusFrame);

    // Scan button
    scanBtn = new QPushButton("Scan for Networks", this);
    scanBtn->setFixedHeight(SIZES.buttonSize);
    QObject::connect(scanBtn, &QPushButton::clicked, this, &WifiDialog::onScanClicked);
    mainLayout->addWidget(scanBtn);

    // Network list
    networkList = new QListWidget(this);
    networkList->setStyleSheet(
        "QListWidget { border: 1px solid #ddd; }"
        "QListWidget::item { padding: 8px 6px; border-bottom: 1px solid #eee; }");
    networkList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    networkList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QObject::connect(networkList, &QListWidget::itemClicked, this, &WifiDialog::onNetworkSelected);
    mainLayout->addWidget(networkList, 1);

    // Info label
    infoLabel = new QLabel(this);
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("color: #888; padding: 4px;");
#ifdef KOBO
    infoLabel->setText("Tap a network to connect. Networks are managed by the Kobo system.");
#else
    infoLabel->setText("WiFi is managed by your OS. This shows connection status only.");
#endif
    mainLayout->addWidget(infoLabel);

    updateStatus();
    open();
}

void WifiDialog::connect()
{
    if (statusLabel)
        statusLabel->setText("Connecting...");
    if (toggleBtn)
        toggleBtn->setEnabled(false);

    lastConnection = QtConcurrent::run([this]() {
        networkManager->connectWifi();
        if (destroying)
            return;
        QMetaObject::invokeMethod(this, [this]()
        {
            if (destroying)
                return;
            if (statusLabel)
                updateStatus();
            if (toggleBtn)
                toggleBtn->setEnabled(true);
        }, Qt::QueuedConnection);
    });
}

void WifiDialog::onToggleClicked()
{
    if (networkManager->connected)
    {
        // Disconnect
        statusLabel->setText("Disconnecting...");
        toggleBtn->setEnabled(false);
        QtConcurrent::run([this]() {
            networkManager->disconnectWifi();
            if (destroying) return;
            QMetaObject::invokeMethod(this, [this]() {
                if (destroying) return;
                updateStatus();
                toggleBtn->setEnabled(true);
            }, Qt::QueuedConnection);
        });
    }
    else
    {
        connect();
    }
}

void WifiDialog::onScanClicked()
{
#ifdef KOBO
    scanBtn->setText("Scanning...");
    scanBtn->setEnabled(false);
    networkList->clear();

    QtConcurrent::run([this]() {
        scanNetworks();
        if (destroying) return;
        QMetaObject::invokeMethod(this, [this]() {
            if (destroying) return;
            scanBtn->setText("Scan for Networks");
            scanBtn->setEnabled(true);

            networkList->clear();
            for (const auto &net : scannedNetworks)
            {
                QString text = net.ssid;
                if (net.connected)
                    text += "  [Connected]";

                // Signal bars
                QString bars;
                if (net.signal > 75) bars = "||||";
                else if (net.signal > 50) bars = "|||";
                else if (net.signal > 25) bars = "||";
                else bars = "|";

                text += "  " + bars;
                if (net.secured)
                    text += "  [Secured]";

                auto *item = new QListWidgetItem(text, networkList);
                item->setData(Qt::UserRole, net.ssid);
                if (net.connected)
                    item->setForeground(QColor(0, 100, 0));
            }

            if (scannedNetworks.isEmpty())
                networkList->addItem("No networks found");
        }, Qt::QueuedConnection);
    });
#else
    networkList->clear();
    networkList->addItem("Network scanning not available on desktop");
    networkList->addItem("WiFi is managed by your operating system");
#endif
}

void WifiDialog::onNetworkSelected(QListWidgetItem *item)
{
    if (!item)
        return;

    auto ssid = item->data(Qt::UserRole).toString();
    if (ssid.isEmpty())
        return;

#ifdef KOBO
    // Check if already connected to this network
    for (const auto &net : scannedNetworks)
    {
        if (net.ssid == ssid && net.connected)
        {
            statusLabel->setText("Already connected to " + ssid);
            return;
        }
    }

    // Check if network needs password
    bool secured = false;
    for (const auto &net : scannedNetworks)
    {
        if (net.ssid == ssid)
        {
            secured = net.secured;
            break;
        }
    }

    if (secured)
    {
        // Show password input
        bool ok = false;
        QString password = QInputDialog::getText(this, "WiFi Password",
            "Password for " + ssid + ":", QLineEdit::Password, "", &ok);
        if (!ok || password.isEmpty())
            return;

        statusLabel->setText("Connecting to " + ssid + "...");
        toggleBtn->setEnabled(false);

        QtConcurrent::run([this, ssid, password]() {
            // Use wpa_cli to add and connect to network
            QProcess proc;
            proc.start("wpa_cli", {"-i", "wlan0", "add_network"});
            proc.waitForFinished(5000);
            auto netId = proc.readAllStandardOutput().trimmed().split('\n').last().trimmed();

            QProcess::execute("wpa_cli", {"-i", "wlan0", "set_network", netId, "ssid", "\"" + ssid + "\""});
            QProcess::execute("wpa_cli", {"-i", "wlan0", "set_network", netId, "psk", "\"" + password + "\""});
            QProcess::execute("wpa_cli", {"-i", "wlan0", "enable_network", netId});
            QProcess::execute("wpa_cli", {"-i", "wlan0", "select_network", netId});
            QProcess::execute("wpa_cli", {"-i", "wlan0", "save_config"});

            // Wait for connection
            QThread::sleep(3);
            networkManager->checkInternetConnection();

            if (destroying) return;
            QMetaObject::invokeMethod(this, [this, ssid]() {
                if (destroying) return;
                updateStatus();
                toggleBtn->setEnabled(true);
                if (networkManager->connected)
                    statusLabel->setText("Connected to " + ssid);
                else
                    statusLabel->setText("Failed to connect to " + ssid);
            }, Qt::QueuedConnection);
        });
    }
    else
    {
        // Open network - just connect
        statusLabel->setText("Connecting to " + ssid + "...");
        toggleBtn->setEnabled(false);

        QtConcurrent::run([this, ssid]() {
            QProcess proc;
            proc.start("wpa_cli", {"-i", "wlan0", "add_network"});
            proc.waitForFinished(5000);
            auto netId = proc.readAllStandardOutput().trimmed().split('\n').last().trimmed();

            QProcess::execute("wpa_cli", {"-i", "wlan0", "set_network", netId, "ssid", "\"" + ssid + "\""});
            QProcess::execute("wpa_cli", {"-i", "wlan0", "set_network", netId, "key_mgmt", "NONE"});
            QProcess::execute("wpa_cli", {"-i", "wlan0", "enable_network", netId});
            QProcess::execute("wpa_cli", {"-i", "wlan0", "select_network", netId});

            QThread::sleep(3);
            networkManager->checkInternetConnection();

            if (destroying) return;
            QMetaObject::invokeMethod(this, [this, ssid]() {
                if (destroying) return;
                updateStatus();
                toggleBtn->setEnabled(true);
            }, Qt::QueuedConnection);
        });
    }
#endif
}

void WifiDialog::onCloseClicked()
{
    close();
}

void WifiDialog::updateStatus()
{
    if (networkManager->connected)
    {
        statusIcon->setText("*");
        statusLabel->setText("Connected");
        toggleBtn->setText("Disconnect");
    }
    else
    {
        statusIcon->setText("-");
        statusLabel->setText("Not connected");
        toggleBtn->setText("Connect");
    }
}

void WifiDialog::scanNetworks()
{
    scannedNetworks.clear();

#ifdef KOBO
    // First ensure WiFi hardware is on
    try { KoboPlatformFunctions::enableWiFiConnection(); }
    catch (...) {}

    // Give it a moment to initialize
    QThread::msleep(500);

    // Trigger scan
    QProcess::execute("wpa_cli", {"-i", "wlan0", "scan"});
    QThread::sleep(2);  // Wait for scan

    // Get results
    QProcess proc;
    proc.start("wpa_cli", {"-i", "wlan0", "scan_results"});
    proc.waitForFinished(5000);
    auto output = proc.readAllStandardOutput();

    // Get current network
    QProcess statusProc;
    statusProc.start("wpa_cli", {"-i", "wlan0", "status"});
    statusProc.waitForFinished(5000);
    auto statusOutput = statusProc.readAllStandardOutput();
    QString currentSSID;
    for (const auto &line : statusOutput.split('\n'))
    {
        if (line.startsWith("ssid="))
            currentSSID = QString(line.mid(5)).trimmed();
    }

    // Parse scan results: bssid / frequency / signal level / flags / ssid
    for (const auto &line : output.split('\n'))
    {
        auto parts = QString(line).split('\t');
        if (parts.size() < 5)
            continue;
        if (parts[0].startsWith("bssid"))
            continue;  // header line

        NetworkInfo net;
        net.ssid = parts[4].trimmed();
        if (net.ssid.isEmpty())
            continue;

        // Signal: wpa_cli gives dBm, convert to 0-100
        int dbm = parts[2].toInt();
        net.signal = qBound(0, 2 * (dbm + 100), 100);

        net.secured = parts[3].contains("WPA") || parts[3].contains("WEP");
        net.connected = (net.ssid == currentSSID && networkManager->connected);

        // Deduplicate (same SSID, keep strongest)
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

    // Sort: connected first, then by signal strength
    std::sort(scannedNetworks.begin(), scannedNetworks.end(),
              [](const NetworkInfo &a, const NetworkInfo &b) {
                  if (a.connected != b.connected) return a.connected;
                  return a.signal > b.signal;
              });
#endif
}
