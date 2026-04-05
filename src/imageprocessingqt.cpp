
#include "imageprocessingqt.h"

inline QPair<int, int> getTrimRectHelper(const uchar *linePtr, int imgWidth, int limitLeft, int limitRight,
                                         const uchar threshold)
{
    int left = 0;
    int right = 0;

    for (left = 0; left < limitLeft; left++)
        if (linePtr[left] <= threshold)
            break;

    for (right = imgWidth - 1; right >= limitRight; right--)
        if (linePtr[right] <= threshold)
            break;

    return {left, right};
}

QRect getTrimRect(const QByteArray &buffer, int imgWidth, int imgHeight, int stride, int trimLevel)
{
    // Trim levels:
    // 1 = Light: only pure white borders (threshold 250)
    // 2 = Normal: standard white margin detection (threshold 234)
    // 3 = Aggressive: includes light grey borders (threshold 210), extra margin crop
    // 4 = Maximum: deep crop including page numbers (threshold 190), 5% inset
    uchar threshold;
    int extraInset = 0;  // Additional pixels to crop from each edge
    switch (trimLevel)
    {
        case 1:  threshold = 250; break;
        case 2:  threshold = 234; break;
        case 3:  threshold = 210; extraInset = imgWidth / 50; break;   // +2% each side
        case 4:  threshold = 190; extraInset = imgWidth / 20; break;   // +5% each side
        default: threshold = 234; break;
    }

    int bottom = 0;
    int top = 0;
    int leftMin = imgWidth;
    int rightMin = 0;

    uchar *linePtr;

    for (top = 0; top < imgHeight; top++)
    {
        linePtr = (uchar *)buffer.data() + top * stride;
        auto [left, right] = getTrimRectHelper(linePtr, imgWidth, imgWidth, rightMin, threshold);

        bool allWhite = left == imgWidth;
        if (!allWhite)
        {
            leftMin = qMin(leftMin, left);
            rightMin = qMax(rightMin, right);
            break;
        }
    }

    for (bottom = imgHeight - 1; bottom >= top; bottom--)
    {
        linePtr = (uchar *)buffer.data() + bottom * stride;
        auto [left, right] = getTrimRectHelper(linePtr, imgWidth, imgWidth, rightMin, threshold);

        bool allWhite = left == imgWidth;
        if (!allWhite)
        {
            leftMin = qMin(leftMin, left);
            rightMin = qMax(rightMin, right);
            break;
        }
    }

    for (int middle = top; middle < bottom; middle++)
    {
        linePtr = (uchar *)buffer.data() + middle * stride;
        auto [left, right] = getTrimRectHelper(linePtr, imgWidth, imgWidth, rightMin, threshold);

        bool allWhite = left == imgWidth;
        if (!allWhite)
        {
            leftMin = qMin(leftMin, left);
            rightMin = qMax(rightMin, right);
        }
    }

    // Apply extra inset for aggressive/maximum modes
    if (extraInset > 0)
    {
        leftMin = qMin(leftMin + extraInset, rightMin);
        rightMin = qMax(rightMin - extraInset, leftMin);
        top = qMin(top + extraInset, bottom);
        bottom = qMax(bottom - extraInset, top);
    }

    // If image is entirely white/blank, return the full image rect (no trimming)
    if (leftMin >= imgWidth || rightMin <= 0 || top >= imgHeight || bottom < top)
        return QRect(0, 0, imgWidth, imgHeight);

    int width = rightMin - leftMin + 1;
    int height = bottom - top + 1;
    int ntw = qMax(0, width + 4 - width % 4);
    if (ntw + leftMin > imgWidth)
        ntw -= 4;

    return QRect(leftMin, top, qMax(4, ntw), qMax(4, height));
}

int calcRotationInfo(QSize imgSize, QSize screenSize, DoublePageMode doublePageMode)
{
    bool rot90 = (imgSize.width() <= imgSize.height()) != (screenSize.width() <= screenSize.height());

    if (rot90 && doublePageMode == DoublePage90CW)
        return 90;
    else if (rot90 && doublePageMode == DoublePage90CCW)
        return -90;

    return 0;
}

