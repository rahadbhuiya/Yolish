-- TLS certificate verification (v2.33) -- y.net.tls_connect now
-- rejects a self-signed / untrusted certificate instead of silently
-- accepting any cert a server presents.
--
-- To see it reject: build 07_tls_server.y (self-signed) and connect
-- to it here instead of a real host -- it will return -1.
-- To see it accept: run as-is against a real, trusted-cert host.
--
-- Build: ./ys -c 10_tls_cert_verify.y --target linux -o tls_cert_verify
-- Run:   ./tls_cert_verify

let h = y.net.tls_connect("example.com", 443)
y.println(h)
if h >= 0 {
    y.net.tls_close(h)
}
y.println("done")
