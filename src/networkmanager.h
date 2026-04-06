#ifndef DOWNLOADMANAGER_H
#define DOWNLOADMANAGER_H

#include <QNetworkReply>

#include "downloadbufferjob.h"
#include "downloadfilejob.h"
#include "downloadimageandrescalejob.h"
#include "downloadstringjob.h"
#include "settings.h"

class NetworkManager : public QObject
{
    Q_OBJECT

public:
    explicit NetworkManager(QObject *parent = nullptr);

    QNetworkAccessManager *networkAccessManager();

    QSharedPointer<DownloadStringJob> downloadAsString(const QString &url, int timeout = 6000,
                                                       const QByteArray &postData = QByteArray(),
                                                       const QList<std::tuple<const char *, const char *>> &headers = {},
                                                       int maxRetries = 2);
    QSharedPointer<DownloadBufferJob> downloadToBuffer(const QString &url, int timeout = 6000,
                                                       const QByteArray &postData = QByteArray(),
                                                       int maxRetries = 2);
    QSharedPointer<DownloadFileJob> downloadAsFile(const QString &url, const QString &localPath,
                                                   int maxRetries = 2);
    QSharedPointer<DownloadFileJob> downloadAsScaledImage(const QString &url, const QString &localPath,
                                                          int maxRetries = 2);

    void setDownloadSettings(const QSize &size, Settings *settings);

    void addCookie(const QString &domain, const char *key, const char *value);
    void addSetCustomRequestHeader(const QString &domain, const char *key, const char *value);

    bool checkInternetConnection();
    bool connectWifi();
    bool disconnectWifi();

    bool isWifiHardwareEnabled();
    void setWifiHardwareEnabled(bool enabled);

    static void loadCertificates(const QString &certsPath);
    bool urlExists(const QString &url);

    bool connected;

signals:
    void connectionStatusChanged(bool connected);
    void activity();
    void downloadedImage(const QString &path, QSharedPointer<QImage> img);
    void networkError(const QString &message);

private:
    QNetworkAccessManager *networkManager;

    QSize imageRescaleSize;
    Settings *settings;

    QList<std::tuple<QString, const char *, const char *>> customHeaders;

    QMap<QString, QWeakPointer<DownloadFileJob>> fileDownloads;
    QString fixUrl(const QString &url);
};

#endif  // DOWNLOADMANAGER_H
