// TlsConn.h — TLS implementation of IByteStream (OpenSSL), so the same SEC1/WS
// framing runs encrypted: TCP becomes TLS (PC client) and ws becomes wss (mobile
// client). Compiled only when SEC_ENABLE_TLS is defined (build with `make TLS=1`
// / `qmake CONFIG+=tls`); without it this header is empty and the default build
// needs no OpenSSL.
//
//   TlsContext   — wraps one shared SSL_CTX (cert + key), created once in main()
//                  and handed to every TLS listener. SSL_CTX is thread-safe for
//                  spawning per-connection SSL objects.
//   TlsByteStream— one SSL session over an accepted fd. handshake()=SSL_accept;
//                  read()/writeAll() = SSL_read (+ SSL_pending drain) / SSL_write
//                  on a blocking socket bounded by the session's SO_RCVTIMEO.
#pragma once

#ifdef SEC_ENABLE_TLS

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include <openssl/ssl.h>

#include "IByteStream.h"

namespace sec {

class TlsContext {
public:
    // Build a server context from a PEM cert chain + private key. Returns nullptr
    // (after logging the OpenSSL error) if either file is missing/unreadable or
    // the key does not match the cert, so main() can fall back to plaintext-only.
    static std::unique_ptr<TlsContext> create(const std::string& certFile,
                                              const std::string& keyFile);
    ~TlsContext();

    SSL_CTX* ctx() const { return ctx_; }

    TlsContext(const TlsContext&) = delete;
    TlsContext& operator=(const TlsContext&) = delete;

private:
    explicit TlsContext(SSL_CTX* ctx) : ctx_(ctx) {}
    SSL_CTX* ctx_;
};

class TlsByteStream : public IByteStream {
public:
    TlsByteStream(SSL_CTX* ctx, int fd);
    ~TlsByteStream() override { close(); }

    bool handshake() override;                                  // SSL_accept
    int  read(uint8_t* buf, std::size_t cap) override;          // SSL_read + drain
    bool writeAll(const uint8_t* data, std::size_t len) override;  // SSL_write

    int  fd() const override { return fd_; }
    void close() override;

private:
    SSL* ssl_ = nullptr;
    int  fd_;
};

} // namespace sec

#endif // SEC_ENABLE_TLS
