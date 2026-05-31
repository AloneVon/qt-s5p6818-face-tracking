#include "ui/MainWindow.h"

#include <QFile>
#include <QFileDialog>
#include <QLabel>
#include <QMessageBox>
#include <QSplitter>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

#include "net/TcpClient.h"
#include "ui/ControlPanel.h"
#include "ui/FileBrowser.h"
#include "ui/VideoWidget.h"

namespace secpc {

namespace {
constexpr double kAimMaxDeg = 12.0;  // a click at the frame edge nudges this far
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("S5P6818 安防终端 — PC 客户端"));
    resize(1024, 640);

    tcp_      = new TcpClient(this);
    video_    = new VideoWidget;
    controls_ = new ControlPanel;
    files_    = new FileBrowser;

    // ---- Layout: video | [controls / files] -----------------------------
    auto* side = new QWidget;
    auto* sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(0, 0, 0, 0);
    sideLayout->addWidget(controls_);
    sideLayout->addWidget(files_, 1);

    auto* splitter = new QSplitter(this);
    splitter->addWidget(video_);
    splitter->addWidget(side);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({720, 304});
    setCentralWidget(splitter);

    // ---- Status bar ------------------------------------------------------
    connLabel_ = new QLabel(this);
    resLabel_  = new QLabel(this);
    fpsLabel_  = new QLabel(this);
    statusBar()->addWidget(connLabel_);
    statusBar()->addPermanentWidget(resLabel_);
    statusBar()->addPermanentWidget(fpsLabel_);

    // ---- UI intent -> client ---------------------------------------------
    connect(controls_, &ControlPanel::connectRequested, this,
            [this](const QString& host, quint16 port, const QString& token,
                   bool tls, const QString& certPath) {
                peer_ = QStringLiteral("%1:%2").arg(host).arg(port);
                connLabel_->setText(tr("Connecting to %1%2…")
                                        .arg(peer_, tls ? QStringLiteral(" (TLS)")
                                                        : QString()));
                tcp_->connectToTerminal(host, port, token, tls, certPath);
            });
    connect(controls_, &ControlPanel::disconnectRequested,
            tcp_, &TcpClient::disconnectFromTerminal);
    connect(controls_, &ControlPanel::panTiltStep, tcp_, &TcpClient::sendServoStep);
    connect(controls_, &ControlPanel::homeRequested, this,
            [this] { tcp_->sendServoAbs(90.0, 90.0); });
    connect(controls_, &ControlPanel::modeChanged, tcp_, &TcpClient::sendMode);

    connect(video_, &VideoWidget::aimRequested, this, &MainWindow::onAim);

    connect(files_, &FileBrowser::refreshRequested, tcp_, &TcpClient::requestFileList);
    connect(files_, &FileBrowser::downloadRequested, tcp_, &TcpClient::requestFile);
    connect(files_, &FileBrowser::deleteRequested, tcp_, &TcpClient::deleteFile);
    connect(files_, &FileBrowser::snapshotRequested, tcp_, &TcpClient::requestSnapshot);

    // ---- client -> UI ----------------------------------------------------
    connect(tcp_, &TcpClient::connected,     this, &MainWindow::onConnected);
    connect(tcp_, &TcpClient::disconnected,  this, &MainWindow::onDisconnected);
    connect(tcp_, &TcpClient::errorOccurred, this, &MainWindow::onError);
    connect(tcp_, &TcpClient::videoFrameReceived, this, &MainWindow::onVideoFrame);
    connect(tcp_, &TcpClient::fileListReceived, files_, &FileBrowser::setFiles);
    connect(tcp_, &TcpClient::fileDownloaded, this, &MainWindow::onFileDownloaded);
    connect(tcp_, &TcpClient::fileDeleted,    this, &MainWindow::onFileDeleted);

    setConnectedUi(false);
    updateStatus();
}

void MainWindow::onConnected() {
    connected_ = true;
    setConnectedUi(true);
    frameCount_ = 0;
    fps_ = 0.0;
    fpsTimer_.start();
    tcp_->requestFileList();  // populate the browser right away
    updateStatus();
}

void MainWindow::onDisconnected() {
    connected_ = false;
    setConnectedUi(false);
    updateStatus();
}

void MainWindow::onError(const QString& message) {
    statusBar()->showMessage(tr("Error: %1").arg(message), 5000);
}

void MainWindow::onVideoFrame(const QImage& image, quint32 /*seq*/,
                              quint32 width, quint32 height, quint64 /*tsMs*/) {
    video_->setFrame(image);
    lastW_ = width;
    lastH_ = height;

    ++frameCount_;
    const qint64 ms = fpsTimer_.elapsed();
    if (ms >= 1000) {
        fps_ = frameCount_ * 1000.0 / static_cast<double>(ms);
        frameCount_ = 0;
        fpsTimer_.restart();
    }
    updateStatus();
}

void MainWindow::onAim(double dxNorm, double dyNorm) {
    if (!connected_) return;
    // Click right of center pans right (pan+); below center tilts down (tilt-),
    // matching the d-pad convention (pan+ = right, tilt+ = up).
    tcp_->sendServoStep(dxNorm * kAimMaxDeg, -dyNorm * kAimMaxDeg);
}

void MainWindow::onFileDownloaded(const QString& name, const QByteArray& bytes) {
    if (bytes.isEmpty()) {
        QMessageBox::warning(this, tr("Download"),
                             tr("“%1” is empty or was not found on the terminal.").arg(name));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, tr("Save File"), name);
    if (path.isEmpty()) return;  // user cancelled

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size()) {
        QMessageBox::warning(this, tr("Save Failed"),
                             tr("Could not write to %1").arg(path));
        return;
    }
    statusBar()->showMessage(tr("Saved %1 (%2 bytes)").arg(name).arg(bytes.size()), 4000);
}

void MainWindow::onFileDeleted(const QString& name, bool ok) {
    if (ok) {
        statusBar()->showMessage(tr("Deleted %1").arg(name), 4000);
        tcp_->requestFileList();
    } else {
        QMessageBox::warning(this, tr("Delete"),
                             tr("The terminal could not delete “%1”.").arg(name));
    }
}

void MainWindow::setConnectedUi(bool connected) {
    controls_->setConnected(connected);
    files_->setConnected(connected);
    if (!connected) {
        video_->clear();
        lastW_ = lastH_ = 0;
        fps_ = 0.0;
    }
}

void MainWindow::updateStatus() {
    connLabel_->setText(connected_ ? tr("Connected to %1").arg(peer_)
                                   : tr("Not connected"));
    resLabel_->setText(lastW_ && lastH_ ? tr("%1×%2").arg(lastW_).arg(lastH_)
                                        : QStringLiteral("—"));
    fpsLabel_->setText(connected_ && fps_ > 0.0 ? tr("%1 fps").arg(fps_, 0, 'f', 1)
                                                : QStringLiteral("—"));
}

}  // namespace secpc
