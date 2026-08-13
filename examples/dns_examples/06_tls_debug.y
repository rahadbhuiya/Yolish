let h1 = y.net.tls_connect("example.com", 443)
y.println(h1)

if h1 >= 0 {
    let wn = y.net.tls_send(h1, "GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n")
    y.println(wn)
    let rn = y.net.tls_recv_print(h1, 400)
    y.println(rn)
    y.net.tls_close(h1)
}
y.println("done")
