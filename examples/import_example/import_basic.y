-- import_basic.y
-- import "file.y" merges the file's top-level functions/variables into
-- this scope. Importing the same file again is silently skipped
-- (cached) — needs import_utils.y in the same directory.

import "./import_utils.y"

greet("Diaz")
y.println(add(3, 7))
y.println(max_val(15, 8))
y.println(UTILS_VERSION)

-- importing the same file twice is silently skipped (cached)
import "./import_utils.y"
y.println("double import: ok")
