-- gc_stress.y  —  v1.5: Stress test — 1000 allocs, measure reclaim rate

y.println("=== GC Stress Test ===")
y.println("1000 array allocations in a loop...")

var n = 0
while n < 1000 {
    let data = [n, n+1, n+2, n+3, n+4]
    n = n + 1
}

let s = gc.stats()
y.print("total alloc : ") y.println(s.alloc)
y.print("total freed : ") y.println(s.freed)
y.print("live        : ") y.println(s.live)
y.print("gc cycles   : ") y.println(s.cycle)

let reclaim_pct = (s.freed * 100) / s.alloc
y.print("reclaim %   : ") y.println(reclaim_pct)

y.println("")
if reclaim_pct > 50 {
    y.println("PASS: GC reclaimed >50% of allocations")
} else {
    y.println("NOTE: reclaim rate lower than expected")
}

y.println("")
y.println("=== Final collect ==="  )
gc.collect()
let s2 = gc.stats()
y.print("live after final collect: ") y.println(s2.live)
