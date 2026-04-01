#include <QtCore>

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

#ifdef DESKTOP
    if (logFile)
    {
        logFile->close();
        delete logFile;
    }
#endif

    return ret;
}
