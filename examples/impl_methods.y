-- impl_methods.y
-- Struct methods via impl blocks

struct Point { x, y }

impl Point {
    fn distance(self) {
        return y.math.sqrt(self.x * self.x + self.y * self.y)
    }
    fn scale(self, factor) {
        return Point { x: self.x * factor, y: self.y * factor }
    }
    fn add(self, other) {
        return Point { x: self.x + other.x, y: self.y + other.y }
    }
    fn to_str(self) {
        return y.format(r"Point({0}, {1})", self.x, self.y)
    }
    fn is_origin(self) {
        return self.x == 0 && self.y == 0
    }
}

struct Rect { w, h }

impl Rect {
    fn area(self)      { return self.w * self.h }
    fn perimeter(self) { return 2 * (self.w + self.h) }
    fn is_square(self) { return self.w == self.h }
    fn scale(self, f)  { return Rect { w: self.w * f, h: self.h * f } }
    fn to_str(self)    { return y.format(r"Rect({0}x{1})", self.w, self.h) }
}

struct Counter { value }

impl Counter {
    fn inc(self)       { return Counter { value: self.value + 1 } }
    fn add(self, n)    { return Counter { value: self.value + n } }
    fn reset(self)     { return Counter { value: 0 } }
    fn get(self)       { return self.value }
}

fn main() {
    y.println("=== Point ===")
    let p = Point { x: 3, y: 4 }
    y.println(p.to_str())
    y.println(y.format(r"distance: {0}", p.distance()))
    y.println(y.format(r"is_origin: {0}", p.is_origin()))

    let p2 = p.scale(2)
    y.println(p2.to_str())

    let p3 = p.add(Point { x: 1, y: 0 })
    y.println(p3.to_str())

    y.println("=== method chaining ===")
    y.println(p.scale(3).distance())
    y.println(p.scale(2).add(Point { x: 0, y: 2 }).to_str())

    y.println("=== Rect ===")
    let r = Rect { w: 5, h: 3 }
    y.println(r.to_str())
    y.println(y.format(r"area: {0}  perimeter: {1}", r.area(), r.perimeter()))
    y.println(y.format(r"is_square: {0}", r.is_square()))
    y.println(r.scale(2).to_str())

    y.println("=== Counter ===")
    let c = Counter { value: 0 }
    let c2 = c.inc().inc().inc().add(7)
    y.println(c2.get())
    y.println(c2.reset().get())
}
