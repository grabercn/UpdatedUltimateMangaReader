#ifndef UPDATER_H
#define UPDATER_H

#include <QObject>

#include "networkmanager.h"

class Updater : public QObject
{
    Q_OBJECT

public:
    explicit Updater(NetworkManager *networkManager, QObject *parent = nullptr);

    static QString currentVersion();
    static const QString repoOwner;
    static const QString repoName;

    void checkForUpdate();
    void downloadAndApply();
    void checkPreviousVersion();
    void revertToPrevious();

    QString latestVersion() const { return m_latestVersion; }
    QString latestFullSha() const { return m_latestFullSha; }
    QString latestNotes() const { return m_latestNotes; }
    QString latestDate() const { return m_latestDate; }
    QString latestDownloadUrl() const { return m_downloadUrl; }
    bool updateAvailable() const { return m_updateAvailable; }
    bool previousAvailable() const { return m_previousAvailable; }
    QString previousVersion() const { return m_previousVersion; }
    QString previousNotes() const { return m_previousNotes; }

    void loadLastCheckDate();
    bool shouldAutoCheck() const;

    // Skip a specific version - won't prompt again for this SHA
    void skipVersion(const QString &sha);
    bool isVersionSkipped(const QString &sha) const;

signals:
    void checkCompleted(bool updateAvailable);
    void previousCheckCompleted(bool available);
    void downloadProgress(int percent);
    void updateLog(const QString &message);
    void updateCompleted(bool success);
    void error(const QString &msg);

private:
    NetworkManager *networkManager;
    QString m_latestVersion;
    QString m_latestFullSha;
    QString m_latestNotes;
    QString m_latestDate;
    QString m_downloadUrl;
    bool m_updateAvailable;

    QString m_previousVersion;
    QString m_previousNotes;
    bool m_previousAvailable = false;
    QDate lastCheckDate;
    QString m_skippedSha;

    void loadSkippedVersion();
    void saveSkippedVersion();
    static int compareVersions(const QString &a, const QString &b);
};

#endif  // UPDATER_H
