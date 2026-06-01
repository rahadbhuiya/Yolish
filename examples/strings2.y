-- strings2.y
-- Multiline strings (backtick) and raw strings (r"...")

fn main() {
    let name = "Diaz"
    let lang = "Yolish"
    let ver  = 6

    y.println("=== multiline strings ===")

    let banner = `=============================
  Welcome to {lang} v0.{ver}!
  The OS language of Exploidus.
=============================`
    y.println(banner)

    let haiku = `old pond —
a frog jumps in,
sound of water`
    y.println(haiku)

    y.println("=== multiline with expressions ===")

    let a = 12
    let b = 34
    let report = `Values  : {a} and {b}
Sum     : {a + b}
Product : {a * b}
Max     : {y.math.max(a, b)}`
    y.println(report)

    y.println("=== raw strings (no escapes, no interpolation) ===")

    let regex_like = r"pattern: \d+\.\d+"
    y.println(regex_like)

    let path = r"C:\Users\Diaz\Documents\file.txt"
    y.println(path)

    let literal_braces = r"template: {name} and {0}"
    y.println(literal_braces)

    y.println("=== raw string as y.format template ===")

    let tpl = r"Hello {0}! You have {1} messages."
    y.println(y.format(tpl, name, 5))

    y.println("=== multiline in a function ===")

    fn make_card(title, body) {
        return `+----------------------+
| {title}
| {body}
+----------------------+`
    }

    y.println(make_card("Status", "All systems go"))
    y.println(make_card("User",   y.format("{0}, age {1}", name, 22)))
}
