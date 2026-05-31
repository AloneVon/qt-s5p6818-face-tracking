#include "TlsConn.h"

#ifdef SEC_ENABLE_TLS

#include <algorithm>
#include <cerrno>

#include <unistd.h>

#include <openssl/err.h>

#include "../core/Log.h"

namespace sec {

namespace {

// Drain and log the OpenSSL error queue (most recent first). Never prints key
// material — only OpenSSL's own error strings.
void logSslErrors(const char* what) {
    unsigned long e;
    while ((e = ERR_get_error()) != 0) {
        char buf[256];
        ERR_error_string_n(e, buf, sizeof(buf));
        LOGE("TLS: %s: %s", what, buf);
    }
}

} // namespace

std::unique_ptr<TlsContext> TlsContext::create(const std::string& certFile,
                                               const std::string& keyFile) {
    // OpenSSL >= 1.1.0 auto-initialises; TLS_server_method negotiates the best
    // available protocol, which we then floor at TLS 1.2.
    SSL_CTX* ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx) {
        logSslErrors("SSL_CTX_new");
        return nullptr;
    }

    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    // Transparently finish any in-handshake renegotiation inside SSL_read/write
    // so callers only ever see real WANT_READ (i.e. "no app data yet").
    SSL_CTX_set_mode(ctx, SSL_MODE_AUTO_RETRY);

    if (SSL_CTX_use_certificate_chain_file(ctx, certFile.c_str()) != 1) {
        logSslErrors("use_certificate_chain_file");
        SSL_CTX_free(ctx);
        return nullptr;
    }
    if (SSL_CTX_use_PrivateKey_file(ctx, keyFile.c_str(), SSL_FILETYPE_PEM) != 1) {
        logSslErrors("use_PrivateKey_file");
        SSL_CTX_free(ctx);
        return nullptr;
    }
    if (SSL_CTX_check_private_key(ctx) != 1) {
        logSslErrors("check_private_key (key does not match cert)");
        SSL_CTX_free(ctx);
        return nullptr;
    }

    LOGI("TLS: context ready (cert=%s)", certFile.c_str());
    return std::unique_ptr<TlsContext>(new TlsContext(ctx));
}

TlsContext::~TlsContext() {
    if (ctx_) SSL_CTX_free(ctx_);
}

TlsByteStream::TlsByteStream(SSL_CTX* ctx, int fd) : fd_(fd) {
    ssl_ = SSL_new(ctx);
    if (ssl_) SSL_set_fd(ssl_, fd_);
    else logSslErrors("SSL_new");
}

bool TlsByteStream::handshake() {
    if (!ssl_) return false;
    // Blocking socket bounded by SO_RCVTIMEO: SSL_accept drives the full
    // handshake and returns 1 on success, or <=0 if a step times out / fails.
    const int r = SSL_accept(ssl_);
    if (r == 1) {
        LOGI("TLS: handshake ok (%s)", SSL_get_version(ssl_));
        return true;
    }
    logSslErrors("SSL_accept");
    return false;
}

int TlsByteStream::read(uint8_t* buf, std::size_t cap) {
    if (!ssl_ || cap == 0) { errno = EAGAIN; return -1; }

    int n = SSL_read(ssl_, buf, static_cast<int>(cap));
    if (n <= 0) {
        const int err = SSL_get_error(ssl_, n);
        switch (err) {
            case SSL_ERROR_WANT_READ:
            case SSL_ERROR_WANT_WRITE:
                errno = EAGAIN; return -1;          // no app data yet — retry
            case SSL_ERROR_ZERO_RETURN:
                return 0;                            // peer's TLS close_notify
            case SSL_ERROR_SYSCALL:
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                    errno = EAGAIN; return -1;       // socket timeout/interrupt
                }
                return 0;                            // EOF / reset → closed
            default:
                logSslErrors("SSL_read");
                return 0;                            // fatal → treat as closed
        }
    }

    // poll() only sees kernel bytes, never the plaintext OpenSSL has already
    // decrypted and buffered. Drain that here so it isn't stranded until the
    // next kernel byte. (SEC1 client→terminal messages are far smaller than the
    // 8 KiB caller buffer, so this loop fully empties SSL and never overflows.)
    int total = n;
    while (static_cast<std::size_t>(total) < cap && SSL_pending(ssl_) > 0) {
        n = SSL_read(ssl_, buf + total, static_cast<int>(cap - static_cast<std::size_t>(total)));
        if (n <= 0) break;
        total += n;
    }
    return total;
}

bool TlsByteStream::writeAll(const uint8_t* data, std::size_t len) {
    if (!ssl_) return false;
    std::size_t off = 0;
    while (off < len) {
        const int n = SSL_write(ssl_, data + off, static_cast<int>(len - off));
        if (n > 0) { off += static_cast<std::size_t>(n); continue; }
        const int err = SSL_get_error(ssl_, n);
        if (err == SSL_ERROR_SYSCALL && errno == EINTR) continue;
        logSslErrors("SSL_write");
        return false;                                // timeout / reset / fatal
    }
    return true;
}

void TlsByteStream::close() {
    if (ssl_) {
        SSL_shutdown(ssl_);   // best-effort close_notify; don't wait for peer's
        SSL_free(ssl_);
        ssl_ = nullptr;
    }
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
}

} // namespace sec

#endif // SEC_ENABLE_TLS
