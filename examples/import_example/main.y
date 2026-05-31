import "mathlib.y"  as math
import "strutils.y" as str

fn main() {
    y.print("=== math module ===
")
    y.println("square(7)      = {math.square(7)}")
    y.println("cube(4)        = {math.cube(4)}")
    y.println("factorial(6)   = {math.factorial(6)}")
    y.println("fib(10)        = {math.fib(10)}")
    y.println("clamp(15,0,10) = {math.clamp(15, 0, 10)}")
    y.println("abs_val(-42)   = {math.abs_val(-42)}")

    y.print("
=== primes up to 20 ===
")
    y.print("  ")
    for i in 2..21 {
        if math.is_prime(i) { y.print("{i} ") }
    }
    y.print("
")

    y.print("
=== string utils module ===
")
    let s = "hello world from yolish"
    y.println("original   : {s}")
    y.println("word_count : {str.word_count(s)}")
    y.println("capitalize : {str.capitalize(s)}")
    y.println("words      : {str.words(s)}")
    y.println("is_empty   : {str.is_empty(s)}")
    y.println("is_empty ''  : {str.is_empty("")}")
}
