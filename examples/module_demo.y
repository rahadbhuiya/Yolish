-- module_demo.y  —  v1.6: Relative import + caching

import "./utils.y"

greet("Bhuiya")
y.println(add(3, 7))
y.println(max_val(15, 8))
y.println(UTILS_VERSION)

-- Importing same file twice is silently skipped (cached)
import "./utils.y"
y.println("double import: ok")
