-- A hostname that can't resolve should return -1 cleanly,
-- not crash or hang past the 3s DNS timeout.
--
-- NATIVE ONLY (see note in 01_hostname_connect.y).
--
-- Build:  ./ys -c 03_bad_hostname.y --target linux -o bad_hostname


let s = y.net.connect("thishostnamedoesnotexist12345.invalid", 80)
y.println(s)
-- expect: -1
