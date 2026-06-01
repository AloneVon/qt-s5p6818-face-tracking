#include "net/TcpClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSslCertificate>
#include <QSslSocket>
#include <QStringList>

#include "net/Protocol.h"  // from terminal/src (INCLUDEPATH) — shared framing

namespace secpc {

using sec::proto::MsgType;

namespace {
constexpr quint32 kMaxMessageBytes = 64u * 1024u * 1024u;  // sanity cap

QByteArray toJson(const QJsonObject& o) {
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}
}  // namespace

TcpClient::TcpClient(QObject* parent)
    : QObject(parent), sock_(new QSslSocket(this)) {
    connect(sock_, &QSslSocket::readyRead, this, &TcpClient::onReadyRead);
    connect(sock_, &QSslSocket::connected, this, &TcpClient::onConnected);
    connect(sock_, &QSslSocket::encrypted, this, &TcpClient::onEncrypted);
    connect(sock_, &QSslSocket::disconnected, this, &TcpClient::disconnected);
    connect(sock_, &QAbstractSocket::errorOccurred, this, &TcpClient::onSocketError);
    // sslErrors is overloaded (signal vs. the sslErrors() getter) — disambiguate.
    connect(sock_, QOverload<const QList<QSslError>&>::of(&QSslSocket::sslErrors),
            this, &TcpClient::onSslErrors);
}

TcpClient::~TcpClient() = default;

bool TcpClient::isConnected() const {
    return sock_->state() == QAbstractSocket::ConnectedState;
}

void TcpClient::connectToTerminal(const QString& host, quint16 port,
                                  const QString& token, bool tls,
                                  const QString& certPath) {
    token_ = token;
    tls_ = tls;
    pinnedCert_ = QSslCertificate();  // clear any pin from a prior connection
    rx_.clear();
    downloads_.clear();
    sock_->abort();

    if (!tls_) {
        sock_->connectToHost(host, port);
        return;
    }

    if (!QSslSocket::supportsSsl()) {
        emit errorOccurred(
            QStringLiteral("TLS requested but this Qt build has no SSL support"));
        return;
    }
    if (!certPath.isEmpty()) {  // pin this exact self-signed cert
        const QList<QSslCertificate> certs =
            QSslCertificate::fromPath(certPath, QSsl::Pem);
        if (certs.isEmpty()) {
            emit errorOccurred(
                QStringLiteral("could not load pinned cert from %1").arg(certPath));
            return;
        }
        pinnedCert_ = certs.first();
    }
    sock_->connectToHostEncrypted(host, port);
}

void TcpClient::disconnectFromTerminal() {
    sock_->disconnectFromHost();
}

// ---- outgoing ------------------------------------------------------------

void TcpClient::sendMessage(quint16 type, const QByteArray& payload) {
    if (!isConnected()) return;
    auto msg = sec::proto::buildMessage(
        static_cast<MsgType>(type),
        reinterpret_cast<const uint8_t*>(payload.constData()),
        static_cast<std::size_t>(payload.size()));
    sock_->write(reinterpret_cast<const char*>(msg.data()),
                 static_cast<qint64>(msg.size()));
}

void TcpClient::sendServoStep(double dPanDeg, double dTiltDeg) {
    QJsonObject o{{"mode", "step"}, {"pan", dPanDeg}, {"tilt", dTiltDeg}};
    sendMessage(static_cast<quint16>(MsgType::ServoCmd), toJson(o));
}

void TcpClient::sendServoAbs(double panDeg, double tiltDeg) {
    QJsonObject o{{"mode", "abs"}, {"pan", panDeg}, {"tilt", tiltDeg}};
    sendMessage(static_cast<quint16>(MsgType::ServoCmd), toJson(o));
}

void TcpClient::sendMode(bool autoTrack) {
    QJsonObject o{{"autoTrack", autoTrack}};
    sendMessage(static_cast<quint16>(MsgType::ModeCmd), toJson(o));
}

void TcpClient::requestFileList() {
    sendMessage(static_cast<quint16>(MsgType::FileListReq));
}

void TcpClient::requestFile(const QString& name) {
    QJsonObject o{{"name", name}};
    sendMessage(static_cast<quint16>(MsgType::FileGetReq), toJson(o));
}

void TcpClient::deleteFile(const QString& name) {
    QJsonObject o{{"name", name}};
    sendMessage(static_cast<quint16>(MsgType::FileDeleteReq), toJson(o));
}

void TcpClient::requestSnapshot() {
    sendMessage(static_cast<quint16>(MsgType::SnapshotReq));
}

// ---- incoming ------------------------------------------------------------

void TcpClient::onConnected() {
    // Plaintext: the TCP connection is ready, so HELLO now. In TLS mode we wait
    // for encrypted() instead — sending before the handshake would leak HELLO.
    if (!tls_) sendHello();
}

void TcpClient::onEncrypted() {
    sendHello();  // TLS handshake done; the channel is now safe for HELLO
}

void TcpClient::sendHello() {
    QJsonObject hello{{"role", "pc"}, {"ver", 1}};
    if (!token_.isEmpty()) hello.insert("token", token_);
    sendMessage(static_cast<quint16>(MsgType::Hello), toJson(hello));
    emit connected();
}

void TcpClient::onSslErrors(const QList<QSslError>& errors) {
    // A self-signed cert legitimately fails CA + hostname verification, so when
    // the user pins a cert we trust the connection iff the peer presents that
    // exact certificate. Anything else is a possible MITM and we refuse.
    if (!pinnedCert_.isNull()) {
        if (sock_->peerCertificate() == pinnedCert_) {
            sock_->ignoreSslErrors();
        } else {
            emit errorOccurred(QStringLiteral(
                "server certificate does not match the pinned cert; refusing"));
            // Explicit abort so the UI sees a prompt disconnected() instead of
            // relying on Qt's default sslErrors-without-ignore path, which can
            // leave the socket in a half-open state until the kernel times out.
            sock_->abort();
        }
        return;
    }
    // No pin: encrypted but unauthenticated (dev convenience). Warn, then go on.
    QStringList msgs;
    for (const QSslError& e : errors) msgs << e.errorString();
    emit errorOccurred(QStringLiteral("TLS warning (unverified, MITM-able): %1")
                           .arg(msgs.join(QStringLiteral("; "))));
    sock_->ignoreSslErrors();
}

void TcpClient::onSocketError() {
    emit errorOccurred(sock_->errorString());
}

void TcpClient::onReadyRead() {
    rx_.append(sock_->readAll());
    processBuffer();
}

void TcpClient::processBuffer() {
    using namespace sec::proto;
    const auto* base = reinterpret_cast<const uint8_t*>(rx_.constData());
    const std::size_t avail = static_cast<std::size_t>(rx_.size());
    std::size_t off = 0;

    while (avail - off >= kHeaderSize) {
        Header h;
        if (!decodeHeader(base + off, avail - off, h)) {
            emit errorOccurred(QStringLiteral("protocol error: bad magic"));
            sock_->abort();
            return;
        }
        if (h.length > kMaxMessageBytes) {
            emit errorOccurred(QStringLiteral("protocol error: oversize message"));
            sock_->abort();
            return;
        }
        if (avail - off < kHeaderSize + h.length) break;  // need more bytes

        QByteArray payload(reinterpret_cast<const char*>(base + off + kHeaderSize),
                           static_cast<int>(h.length));
        handleMessage(h.type, payload);
        off += kHeaderSize + h.length;
    }
    if (off) rx_.remove(0, static_cast<int>(off));
}

void TcpClient::handleMessage(quint16 type, const QByteArray& payload) {
    switch (static_cast<MsgType>(type)) {
        case MsgType::VideoFrame:     handleVideoFrame(payload);     break;
        case MsgType::FileListResp:   handleFileList(payload);       break;
        case MsgType::FileData:       handleFileData(payload);       break;
        case MsgType::FileDeleteResp: handleFileDeleteResp(payload); break;
        case MsgType::Hello:
        case MsgType::Heartbeat:
        default:
            break;  // ignored on the client
    }
}

void TcpClient::handleVideoFrame(const QByteArray& payload) {
    using namespace sec::proto;
    if (payload.size() < static_cast<int>(kVideoSubheaderSize)) return;
    const auto* p = reinterpret_cast<const uint8_t*>(payload.constData());
    const quint32 seq = getU32(p);
    const quint32 w   = getU32(p + 4);
    const quint32 h   = getU32(p + 8);
    const quint64 ts  = getU64(p + 12);

    QImage img;
    const char* jpeg = payload.constData() + kVideoSubheaderSize;
    const int jpegLen = payload.size() - static_cast<int>(kVideoSubheaderSize);
    if (!img.loadFromData(reinterpret_cast<const uchar*>(jpeg), jpegLen, "JPG"))
        return;  // undecodable frame, skip
    emit videoFrameReceived(img, seq, w, h, ts);
}

void TcpClient::handleFileList(const QByteArray& payload) {
    QVector<RemoteFile> files;
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &err);
    if (doc.isArray()) {
        const QJsonArray arr = doc.array();
        files.reserve(arr.size());
        for (const QJsonValue& v : arr) {
            const QJsonObject o = v.toObject();
            RemoteFile f;
            f.name  = o.value("name").toString();
            f.size  = static_cast<quint64>(o.value("size").toDouble());
            f.mtime = static_cast<quint64>(o.value("mtime").toDouble());
            files.push_back(f);
        }
    }
    emit fileListReceived(files);
}

void TcpClient::handleFileData(const QByteArray& payload) {
    using namespace sec::proto;
    if (payload.size() < 9) return;  // [u32 idx][u32 total][u8 nameLen]
    const auto* p = reinterpret_cast<const uint8_t*>(payload.constData());
    const quint32 total = getU32(p + 4);
    const quint8 nameLen = p[8];
    if (payload.size() < 9 + static_cast<int>(nameLen)) return;

    const QString name = QString::fromUtf8(payload.constData() + 9, nameLen);
    const QByteArray bytes = payload.mid(9 + nameLen);

    if (total == 0) {  // not found / empty: terminal sends one empty chunk
        emit fileDownloaded(name, QByteArray());
        downloads_.remove(name);
        return;
    }

    Download& d = downloads_[name];
    if (d.total == 0) d.total = total;
    d.bytes.append(bytes);
    ++d.received;
    if (d.received >= d.total) {
        emit fileDownloaded(name, d.bytes);
        downloads_.remove(name);
    }
}

void TcpClient::handleFileDeleteResp(const QByteArray& payload) {
    const QJsonObject o = QJsonDocument::fromJson(payload).object();
    emit fileDeleted(o.value("name").toString(), o.value("ok").toBool());
}

}  // namespace secpc
