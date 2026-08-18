-- Native UDP send to a hostname (v2.32) -- host may now be a hostname
-- literal, not just an IPv4 dotted-decimal one. Resolved via DNS at
-- runtime, same as y.net.connect's hostname branch.
--
-- IMPORTANT: this resolver only ever does a real DNS query -- it never
-- consults /etc/hosts (unlike glibc's getaddrinfo, which checks
-- /etc/hosts before ever asking a DNS server). That means "localhost"
-- specifically is a bad test case here: it's conventionally resolved
-- via /etc/hosts, not real DNS, so a real resolver asked about it over
-- the wire will typically answer NXDOMAIN/no-answer rather than
-- 127.0.0.1 -- udp_send correctly returning -1 for "localhost" against
-- a real resolver isn't a bug, it's this simplified resolver's known
-- scope (same as y.net.connect's hostname branch already has). Use a
-- real, DNS-resolvable external hostname to test this, as below.
--
-- Build: ./ys -c 09_udp_hostname.y --target linux -o udp_hostname
-- Run:   ./udp_hostname

let cli = y.net.udp_socket()
let n = y.net.udp_send(cli, "dns.google", 53, "hello")
y.println(n)
y.net.udp_close(cli)
y.println("done")
