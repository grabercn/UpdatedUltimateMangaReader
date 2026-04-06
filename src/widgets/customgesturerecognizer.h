#ifndef CUSTOMGESTURERECOGNIZER_H
#define CUSTOMGESTURERECOGNIZER_H

#include <QElapsedTimer>
#include <QGesture>
#include <QGestureRecognizer>
#include <QPointF>

class SwipeGestureRecognizer : public QGestureRecognizer
{
    enum State
    {
        NoGesture,
        Started,
        ThreePointsReached
    };

    static const int MoveThreshold = 50;
    static const int directionChangeThreshold = MoveThreshold / 2;

    qreal velocity() const { return velocityValue; }
    void setVelocity(qreal value) { velocityValue = value; }

    QSwipeGesture::SwipeDirection horizontalDirection;
    QSwipeGesture::SwipeDirection verticalDirection;
    qreal swipeAngle;

    QPoint startPosition;
    State state;
    qreal velocityValue;
    QElapsedTimer time;

public:
    SwipeGestureRecognizer();
    QGesture *create(QObject *);
    Result recognize(QGesture *state, QObject *watched, QEvent *event);
    void reset(QGesture *state);
};

class TapGestureRecognizer : public QGestureRecognizer
{
    QPointF position;
    bool pressed;
    QElapsedTimer timer;
    static const int MAX_TAP_TIME = 400; // ms
    static const int TAP_RADIUS = 30;    // pixels

public:
    TapGestureRecognizer();

    QGesture *create(QObject *) override;
    QGestureRecognizer::Result recognize(QGesture *state, QObject *watched, QEvent *event) override;
    void reset(QGesture *state) override;
};

class LongPressGestureRecognizer : public QGestureRecognizer
{
    QPointF position;
    bool pressed;
    bool triggered;
    QElapsedTimer timer;
    static const int MIN_HOLD_TIME = 800; // ms
    static const int MOVE_THRESHOLD = 20; // pixels

public:
    LongPressGestureRecognizer();

    QGesture *create(QObject *) override;
    QGestureRecognizer::Result recognize(QGesture *state, QObject *watched, QEvent *event) override;
    void reset(QGesture *state) override;
};

#endif  // CUSTOMGESTURERECOGNIZER_H
