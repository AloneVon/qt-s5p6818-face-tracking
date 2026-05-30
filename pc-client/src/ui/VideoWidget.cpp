#include "ui/VideoWidget.h"

#include <QMouseEvent>
#include <QPainter>

namespace secpc {

VideoWidget::VideoWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(320, 240);
    setAttribute(Qt::WA_OpaquePaintEvent);  // paintEvent covers every pixel
    setCursor(Qt::CrossCursor);
}

void VideoWidget::setFrame(const QImage& img) {
    frame_ = img;
    update();
}

void VideoWidget::clear() {
    frame_ = QImage();
    update();
}

QRect VideoWidget::targetRect() const {
    if (frame_.isNull()) return {};
    QSize s = frame_.size();
    s.scale(size(), Qt::KeepAspectRatio);
    QRect r(QPoint(0, 0), s);
    r.moveCenter(rect().center());
    return r;
}

void VideoWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), Qt::black);

    if (frame_.isNull()) {
        p.setPen(QColor(150, 150, 150));
        p.drawText(rect(), Qt::AlignCenter, tr("no signal"));
        return;
    }
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.drawImage(targetRect(), frame_);
}

void VideoWidget::mousePressEvent(QMouseEvent* e) {
    const QRect tr = targetRect();
    if (tr.isNull() || !tr.contains(e->pos())) return;

    const double halfW = tr.width()  / 2.0;
    const double halfH = tr.height() / 2.0;
    const double dx = (e->pos().x() - tr.center().x()) / halfW;
    const double dy = (e->pos().y() - tr.center().y()) / halfH;
    emit aimRequested(dx, dy);
}

}  // namespace secpc
