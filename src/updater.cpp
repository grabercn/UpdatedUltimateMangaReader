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

    // Fetch release notes from the release body (has full changelog)
    auto releaseUrl = QString("https://api.github.com/repos/%1/%2/releases/tags/latest")
                          .arg(repoOwner, repoName);
    auto releaseJob = networkManager->downloadAsString(releaseUrl, 10000);
    if (releaseJob->await(10000))
    {
        // Extract "body" field from release JSON
        QRegularExpression bodyRx(R"lit("body"\s*:\s*"((?:[^"\\]|\\.)*)")lit");
        auto bodyMatch = bodyRx.match(releaseJob->bufferStr);
        if (bodyMatch.hasMatch())
        {
            m_latestNotes = bodyMatch.captured(1);
            m_latestNotes.replace("\\n", "\n").replace("\\r", "").replace("\\\"", "\"");
            // Strip the install instructions - only keep "What's New" section
            int dashIdx = m_latestNotes.indexOf("\n---");
            if (dashIdx > 0)
                m_latestNotes = m_latestNotes.left(dashIdx).trimmed();
            // Clean up markdown
            m_latestNotes.remove(QRegularExpression(R"(^## )", QRegularExpression::MultilineOption));
        }
        else
        {
            m_latestNotes = msgMatch.hasMatch() ? msgMatch.captured(1) : "No description";
            m_latestNotes.replace("\\n", "\n");
        }
    }
    else
    {
        m_latestNotes = msgMatch.hasMatch() ? msgMatch.captured(1) : "No description";
        m_latestNotes.replace("\\n", "\n");
    }

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

void Updater::checkPreviousVersion()
{
    m_previousAvailable = false;

    // Check if previous-stable release exists by fetching its release info
    auto url = QString("https://api.github.com/repos/%1/%2/releases/tags/previous-stable")
                   .arg(repoOwner, repoName);
    auto job = networkManager->downloadAsString(url, 10000);

    if (!job->await(10000) || job->bufferStr.contains("\"Not Found\""))
    {
        emit previousCheckCompleted(false);
        return;
    }

    // Parse release name for version info
    QRegularExpression nameRx(R"lit("name"\s*:\s*"([^"]*)")lit");
    QRegularExpression bodyRx(R"lit("body"\s*:\s*"([^"]*)")lit");

    auto nameMatch = nameRx.match(job->bufferStr);
    auto bodyMatch = bodyRx.match(job->bufferStr);

    if (nameMatch.hasMatch())
    {
        m_previousVersion = nameMatch.captured(1);
        // Extract version from "Previous: vX.Y.Z (date)"
        QRegularExpression verRx(R"(v(\d+\.\d+\.\d+))");
        auto vm = verRx.match(m_previousVersion);
        if (vm.hasMatch())
            m_previousVersion = vm.captured(1);
    }
    else
    {
        m_previousVersion = "previous";
    }

    m_previousNotes = bodyMatch.hasMatch() ? bodyMatch.captured(1) : "";
    m_previousNotes.replace("\\n", "\n").replace("\\r", "");

    m_previousAvailable = true;
    emit previousCheckCompleted(true);
}

void Updater::revertToPrevious()
{
#ifdef KOBO
    auto url = QString("https://github.com/%1/%2/releases/download/previous-stable/UltimateMangaReader-Kobo.tar.gz")
                   .arg(repoOwner, repoName);

    // Reuse downloadAndApply logic with the previous-stable URL
    m_downloadUrl = url;
    // TODO: Once CI produces a standalone binary artifact, switch to direct binary URL
    downloadAndApply();
#else
    emit updateLog("On desktop, download the previous release manually from:\n"
                   "github.com/" + repoOwner + "/" + repoName + "/releases/tag/previous-stable");
    emit updateCompleted(false);
#endif
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
    emit updateLog("Applying update... Do NOT close the app or remove power.");

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

        // Use system tar with full path
        QProcess tar;
        tar.start("sh", {"-c", "cd " + extractDir + " && tar xzf " + tempPath + " 2>&1"});
        tar.waitForFinished(60000);
        auto tarOutput = tar.readAllStandardOutput() + tar.readAllStandardError();
        qDebug() << "tar extract:" << tar.exitCode() << tarOutput.left(200);

        QFile::remove(tempPath);

        // Find the binary inside the extracted archive
        auto binaryPath = extractDir + "/.adds/UltimateMangaReader/UltimateMangaReader";
        if (!QFile::exists(binaryPath))
        {
            qDebug() << "Binary not found at:" << binaryPath;
            // Try listing what was extracted
            QProcess ls;
            ls.start("sh", {"-c", "find " + extractDir + " -type f 2>&1"});
            ls.waitForFinished(5000);
            qDebug() << "Extracted files:" << ls.readAllStandardOutput().left(500);

            emit updateLog("Couldn't find binary in update archive.");
            QDir(extractDir).removeRecursively();
            emit updateCompleted(false);
            return;
        }

        // Copy (not rename - may be different filesystem)
        QFile::remove(tempPath);
        if (!QFile::copy(binaryPath, tempPath))
        {
            qDebug() << "Failed to copy binary from" << binaryPath << "to" << tempPath;
            emit updateLog("Failed to copy update binary.");
            QDir(extractDir).removeRecursively();
            emit updateCompleted(false);
            return;
        }
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
        emit updateLog("Update applied! Restarting in 3 seconds...\nDo not touch anything.");
        emit updateCompleted(true);

        // Write update-complete marker for post-reboot success screen
        QFile markerFile(CONF.cacheDir + "update_complete.txt");
        if (markerFile.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            QTextStream out(&markerFile);
            out << m_latestVersion << "\n";
            out << m_latestNotes;
            markerFile.close();
        }

        // Restore framebuffer before restart so Nickel/next launch gets clean display
        QProcess::execute("sh", {"-c",
            "/mnt/onboard/.adds/UltimateMangaReader/fbdepth -d 32 2>/dev/null"});

        // Give the UI a moment to show the message, then restart
        QThread::sleep(3);
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
