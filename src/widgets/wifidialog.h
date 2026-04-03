#ifndef WIFIDIALOG_H
#define WIFIDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>
#include <atomic>

#include "networkmanager.h"
#include "sizes.h"

class WifiDialog : public QDialog
{
    Q_OBJECT

public:
    explicit WifiDialog(QWidget *parent, NetworkManager *networkManager);
    ~WifiDialog();

    void connect();
    void openFullScreen();

private slots:
    void onToggleClicked();
    void onScanClicked();
    void onNetworkSelected(QListWidgetItem *item);
    void onCloseClicked();

private:
    NetworkManager *networkManager;
    QFuture<void> lastConnection;
    std::atomic<bool> destroying{false};

    // UI
    QLabel *statusLabel;
    QLabel *statusIcon;
    QPushButton *toggleBtn;
    QPushButton *scanBtn;
    QPushButton *closeBtn;
    QListWidget *networkList;
    QLabel *infoLabel;

    void updateStatus();
    void scanNetworks();

    // For Kobo wpa_cli integration
    struct NetworkInfo
    {
        QString ssid;
        int signal = 0;       // signal strength 0-100
        bool secured = false;
        bool connected = false;
    };
    QList<NetworkInfo> scannedNetworks;
};

#endif  // WIFIDIALOG_H
