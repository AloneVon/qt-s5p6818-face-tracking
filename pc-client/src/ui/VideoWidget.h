// VideoWidget.h — paints the latest decoded video frame, letterboxed and
// aspect-correct on a black background. Clicking the image emits a normalized
// offset from center so the window can translate it into a gimbal nudge
// (click-to-aim).
#pragma once

#include <QImage>
#include <QRect>
#include <QWidget>

namespace secpc {

class VideoWidget : public QWidget {
    Q_OBJECT
public:
    explicit VideoWidget(QWidget* parent = nullptr);

    QSize sizeHint() const override { return QSize(640, 480); }

public slots:
    void setFrame(const QImage& img);
    void clear();

signals:
    // Offset of the click from the image center, each axis normalized to
    // [-1, 1] (right/down positive).
    void aimRequested(double dxNorm, double dyNorm);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;

private:
    QRect targetRect() const;  // where the frame is drawn inside the widget

    QImage frame_;
};

}  // namespace secpc
