-- error_demo.y  —  v1.4: Better error messages
-- This file intentionally has a typo to demonstrate error reporting.
--
-- Expected output:
--   error_demo.y:N:M: undefined 'cofig' — did you mean 'config'?

let config = "production"
y.println(cofig)
