#include "updater.h"

#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QSslSocket>

#include "staticsettings.h"

QString Updater::currentVersion()
{
#ifdef APP_VERSION
    return QStringLiteral(APP_VERSION);
#else
    return QStringLiteral("0.0.0");
#endif
}
const QString Updater::repoOwner = "grabercn";
const QString Updater::repoName = "UpdatedUltimateMangaReader";

Updater::Updater(NetworkManager *networkManager, QObject *parent)
    : QObject(parent), networkManager(networkManager), m_updateAvailable(false)
{
    loadLastCheckDate();
    loadSkippedVersion();
}

void Updater::loadLastCheckDate()
{
    QFile file(CONF.cacheDir + "last_update_check.dat");
    if (file.open(QIODevice::ReadOnly))
    {
        QDataStream in(&file);
        in >> lastCheckDate;
        file.close();
    }
}

bool Updater::shouldAutoCheck() const
{
    return !lastCheckDate.isValid() || lastCheckDate != QDate::currentDate();
}

void Updater::checkForUpdate()
{
    emit updateLog("Checking for updates...");

    if (!QSslSocket::supportsSsl())
    {
        emit error("SSL not available. OpenSSL DLLs may be missing.\n"
                   "SSL build: " + QSslSocket::sslLibraryBuildVersionString());
        emit checkCompleted(false);
        return;
    }

    // Fetch remote VERSION file (raw content, no API rate limits)
    auto versionUrl = QString("https://raw.githubusercontent.com/%1/%2/master/VERSION")
                          .arg(repoOwner, repoName);
    auto versionJob = networkManager->downloadAsString(versionUrl, 10000);

    if (!versionJob->await(10000))
    {
        emit error("Couldn't connect to GitHub: " + versionJob->errorString);
        emit checkCompleted(false);
        return;
    }

    auto remoteVersion = versionJob->bufferStr.trimmed();
    if (remoteVersion.isEmpty() || !remoteVersion.contains('.'))
    {
        emit error("Invalid version info from server.");
        emit checkCompleted(false);
        return;
    }

    // Also fetch latest commit info for the update notes
    auto apiUrl = QString("https://api.github.com/repos/%1/%2/commits/master")
                      .arg(repoOwner, repoName);
    auto job = networkManager->downloadAsString(apiUrl, 15000);
    job->await(15000);  // best-effort, don't fail if API is rate-limited

    // Save check date
    lastCheckDate = QDate::currentDate();
    QFile dateFile(CONF.cacheDir + "last_update_check.dat");
    if (dateFile.open(QIODevice::WriteOnly))
    {
        QDataStream out(&dateFile);
        out << lastCheckDate;
        dateFile.close();
    }

    // Parse commit info (best-effort)
    QRegularExpression shaRx(R"lit("sha"\s*:\s*"([a-f0-9]{40})")lit");
    QRegularExpression msgRx(R"lit("message"\s*:\s*"([^"]*)")lit");
    QRegularExpression dateRx(R"lit("date"\s*:\s*"([^"]*)")lit");

    auto shaMatch = shaRx.match(job->bufferStr);
    auto msgMatch = msgRx.match(job->bufferStr);
    auto dateMatch = dateRx.match(job->bufferStr);

    m_latestVersion = remoteVersion;
    m_latestFullSha = shaMatch.hasMatch() ? shaMatch.captured(1) : "";
    m_latestDate = dateMatch.hasMatch() ? dateMatch.captured(1).left(10) : "unknown";

    // Compare version numbers (semver: major.minor.patch)
    if (compareVersions(currentVersion(), remoteVersion) >= 0)
    {
        m_updateAvailable = false;
        emit updateLog("You're up to date! (v" + currentVersion() + ")");
        emit checkCompleted(false);
        return;
    }

    m_updateAvailable = true;
    m_latestNotes = msgMatch.hasMatch() ? msgMatch.captured(1) : "No description";
    m_latestNotes.replace("\\n", "\n");

    // Build download URL for the release binary
    // On Kobo: download the ARM binary from releases
    // On desktop: just notify user to rebuild
#ifdef KOBO
    // Download just the binary directly from the release
    m_downloadUrl = QString("https://github.com/%1/%2/releases/latest/download/UltimateMangaReader-Kobo.tar.gz")
                        .arg(repoOwner, repoName);
    // TODO: Once CI produces a standalone binary artifact, switch to direct binary URL
#else
    m_downloadUrl.clear();  // No auto-download on desktop
#endif

    emit updateLog("Update available! (" + m_latestVersion + " from " + m_latestDate + ")\n" + m_latestNotes);
    emit checkCompleted(true);
}

void Updater::downloadAndApply()
{
#ifdef KOBO
    if (m_downloadUrl.isEmpty())
    {
        emit error("No download URL available.");
        emit updateCompleted(false);
        return;
    }

    emit updateLog("Downloading update...");
    emit downloadProgress(0);

    auto appPath = QCoreApplication::applicationFilePath();
    auto tempPath = appPath + ".update";
    auto backupPath = appPath + ".backup";

    auto job = networkManager->downloadAsFile(m_downloadUrl, tempPath);

    // Wait for download with timeout
    if (!job->await(120000))  // 2 minute timeout
    {
        emit updateLog("Download failed: " + job->errorString);
        QFile::remove(tempPath);
        emit updateCompleted(false);
        return;
    }

    emit downloadProgress(50);
    emit updateLog("Download complete. Applying update...");

    // Verify the downloaded file is reasonable
    QFileInfo fi(tempPath);
    if (fi.size() < 100000)  // less than 100KB is suspicious
    {
        emit updateLog("Downloaded file too small - may be corrupt.");
        QFile::remove(tempPath);
        emit updateCompleted(false);
        return;
    }

    // If downloaded file is a tar.gz, extract the binary from it
    if (m_downloadUrl.endsWith(".tar.gz"))
    {
        auto extractDir = appPath + ".extract";
        QDir().mkpath(extractDir);
        QProcess tar;
        tar.setWorkingDirectory(extractDir);
        tar.start("tar", {"xzf", tempPath});
        tar.waitForFinished(30000);
        QFile::remove(tempPath);

        // Find the binary inside the extracted archive
        auto binaryPath = extractDir + "/.adds/UltimateMangaReader/UltimateMangaReader";
        if (!QFile::exists(binaryPath))
        {
            emit updateLog("Couldn't find binary in update archive.");
            QDir(extractDir).removeRecursively();
            emit updateCompleted(false);
            return;
        }
        // Move extracted binary to temp path
        QFile::rename(binaryPath, tempPath);
        QDir(extractDir).removeRecursively();
    }

    // Backup current binary
    QFile::remove(backupPath);
    QFile::rename(appPath, backupPath);

    // Move new binary into place
    if (QFile::rename(tempPath, appPath))
    {
        // Make executable
        QFile::setPermissions(appPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                           QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                                           QFileDevice::ExeGroup);

        // Save the full SHA so we know this version is installed
        QFile versionFile(CONF.cacheDir + "installed_version.dat");
        if (versionFile.open(QIODevice::WriteOnly))
        {
            QDataStream out(&versionFile);
            out << m_latestFullSha;
            versionFile.close();
        }

        // Clear any skipped version since we just updated
        m_skippedSha.clear();
        saveSkippedVersion();

        emit downloadProgress(100);
        emit updateLog("Update applied! The app will restart.");
        emit updateCompleted(true);

        // Restart the application
        QProcess::startDetached(appPath, QCoreApplication::arguments());
        QCoreApplication::quit();
    }
    else
    {
        // Restore backup
        emit updateLog("Failed to apply update. Restoring backup...");
        QFile::rename(backupPath, appPath);
        QFile::remove(tempPath);
        emit updateCompleted(false);
    }
#else
    // Desktop: just notify
    emit updateLog("On desktop, please rebuild from source:\n"
                   "  git pull && build-win.bat\n\n"
                   "Or download the latest from:\n"
                   "github.com/" + repoOwner + "/" + repoName);
    emit updateCompleted(false);
#endif
}

void Updater::skipVersion(const QString &sha)
{
    m_skippedSha = sha;
    saveSkippedVersion();
}

bool Updater::isVersionSkipped(const QString &sha) const
{
    return !m_skippedSha.isEmpty() && m_skippedSha == sha;
}

void Updater::loadSkippedVersion()
{
    QFile file(CONF.cacheDir + "skipped_version.dat");
    if (file.open(QIODevice::ReadOnly))
    {
        QDataStream in(&file);
        in >> m_skippedSha;
        file.close();
    }
}

void Updater::saveSkippedVersion()
{
    QDir().mkpath(CONF.cacheDir);
    QFile file(CONF.cacheDir + "skipped_version.dat");
    if (file.open(QIODevice::WriteOnly))
    {
        QDataStream out(&file);
        out << m_skippedSha;
        file.close();
    }
}

int Updater::compareVersions(const QString &a, const QString &b)
{
    auto partsA = a.split('.');
    auto partsB = b.split('.');
    int len = qMax(partsA.size(), partsB.size());
    for (int i = 0; i < len; i++)
    {
        int va = (i < partsA.size()) ? partsA[i].toInt() : 0;
        int vb = (i < partsB.size()) ? partsB[i].toInt() : 0;
        if (va != vb)
            return va - vb;
    }
    return 0;
}
