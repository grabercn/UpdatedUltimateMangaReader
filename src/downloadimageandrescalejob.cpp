#include "downloadimageandrescalejob.h"

#include <QTransform>

DownloadScaledImageJob::DownloadScaledImageJob(
    QNetworkAccessManager *networkManager, const QString &url, const QString &path, QSize screenSize,
    Settings *settings, const QList<std::tuple<const char *, const char *>> &customHeaders,
    const EncryptionDescriptor &encryption)
    : DownloadFileJob(networkManager, url, path, customHeaders),
      resultImage(nullptr),
      screenSize(screenSize),
      settings(settings),
      encryption(encryption)
{
}

void DownloadScaledImageJob::downloadFileReadyRead()
{
    // don't save to file because its gonna be rescaled anyway
    //    file.write(reply->readAll());
}

void DownloadScaledImageJob::downloadFileFinished()
{
    if (file.isOpen())
    {
        file.close();
        file.remove();
    }

    QUrl redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
    if (redirect.isValid() && reply->url() != redirect)
    {
        this->url = redirect.toString();
        this->restart();
        return;
    }

    if (reply->error() != QNetworkReply::NoError)
    {
        onError(QNetworkReply::NetworkError());
    }
    else
    {
        if (processImage(reply->readAll()))
        {
            isCompleted = true;
            emit completed();
        }
        else
        {
            errorString = "Failed to load or process image.";
            emit downloadError();
        }
    }
}

bool DownloadScaledImageJob::processImage(QByteArray &&array)
{
    //    QElapsedTimer t;
    //    t.start();

    if (encryption.type == XorEncryption)
    {
#ifdef KOBO
        decryptXorInplace_NEON(array, encryption.key);
#else
        decryptXorInplace(array, encryption.key);
#endif
    }

    //    qDebug() << "Image processing decrypt:" << t.elapsed();
    QImage pimg;

    bool useDither = !settings->colorMode && settings->ditheringMode >= SWDithering;

    if (settings->colorMode)
    {
        // Color mode: skip greyscale conversion, just resize
        QImage img;
        if (img.loadFromData(array))
        {
            auto rot90 = calcRotationInfo(img.size(), screenSize, settings->doublePageMode);
            if (rot90 != 0)
                img = img.transformed(QTransform().rotate(rot90));
            auto rescaleSize = calcRescaleSize(img.size(), screenSize, rot90 != 0, settings->manhwaMode);
            pimg = img.scaled(rescaleSize.width(), rescaleSize.height(),
                              Qt::KeepAspectRatio, Qt::SmoothTransformation);
            pimg.save(filepath, nullptr, 85);
        }
    }
    else if (isJpeg(array) || isPng(array))
    {
        pimg = processImageN(array, filepath, screenSize, settings->doublePageMode, settings->trimPages,
                             settings->manhwaMode, useDither);
    }

    if (!pimg.isNull())
    {
        resultImage.reset(new QImage(pimg));
    }
    else if (!settings->colorMode)
    {
        qDebug() << "Fast decoding failed, using fallback!";

        pimg = processImageQt(array, filepath, screenSize, settings->doublePageMode, settings->trimPages,
                              settings->manhwaMode, useDither);

        if (!pimg.isNull())
            resultImage.reset(new QImage(pimg));
    }

    //    qDebug() << "Image processing:" << t.elapsed();

    return !pimg.isNull();
}
