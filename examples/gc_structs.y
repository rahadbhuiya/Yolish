-- gc_structs.y  —  v1.5: GC with structs and field arrays

y.println("=== Struct GC Test ===")

struct Point { x  y }
struct Player { name  score  items }

-- Create structs with field arrays
let p1 = Point { x: 10  y: 20 }
let p2 = Point { x: 30  y: 40 }

let player = Player {
    name:  "Bhuiya"
    score: 9999
    items: "sword"
}

y.print("Player: ") y.println(player.name)
y.print("Score:  ") y.println(player.score)

let s1 = gc.stats()
y.print("live before collect: ") y.println(s1.live)

-- Overwrite p2 with a new struct — old p2's field_vals becomes reclaimable
var count = 0
while count < 50 {
    let tmp = Point { x: count  y: count }
    count = count + 1
}

gc.collect()
let s2 = gc.stats()
y.println("")
y.print("alloc : ") y.println(s2.alloc)
y.print("freed : ") y.println(s2.freed)
y.print("live  : ") y.println(s2.live)

-- Original structs still valid
y.println("")
y.println("=== Original structs intact ===")
y.print("p1.x: ") y.println(p1.x)
y.print("p1.y: ") y.println(p1.y)
y.print("p2.x: ") y.println(p2.x)
