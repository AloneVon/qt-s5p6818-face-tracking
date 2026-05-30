// MainWindow.h — top-level window. A VideoWidget takes center stage with a
// side panel (ControlPanel + FileBrowser); a TcpClient owns the connection.
// This class is the only place UI intent meets the protocol: it translates
// widget signals into TcpClient calls and reflects connection/stream state in
// the status bar (connection, resolution, client-side FPS).
#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QMainWindow>
#include <QString>

class QLabel;

namespace secpc {

class TcpClient;
class VideoWidget;
class ControlPanel;
class FileBrowser;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void onConnected();
    void onDisconnected();
    void onError(const QString& message);
    void onVideoFrame(const QImage& image, quint32 seq,
                      quint32 width, quint32 height, quint64 tsMs);
    void onAim(double dxNorm, double dyNorm);
    void onFileDownloaded(const QString& name, const QByteArray& bytes);
    void onFileDeleted(const QString& name, bool ok);

private:
    void setConnectedUi(bool connected);
    void updateStatus();

    TcpClient*    tcp_;
    VideoWidget*  video_;
    ControlPanel* controls_;
    FileBrowser*  files_;

    QLabel* connLabel_;
    QLabel* resLabel_;
    QLabel* fpsLabel_;

    bool    connected_ = false;
    QString peer_;

    // Client-side FPS: tally frames and recompute once a second.
    QElapsedTimer fpsTimer_;
    int           frameCount_ = 0;
    double        fps_   = 0.0;
    quint32       lastW_ = 0;
    quint32       lastH_ = 0;
};

}  // namespace secpc
