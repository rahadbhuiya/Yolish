-- impl_basic.y
-- impl blocks: struct methods, method chaining, a method calling
-- another method on `self`, and structs passed through arrays/loops.

struct Vec { x, y }

impl Vec {
    fn length(self) {
        return y.math.sqrt(self.x * self.x + self.y * self.y)
    }
    fn add(self, other) {
        return Vec { x: self.x + other.x, y: self.y + other.y }
    }
    fn scale(self, k) {
        return Vec { x: self.x * k, y: self.y * k }
    }
    fn to_str(self) {
        return y.format("({0}, {1})", self.x, self.y)
    }
}

struct Counter { value }

impl Counter {
    fn inc(self)    { return Counter { value: self.value + 1 } }
    fn get(self)    { return self.value }
}

fn main() {
    let v1 = Vec { x: 3, y: 4 }
    y.println(v1.length())
    y.println(v1.to_str())

    let v2 = v1.add(Vec { x: 1, y: 1 })
    y.println(v2.to_str())

    y.println("--- method chaining ---")
    y.println(v1.scale(2).add(Vec { x: 0, y: 1 }).to_str())

    y.println("--- structs through arrays/loops ---")
    let vecs = [Vec{x:3,y:4}, Vec{x:0,y:5}, Vec{x:6,y:8}]
    for v in vecs {
        y.println(v.length())
    }

    y.println("--- chained counter (method calling nothing external, just self) ---")
    let c = Counter { value: 0 }
    let c2 = c.inc().inc().inc()
    y.println(c2.get())
}
