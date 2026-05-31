#!/usr/bin/env bash
# gen-cert.sh — make a self-signed TLS cert+key for the terminal's TLS/wss
# listeners. The terminal loads these (Config::tlsCertFile / tlsKeyFile); clients
# pin/trust the public cert (PC client: point its cert field at it; mobile:
# install it on the device as trusted).
#
# Usage:
#   tools/gen-cert.sh [SAN ...]          # SANs: the board's IP(s) and/or hostname(s)
#   tools/gen-cert.sh 192.168.1.50 sec-terminal.local
#   OUT=certs DAYS=825 CN=sec-terminal tools/gen-cert.sh 10.0.0.7
#
# Output (in $OUT, default ./certs):
#   server-key.pem    private key  (secret — keep readable only by the terminal)
#   server-cert.pem   certificate  (public — copy to clients to pin/trust)
#
# Needs OpenSSL >= 1.1.1 (for -addext). The Anaconda/Homebrew openssl works;
# macOS's system LibreSSL may not — use a real OpenSSL if -addext errors.
set -euo pipefail

OUT="${OUT:-certs}"
DAYS="${DAYS:-825}"            # <= 825 keeps Apple and most clients happy
CN="${CN:-sec-terminal}"

mkdir -p "$OUT"

# subjectAltName from the args: IPv4 literals become IP:, everything else DNS:.
# localhost / 127.0.0.1 are always included so local testing verifies hostnames.
sans="DNS:localhost,IP:127.0.0.1"
for h in "$@"; do
    if [[ "$h" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
        sans="$sans,IP:$h"
    else
        sans="$sans,DNS:$h"
    fi
done

echo "Generating self-signed cert (CN=$CN, SAN=$sans, ${DAYS}d) -> $OUT/"
openssl req -x509 -newkey rsa:2048 -nodes \
    -keyout "$OUT/server-key.pem" \
    -out    "$OUT/server-cert.pem" \
    -days   "$DAYS" \
    -subj   "/CN=$CN" \
    -addext "subjectAltName=$sans"

chmod 600 "$OUT/server-key.pem"

cat <<EOF

Done.
  key : $OUT/server-key.pem   (secret  — readable only by the terminal)
  cert: $OUT/server-cert.pem  (public  — give to clients to pin/trust)

Next:
  1) Terminal: set Config::tlsCertFile / tlsKeyFile to these paths and build with
     TLS:  make TLS=1   (cross builds add TLS=1 too). Restart it; it then opens
     TLS :8443 (PC) and wss :8444 (mobile) alongside the plaintext ports.
  2) PC client: tick "TLS", point the cert field at server-cert.pem, and connect
     to the board's IP on port 8443.
  3) Mobile client: tick "TLS" and connect on port 8444. A self-signed cert must
     be trusted by the device/WebView first (install server-cert.pem as a trusted
     CA on the phone), otherwise the OS blocks the wss handshake.
EOF
