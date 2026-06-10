-- gc_basic.y  —  v1.5: Basic GC stats and manual collect

-- Check initial state
let s1 = gc.stats()
y.println("=== Initial State ===")
y.print("alloc     : ") y.println(s1.alloc)
y.print("freed     : ") y.println(s1.freed)
y.print("live      : ") y.println(s1.live)
y.print("threshold : ") y.println(s1.threshold)
y.print("cycle     : ") y.println(s1.cycle)

-- Create a few arrays
let a = [1, 2, 3, 4, 5]
let b = [10, 20, 30]
let c = [100, 200]

let s2 = gc.stats()
y.println("")
y.println("=== After 3 array allocations ===")
y.print("alloc : ") y.println(s2.alloc)
y.print("live  : ") y.println(s2.live)

-- Force a collection
gc.collect()
let s3 = gc.stats()
y.println("")
y.println("=== After gc.collect() ===")
y.print("freed : ") y.println(s3.freed)
y.print("live  : ") y.println(s3.live)
y.print("cycle : ") y.println(s3.cycle)

-- Verify arrays still work after GC
y.println("")
y.println("=== Arrays still valid after GC ===")
y.println(a)
y.println(b)
y.println(c)
