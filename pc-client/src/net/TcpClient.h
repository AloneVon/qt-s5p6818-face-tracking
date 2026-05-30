// TcpClient.h — talks the SEC1 wire protocol to the terminal over QTcpSocket.
// Single-threaded: Qt's event loop drives async I/O, and JPEG frames are decoded
// with QImage (no OpenCV). Reuses terminal/src/net/Protocol.h for framing, so
// the binary layout has one source of truth shared with the terminal.
#pragma once

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QString>
#include <QVector>

class QTcpSocket;

namespace secpc {

struct RemoteFile {
    QString name;
    quint64 size  = 0;
    quint64 mtime = 0;  // seconds since epoch
};

class TcpClient : public QObject {
    Q_OBJECT
public:
    explicit TcpClient(QObject* parent = nullptr);
    ~TcpClient() override;

    bool isConnected() const;

public slots:
    void connectToTerminal(const QString& host, quint16 port);
    void disconnectFromTerminal();

    // Control channel (client -> terminal).
    void sendServoStep(double dPanDeg, double dTiltDeg);  // relative nudge
    void sendServoAbs(double panDeg, double tiltDeg);     // absolute target
    void sendMode(bool autoTrack);                        // auto/manual toggle

    // File channel.
    void requestFileList();
    void requestFile(const QString& name);
    void deleteFile(const QString& name);
    void requestSnapshot();

signals:
    void connected();
    void disconnected();
    void errorOccurred(const QString& message);

    void videoFrameReceived(const QImage& image, quint32 seq,
                            quint32 width, quint32 height, quint64 tsMs);
    void fileListReceived(const QVector<secpc::RemoteFile>& files);
    void fileDownloaded(const QString& name, const QByteArray& bytes);
    void fileDeleted(const QString& name, bool ok);

private slots:
    void onReadyRead();
    void onConnected();
    void onSocketError();

private:
    void sendMessage(quint16 type, const QByteArray& payload = {});
    void processBuffer();
    void handleMessage(quint16 type, const QByteArray& payload);
    void handleVideoFrame(const QByteArray& payload);
    void handleFileList(const QByteArray& payload);
    void handleFileData(const QByteArray& payload);
    void handleFileDeleteResp(const QByteArray& payload);

    QTcpSocket* sock_;
    QByteArray  rx_;  // accumulates bytes until whole frames are available

    // Chunked-download reassembly, keyed by filename.
    struct Download { quint32 total = 0; quint32 received = 0; QByteArray bytes; };
    QHash<QString, Download> downloads_;
};

}  // namespace secpc
