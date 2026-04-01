#ifndef SPINNERWIDGET_H
#define SPINNERWIDGET_H

#include <QPainter>
#include <QTimer>
#include <QWidget>
#include <QtMath>

class SpinnerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SpinnerWidget(QWidget *parent = nullptr)
        : QWidget(parent), angle(0), timer(this)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, false);
        setFixedSize(parent ? parent->size() : QSize(400, 400));
        connect(&timer, &QTimer::timeout, this, &SpinnerWidget::rotate);
    }

    void start()
    {
        angle = 0;
        if (parentWidget())
            setFixedSize(parentWidget()->size());
        raise();
        show();
        timer.start(50);
    }

    void stop()
    {
        timer.stop();
        hide();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // Semi-transparent background
        p.fillRect(rect(), QColor(255, 255, 255, 200));

        // Draw spinner in center
        int size = qMin(width(), height()) / 6;
        if (size < 30) size = 30;
        if (size > 80) size = 80;

        QPoint center = rect().center();
        int numDots = 8;
        int dotRadius = size / 6;

        for (int i = 0; i < numDots; i++)
        {
            qreal a = (360.0 / numDots) * i + angle;
            qreal rad = qDegreesToRadians(a);
            int x = center.x() + static_cast<int>(size * qCos(rad));
            int y = center.y() + static_cast<int>(size * qSin(rad));

            // Fade dots: the one at current angle is darkest
            int alpha = 40 + (215 * ((numDots - i) % numDots)) / numDots;
            p.setBrush(QColor(0, 0, 0, alpha));
            p.setPen(Qt::NoPen);
            p.drawEllipse(QPoint(x, y), dotRadius, dotRadius);
        }
    }

private slots:
    void rotate()
    {
        angle = (angle + 30) % 360;
        update();
    }

private:
    int angle;
    QTimer timer;
};

#endif  // SPINNERWIDGET_H
