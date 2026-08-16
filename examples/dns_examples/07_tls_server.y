-- Native TLS server + client (v2.31). Needs a real cert/key pair on
-- disk; generate a quick self-signed one for testing:
--   openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem \
--       -days 1 -nodes -subj "/CN=localhost"
--
-- Build (this file is the SERVER half):
--   ./ys -c 07_tls_server.y --target linux -o tls_server
-- Run the server first, then run a client against it (see
-- 05_tls_public_api.y for the tls_connect side, pointed at
-- 127.0.0.1:9443 instead of a real host).



let srv = y.net.tls_listen(9443, "cert.pem", "key.pem")
y.println(srv)
if srv >= 0 {
    let conn = y.net.tls_accept(srv)
    y.println(conn)
    if conn >= 0 {
        y.net.tls_recv_print(conn, 500)
        y.net.tls_send(conn, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK")
        y.net.tls_close(conn)
    }
}
y.println("server done")
