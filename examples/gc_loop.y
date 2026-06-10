-- gc_loop.y  —  v1.5: GC reclaims arrays inside loops

y.println("=== Loop Memory Test ===")
y.println("Creating 300 arrays in a loop...")

let before = gc.stats()

var i = 0
while i < 300 {
    -- Each iteration creates a fresh array.
    -- Previous iteration's array is overwritten → becomes unreachable → GC frees it.
    let arr = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
    i = i + 1
}

let after = gc.stats()

y.println("")
y.print("alloc : ") y.println(after.alloc)
y.print("freed : ") y.println(after.freed)
y.print("live  : ") y.println(after.live)
y.print("cycle : ") y.println(after.cycle)

-- freed should be close to alloc — most arrays were reclaimed
y.println("")
if after.freed > 100 {
    y.println("PASS: GC reclaimed memory during loop")
} else {
    y.println("NOTE: fewer frees than expected")
}
