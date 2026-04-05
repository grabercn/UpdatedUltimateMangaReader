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

    // Only build UI once - check if we already have widgets
    if (statusLabel)
    {
        updateStatus();
        exec();
        return;
    }

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 6, 8, 6);
    mainLayout->setSpacing(4);

    // Header
    auto *headerRow = new QHBoxLayout();
    // Header row
    closeBtn = new QPushButton("< Back", this);
    closeBtn->setFixedHeight(SIZES.buttonSize);
    QObject::connect(closeBtn, &QPushButton::clicked, this, &WifiDialog::onCloseClicked);
    headerRow->addWidget(closeBtn);

    auto *titleLbl = new QLabel("<b>WiFi</b>", this);
    titleLbl->setAlignment(Qt::AlignCenter);
    headerRow->addWidget(titleLbl, 1);

    toggleBtn = new QPushButton("Connect", this);
    toggleBtn->setFixedHeight(SIZES.buttonSize);
    QObject::connect(toggleBtn, &QPushButton::clicked, this, &WifiDialog::onToggleClicked);
    headerRow->addWidget(toggleBtn);
    mainLayout->addLayout(headerRow);

    // Status
    statusLabel = new QLabel("Checking...", this);
    statusLabel->setAlignment(Qt::AlignCenter);
    statusLabel->setStyleSheet("padding: 4px; color: #555;");
    mainLayout->addWidget(statusLabel);

    // Scan button
    scanBtn = new QPushButton("Scan for Networks", this);
    scanBtn->setFixedHeight(SIZES.buttonSize);
    QObject::connect(scanBtn, &QPushButton::clicked, this, &WifiDialog::onScanClicked);
    mainLayout->addWidget(scanBtn);

    // Network list
    networkList = new QListWidget(this);
    networkList->setStyleSheet(
        "QListWidget { border: 1px solid #ddd; }"
        "QListWidget::item { padding: 6px 4px; border-bottom: 1px solid #eee; min-height: "
        + QString::number(SIZES.buttonSize) + "px; }");
    networkList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    networkList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    networkList->setFocusPolicy(Qt::NoFocus);
    networkList->setSelectionMode(QAbstractItemView::NoSelection);
    activateScroller(networkList);
    QObject::connect(networkList, &QListWidget::itemClicked, this, &WifiDialog::onNetworkSelected);
    mainLayout->addWidget(networkList, 1);

    // Remove unused members
    statusIcon = nullptr;
    infoLabel = nullptr;

    updateStatus();
    exec();
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
    if (destroying) return;
    if (networkManager->connected)
    {
        // Disconnect
        if (statusLabel) statusLabel->setText("Disconnecting...");
        if (toggleBtn) toggleBtn->setEnabled(false);
        lastConnection = QtConcurrent::run([this]() {
            networkManager->disconnectWifi();
            if (destroying) return;
            QMetaObject::invokeMethod(this, [this]() {
                if (destroying) return;
                updateStatus();
                if (toggleBtn) toggleBtn->setEnabled(true);
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
    if (destroying || !scanBtn || !networkList) return;
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
    QMutexLocker lock(&wifiMutex);
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
        // Show password dialog - full screen for e-ink touch
        QDialog pwDialog(this);
        pwDialog.setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
        pwDialog.resize(this->size());
        pwDialog.move(this->pos());

        // Install MainWidget's event filter on the password dialog so
        // CLineEdit's custom keyboard events reach the virtual keyboard
        pwDialog.installEventFilter(this->parentWidget());

        auto *pwLayout = new QVBoxLayout(&pwDialog);
        pwLayout->setContentsMargins(10, 8, 10, 8);
        pwLayout->setSpacing(6);

        auto *pwTitle = new QLabel("<b>Connect to " + ssid + "</b>", &pwDialog);
        pwTitle->setAlignment(Qt::AlignCenter);
        pwLayout->addWidget(pwTitle);

        // Put password field near top (above keyboard when it shows)
        pwLayout->addSpacing(20);

        auto *pwLabel = new QLabel("Enter WiFi password:", &pwDialog);
        pwLayout->addWidget(pwLabel);

        auto *pwEdit = new CLineEdit(&pwDialog);
        pwEdit->setEchoMode(QLineEdit::Password);
        pwEdit->setPlaceholderText("Tap here to type password...");
        pwEdit->setFixedHeight(SIZES.buttonSize);
        pwLayout->addWidget(pwEdit);

        auto *showPwCheck = new QCheckBox("Show password", &pwDialog);
        QObject::connect(showPwCheck, &QCheckBox::toggled, pwEdit, [pwEdit](bool show) {
            pwEdit->setEchoMode(show ? QLineEdit::Normal : QLineEdit::Password);
        });
        pwLayout->addWidget(showPwCheck);

        pwLayout->addSpacing(10);

        auto *pwBtnRow = new QHBoxLayout();
        auto *pwCancel = new QPushButton("Cancel", &pwDialog);
        pwCancel->setFixedHeight(SIZES.buttonSize);
        QObject::connect(pwCancel, &QPushButton::clicked, &pwDialog, &QDialog::reject);

        auto *pwConnect = new QPushButton("Connect", &pwDialog);
        pwConnect->setFixedHeight(SIZES.buttonSize);
        pwConnect->setStyleSheet("font-weight: bold;");
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
    if (toggleBtn) toggleBtn->setEnabled(false);

    lastConnection = QtConcurrent::run([this, ssid, password]() {
        // Add network via wpa_cli
        QProcess proc;
        proc.start("wpa_cli", {"-i", wifiInterface, "add_network"});
        proc.waitForFinished(5000);
        auto netId = proc.readAllStandardOutput().trimmed().split('\n').last().trimmed();
        qDebug() << "WiFi: adding network" << ssid << "as id" << netId;

        // Set SSID
        QProcess::execute("wpa_cli", {"-i", wifiInterface, "set_network", netId,
                          "ssid", QString("\"%1\"").arg(ssid)});

        if (password.isEmpty())
        {
            // Open network
            QProcess::execute("wpa_cli", {"-i", wifiInterface, "set_network", netId, "key_mgmt", "NONE"});
        }
        else
        {
            // WPA/WPA2 with password
            QProcess::execute("wpa_cli", {"-i", wifiInterface, "set_network", netId,
                              "psk", QString("\"%1\"").arg(password)});
        }

        QProcess::execute("wpa_cli", {"-i", wifiInterface, "enable_network", netId});
        QProcess::execute("wpa_cli", {"-i", wifiInterface, "select_network", netId});
        QProcess::execute("wpa_cli", {"-i", wifiInterface, "save_config"});

        // Wait for DHCP and connection
        QProcess::execute("sh", {"-c", "udhcpc -i " + wifiInterface + " -t 5 -T 3 -n 2>/dev/null &"});
        QThread::sleep(5);
        networkManager->checkInternetConnection();

        if (destroying) return;
        QMetaObject::invokeMethod(this, [this, ssid]() {
            if (destroying) return;
            updateStatus();
            if (toggleBtn) toggleBtn->setEnabled(true);
            if (statusLabel)
            {
                if (networkManager->connected)
                    statusLabel->setText("Connected to " + ssid);
                else
                    statusLabel->setText("Failed to connect to " + ssid);
            }
        }, Qt::QueuedConnection);
    });
#else
    Q_UNUSED(ssid); Q_UNUSED(password);
#endif
}

void WifiDialog::updateStatus()
{
    if (networkManager->connected)
    {
        if (statusIcon) statusIcon->setText("*");
        if (statusLabel) statusLabel->setText("Connected");
        if (toggleBtn) toggleBtn->setText("Disconnect");
    }
    else
    {
        if (statusIcon) statusIcon->setText("-");
        if (statusLabel) statusLabel->setText("Not connected");
        if (toggleBtn) toggleBtn->setText("Connect");
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
    qDebug() << "WiFi scan: using interface" << wifiInterface;

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
        qDebug() << "WiFi scan trigger:" << scanProc.readAllStandardOutput().trimmed();
    }
    QThread::sleep(3);  // Wait for scan to complete

    // Get results
    QProcess proc;
    proc.start("wpa_cli", {"-i", wifiInterface, "scan_results"});
    proc.waitForFinished(5000);
    auto output = proc.readAllStandardOutput();
    qDebug() << "WiFi scan results:" << output.left(500);

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

    // Fallback: if wpa_cli returned nothing, try iwlist
    if (output.trimmed().split('\n').size() <= 1)
    {
        qDebug() << "wpa_cli scan empty, trying iwlist...";
        QProcess iwProc;
        iwProc.start("sh", {"-c", "iwlist " + wifiInterface + " scan 2>/dev/null"});
        iwProc.waitForFinished(10000);
        auto iwOutput = iwProc.readAllStandardOutput();
        qDebug() << "iwlist output:" << iwOutput.left(500);

        // Parse iwlist output
        QString currentCell;
        int currentSignal = 0;
        bool currentSecured = false;

        for (const auto &rawLine : iwOutput.split('\n'))
        {
            auto line = QString(rawLine).trimmed();
            if (line.contains("Cell "))
            {
                if (!currentCell.isEmpty())
                {
                    NetworkInfo net;
                    net.ssid = currentCell;
                    net.signal = currentSignal;
                    net.secured = currentSecured;
                    net.connected = (currentCell == currentSSID && networkManager->connected);
                    scannedNetworks.append(net);
                }
                currentCell.clear();
                currentSignal = 0;
                currentSecured = false;
            }
            if (line.startsWith("ESSID:"))
                currentCell = line.mid(7).chopped(1);  // Remove quotes
            if (line.contains("Signal level="))
            {
                auto idx = line.indexOf("Signal level=") + 13;
                auto val = line.mid(idx).split(' ').first().split('/').first();
                int dbm = val.toInt();
                if (dbm < 0)
                    currentSignal = qBound(0, 2 * (dbm + 100), 100);
                else
                    currentSignal = qBound(0, dbm, 100);
            }
            if (line.contains("WPA") || line.contains("WEP"))
                currentSecured = true;
        }
        if (!currentCell.isEmpty())
        {
            NetworkInfo net;
            net.ssid = currentCell;
            net.signal = currentSignal;
            net.secured = currentSecured;
            net.connected = (currentCell == currentSSID && networkManager->connected);
            scannedNetworks.append(net);
        }
    }

    // Parse wpa_cli scan results: bssid / frequency / signal level / flags / ssid
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
