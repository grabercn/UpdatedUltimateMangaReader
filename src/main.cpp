#include <QtCore>
#include <atomic>
#include <thread>

#include "mainwidget.h"
#include "stacktrace.h"

#ifdef DESKTOP
static QFile *logFile = nullptr;

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    Q_UNUSED(context);
    static const char *typeStr[] = {"DEBUG", "WARNING", "CRITICAL", "FATAL", "INFO"};

    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
    QString line = QString("[%1] %2: %3\n").arg(timestamp, typeStr[type], msg);

    // Write to stderr
    fprintf(stderr, "%s", line.toUtf8().constData());

    // Write to log file
    if (logFile && logFile->isOpen())
    {
        logFile->write(line.toUtf8());
        logFile->flush();
    }

    if (type == QtFatalMsg)
        abort();
}
#endif

int main(int argc, char *argv[])
{
    registerBacktraceHandlers();

#ifdef DESKTOP
    // Set up file logging on desktop (next to the executable)
    QString logPath = QCoreApplication::applicationDirPath() + "/umr.log";
    // On Windows the app dir isn't set before QApplication, use argv[0]
    if (argc > 0)
    {
        QFileInfo fi(QString::fromLocal8Bit(argv[0]));
        logPath = fi.absolutePath() + "/umr.log";
    }
    logFile = new QFile(logPath);
    logFile->open(QIODevice::WriteOnly | QIODevice::Truncate);
    qInstallMessageHandler(messageHandler);
    qDebug() << "=== UltimateMangaReader started ===" << QDateTime::currentDateTime().toString();
    qDebug() << "Log file:" << logPath;
#endif

    QGuiApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QGuiApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

    QApplication app(argc, argv);

    int ret = 1;

#ifdef KOBO
    // Watchdog: restart app if main thread freezes for 60 seconds
    std::atomic<int> watchdogCounter{0};
    QTimer watchdogFeeder;
    watchdogFeeder.setInterval(5000);
    QObject::connect(&watchdogFeeder, &QTimer::timeout, [&watchdogCounter]() {
        watchdogCounter = 0;  // Reset - main thread is alive
    });
    watchdogFeeder.start();

    std::thread watchdogThread([&watchdogCounter, &argv]() {
        while (true)
        {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            watchdogCounter++;
            if (watchdogCounter > 6)  // 60 seconds without reset
            {
                qDebug() << "WATCHDOG: Main thread frozen for 60s, restarting...";
                QProcess::startDetached(QString::fromLocal8Bit(argv[0]), {});
                _exit(1);
            }
        }
    });
    watchdogThread.detach();
#endif

    // Kill Nickel and all its companion daemons to save battery
    // This is the same approach KOReader and Plato use — cleanly SIGTERM everything,
    // then restart Nickel from scratch on exit
    QProcess::execute("sh", {"-c",
        "killall -q -TERM nickel hindenburg sickel fickel strickel fontickel "
        "adobehost foxitpdf iink dhcpcd-dbus bluealsa bluetoothd fmon nanoclock.lua 2>/dev/null;"
        "rm -f /tmp/nickel-hardware-status"  // Remove IPC FIFO to prevent udev handler hangs
    });
    qDebug() << "Nickel and companion daemons stopped";

    try
    {
        MainWidget mainwidget;

        QApplication::setStyle("windows");
        QFile stylesheetFile(":/eink.qss");
        stylesheetFile.open(QFile::ReadOnly);
        mainwidget.setStyleSheet(stylesheetFile.readAll());
        stylesheetFile.close();

        mainwidget.show();

        ret = app.exec();
    }
    catch (const std::exception &e)
    {
        qCritical() << "Fatal exception:" << e.what();
    }
    catch (...)
    {
        qCritical() << "Unknown fatal exception";
    }

    // Restart Nickel from scratch so Kobo returns to normal e-reader mode
    QProcess::execute("sh", {"-c",
        "/mnt/onboard/.adds/UltimateMangaReader/fbdepth -d 32 2>/dev/null;"
        "LIBC_FATAL_STDERR_=1 /usr/local/Kobo/nickel -platform kobo -skipFontLoad &"
    });
    qDebug() << "Nickel restarted";

#ifdef DESKTOP
    if (logFile)
    {
        logFile->close();
        delete logFile;
    }
#endif

    return ret;
}
