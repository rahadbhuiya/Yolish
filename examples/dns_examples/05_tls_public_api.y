-- Exercises the new general y.net.tls_connect/tls_send/tls_recv_print/
-- tls_close API (not the hardcoded tls_test/tls_handshake_test/tls_get_test
-- proof-of-concepts) against a real HTTPS host, plus two connections open
-- at once to check the handle table doesn't cross-wire them.
--
-- NATIVE ONLY, Linux only.
-- Build: ./ys -c 05_tls_public_api.y --target linux -o tls_public_api
-- Run:   ./tls_public_api

let h1 = y.net.tls_connect("example.com", 443)
y.println(h1)

let h2 = y.net.tls_connect("www.google.com", 443)
y.println(h2)

if h1 >= 0 {
    y.net.tls_send(h1, "GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n")
    y.net.tls_recv_print(h1, 400)
    y.net.tls_close(h1)
}

if h2 >= 0 {
    y.net.tls_send(h2, "GET / HTTP/1.1\r\nHost: www.google.com\r\nConnection: close\r\n\r\n")
    y.net.tls_recv_print(h2, 400)
    y.net.tls_close(h2)
}

-- bad handle should fail cleanly, not crash
y.println(y.net.tls_send(99, "x"))
y.println(y.net.tls_recv_print(99, 10))
y.net.tls_close(99)
y.println("done")
