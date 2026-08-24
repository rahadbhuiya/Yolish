-- Native TCP for macOS (v2.34) -- UNVERIFIED, no macOS environment
-- available to test this on. Compiles to a valid Mach-O binary and
-- every Darwin-specific constant was checked byte-for-byte against
-- the disassembled output, but nothing here has actually run on real
-- macOS. If you're on a Mac and something's wrong, please report it --
-- see the v2.34 changelog entry for exactly what to suspect first.
--
-- Build: ./ys -c 12_macos_tcp.y --target macos -o macos_tcp
-- Run (on real macOS only): ./macos_tcp

let s = y.net.connect("93.184.216.34", 80)
y.println(s)
if s >= 0 {
    y.net.send(s, "GET / HTTP/1.0\r\nHost: example.com\r\n\r\n")
    y.net.recv_print(s, 500)
    y.net.close(s)
}