QSize fitToSize(QSize imgSize, QSize maxSize)
{
    if (imgSize.width() <= 0 || imgSize.height() <= 0)
        return maxSize;

    bool fitToWidth = (float)maxSize.width() / imgSize.width() < (float)maxSize.height() / imgSize.height();
    if (fitToWidth)
    {
        return QSize(maxSize.width(), ((float)imgSize.height() * maxSize.width()) / imgSize.width());
    }
    else
    {
        return QSize((float)(imgSize.width() * maxSize.height()) / imgSize.height(), maxSize.height());
    }
}

QSize calcRescaleSize(QSize imgSize, QSize screenSize, bool rot90, bool manhwaMode)
{
    if (imgSize.width() <= 0 || imgSize.height() <= 0 ||
        screenSize.width() <= 0 || screenSize.height() <= 0)
        return screenSize;

    QSize rescaleSize;

    if (rot90)
    {
        rescaleSize = fitToSize(imgSize, screenSize);
    }
    else
    {
        if (manhwaMode && imgSize.width() > 0 && screenSize.width() > 0 &&
            ((float)imgSize.height() / imgSize.width()) >
                1.6 * ((float)screenSize.height() / screenSize.width()))
            rescaleSize =
                QSize(screenSize.width(), (imgSize.height() * screenSize.width()) / imgSize.width());
        else
        {
            rescaleSize = fitToSize(imgSize, screenSize);
        }
    }
    rescaleSize.setWidth(rescaleSize.width() + 4 - rescaleSize.width() % 4);
    return rescaleSize;
}

QImage processImageQt(const QByteArray &array, const QString &filepath, QSize screenSize,
                      DoublePageMode doublePageMode, int trimLevel, bool manhwaMode, bool useSWDither)
{
    QImage img;
    if (!img.loadFromData(array))
        return QImage();

    auto rot90 = calcRotationInfo(img.size(), screenSize, doublePageMode);

    QImage ret;
    bool res = false;

    if (!useSWDither)
    {
        // Color mode: keep original color, just resize
        QImage workImg = img;
        if (rot90 != 0)
            workImg = workImg.transformed(QTransform().rotate(rot90));
        auto rescaleSize = calcRescaleSize(workImg.size(), screenSize, rot90 != 0, manhwaMode);
        ret = workImg.scaled(rescaleSize.width(), rescaleSize.height(),
                             Qt::KeepAspectRatio, Qt::SmoothTransformation);
        res = ret.save(filepath, nullptr, 85);
    }
    else
    {
        // Greyscale mode
        QImage greyImg = img.convertToFormat(QImage::Format_Grayscale8);

        if (!greyImg.isNull())
        {
            if (trimLevel > 0)
            {
                auto arrayT = QByteArray::fromRawData((const char *)greyImg.bits(), greyImg.sizeInBytes());
                auto trimRect = getTrimRect(arrayT, greyImg.width(), greyImg.height(), greyImg.bytesPerLine(), trimLevel);
                if (trimRect.isValid() && trimRect.width() > 10 && trimRect.height() > 10)
                    greyImg = greyImg.copy(trimRect);
            }

            if (rot90 != 0)
                greyImg = greyImg.transformed(QTransform().rotate(rot90));
            auto rescaleSize = calcRescaleSize(greyImg.size(), screenSize, rot90 != 0, manhwaMode);

            ret = greyImg.scaled(rescaleSize.width(), rescaleSize.height(), Qt::KeepAspectRatio,
                                 Qt::SmoothTransformation);
            res = ret.save(filepath, nullptr, 85);
        }
    }

    // if something went wrong with the greyscale img -> use original
    if (!res)
    {
        if (rot90 != 0)
            img = img.transformed(QTransform().rotate(rot90));
        auto rescaleSize = calcRescaleSize(img.size(), screenSize, rot90 != 0, manhwaMode);

        ret = img.scaled(rescaleSize.width(), rescaleSize.height(), Qt::KeepAspectRatio,
                         Qt::SmoothTransformation);
        res = ret.save(filepath, nullptr, 85);
    }

    if (res)
    {
        return ret;
    }

    return QImage();
}
