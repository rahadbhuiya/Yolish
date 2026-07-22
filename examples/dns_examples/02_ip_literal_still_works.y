-- Confirms the original dotted-decimal fast path is unaffected --
-- this one takes NO DNS round trip at all.
--
-- NATIVE ONLY (see note in 01_hostname_connect.y).
-- Uses 1.1.1.1 (Cloudflare) rather than example.com's real IP, which
-- has changed hosting providers before and will again -- a literal
-- IP baked into example code needs to actually stay valid over time.
--
-- Build:  ./ys -c 02_ip_literal_still_works.y --target linux -o ip_literal


let s = y.net.connect("1.1.1.1", 80)
y.println(s)
if s > 0 {
    y.net.close(s)
}
