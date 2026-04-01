#include "wifidialog.h"

#include "ui_wifidialog.h"

WifiDialog::WifiDialog(QWidget *parent, NetworkManager *networkManager)
    : QDialog(parent), ui(new Ui::WifiDialog), networkManager(networkManager), lastConnection()
{
    ui->setupUi(this);
}

WifiDialog::~WifiDialog()
{
    destroying = true;
    if (lastConnection.isRunning())
        lastConnection.waitForFinished();
    delete ui;
}

void WifiDialog::connect()
{
    ui->labelInfo->setText("Connecting to WiFi...");
    ui->pushButtonRetry->hide();

    lastConnection = QtConcurrent::run([this]() {
        networkManager->connectWifi();
        if (destroying)
            return;
        QMetaObject::invokeMethod(this, [this]()
        {
            if (destroying)
                return;
            if (networkManager->connected)
                close();
            else
            {
                ui->pushButtonRetry->show();
                ui->labelInfo->setText("Couldn't connect to WiFi.");
            }
        }, Qt::QueuedConnection);
    });
}

void WifiDialog::on_pushButtonRetry_clicked()
{
    connect();
}

void WifiDialog::on_pushButtonClose_clicked()
{
    close();
}
