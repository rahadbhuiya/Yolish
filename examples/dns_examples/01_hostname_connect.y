-- Basic hostname resolution + HTTP GET
--
-- NATIVE ONLY. recv_print (like the whole DNS resolver this exercises)
-- only exists in the native compiler backend -- running this through
-- the plain interpreter (./ys file.y) will connect fine but silently
-- skip recv_print, since that function isn't defined there at all.
--
-- Build:  ./ys -c 01_hostname_connect.y --target linux -o hostname_connect
-- Run:    ./hostname_connect

let s = y.net.connect("example.com", 80)
y.println(s)

if s > 0 {
    y.net.send(s, "GET / HTTP/1.0\r\nHost: example.com\r\n\r\n")
    y.net.recv_print(s, 500)
    y.net.close(s)
}
