-- Resolves several different real hostnames in one run.
--
-- NATIVE ONLY (see note in 01_hostname_connect.y).
--
-- Build:  ./ys -c 04_multiple_hosts.y --target linux -o multiple_hosts


let a = y.net.connect("example.com", 80)
y.println(a)
if a > 0 { y.net.close(a) }

let b = y.net.connect("www.google.com", 443)
y.println(b)
if b > 0 { y.net.close(b) }
