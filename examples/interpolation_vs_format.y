-- interpolation_vs_format.y
-- Confirms two things at once:
--   1. y.format("{0} {1}", ...) numeric placeholders do NOT trigger a
--      VM fallback (they're plain literal text as far as the compiler
--      is concerned; y.format expands them at runtime).
--   2. Real string interpolation ("Hello {name}!") DOES trigger a
--      fallback to the AST interpreter, and still produces the
--      correct final output there.

fn main() {
    let name = "Diaz"
    let age = 22

    -- (1) y.format placeholders — should compile fine in the VM, no fallback
    y.println(y.format("{0} is {1} years old", name, age))

    -- (2) real interpolation — the VM falls back here; AST handles it
    y.println("Hello {name}, you are {age} years old!")
}
