# Yolish Language Reference

**Version:** v2.1  
**Interpreter/Compiler:** `ys`  
**File extension:** `.y`

---

## Table of Contents

1. [Variables](#1-variables)
2. [Types](#2-types)
3. [Operators](#3-operators)
4. [Control Flow](#4-control-flow)
5. [Loops](#5-loops)
6. [Match Expressions](#6-match-expressions)
7. [Functions](#7-functions)
8. [Closures and First-Class Functions](#8-closures-and-first-class-functions)
9. [Arrays](#9-arrays)
10. [Structs](#10-structs)
11. [Impl — Struct Methods](#11-impl--struct-methods)
12. [String Builtins](#12-string-builtins)
13. [I/O Builtins](#13-io-builtins)
14. [Type Conversion Builtins](#14-type-conversion-builtins)
15. [Multiline and Raw Strings](#15-multiline-and-raw-strings)
16. [Array Functional Builtins](#16-array-functional-builtins)
17. [Capability System](#17-capability-system)
18. [Import / Modules](#18-import--modules)
19. [Annotations](#19-annotations)
20. [Error Handling](#20-error-handling)
21. [Type System](#21-type-system)
22. [String Interpolation](#22-string-interpolation)
23. [Error Objects](#23-error-objects)
24. [REPL](#24-repl)
25. [Standard Library](#25-standard-library)
26. [Native Compiler](#26-native-compiler)
27. [Comments](#27-comments)
28. [Known Limitations](#28-known-limitations)
29. [String Formatting Reference](#29-string-formatting-reference)
30. [Full Example Programs](#30-full-example-programs)
31. [Quick Reference](#31-quick-reference)
32. [Enums](#32-enums)
33. [File I/O — y.fs.*](#33-file-io--yfs)
34. [JSON — y.json.*](#34-json--yjson)
35. [Time — y.time.*](#35-time--ytime)
36. [Path — y.path.*](#36-path--ypath)
37. [Env — y.env.*](#37-env--yenv)
38. [Process & System](#38-process--system)
39. [Garbage Collector — gc.*](#39-garbage-collector--gc)
40. [Testing — ys test](#40-testing--ys-test)
41. [Static Checker — ys check](#41-static-checker--ys-check)
42. [Formatter — ys fmt](#42-formatter--ys-fmt)
---

## 1. Variables

```yolish
let x = 42          -- immutable: cannot be reassigned
var count = 0       -- mutable: can be reassigned
```

Optional type annotation (does not change behaviour):

```yolish
let name: str   = "Diaz"
let age:  int   = 22
let pi:   float = 3.14
let on:   bool  = true
```

Reassignment (`var` only):

```yolish
var i = 0
i = i + 1
```

---

## 2. Types

| Type | Literal | Notes |
|------|---------|-------|
| `int` | `42`, `-7` | 64-bit signed integer |
| `float` | `3.14`, `-0.5` | IEEE 754 double precision (~15 significant digits) |
| `str` | `"hello"` | Max 1023 chars. Escapes: `\n` `\t` `\\` `\"` |
| `bool` | `true`, `false` | |
| `array` | `[1, 2, 3]` | Dynamic, mixed types allowed. Max 512 elements |
| `struct` | `Point { x: 1, y: 2 }` | User-defined |
| `nil` | — | Zero value, unset variable |

---

## 3. Operators

### Arithmetic

```yolish
let a = 10 + 3    -- 13
let b = 10 - 3    -- 7
let c = 10 * 3    -- 30
let d = 10 / 3    -- 3  (integer division for ints)
let e = 10 % 3    -- 1
```

### Comparison

```yolish
x == y    x != y
x <  y    x >  y
x <= y    x >= y
```

### Logical

```yolish
a && b    -- and
a || b    -- or
!a        -- not
```

### String concatenation

```yolish
let s = "Hello" + ", " + "world"
```

### Range

```yolish
0..10    -- inclusive start, exclusive end (used in for loops and match)
```

---

## 4. Control Flow

### if / else if / else

```yolish
if score >= 90 {
    y.println("A")
} else if score >= 80 {
    y.println("B")
} else if score >= 70 {
    y.println("C")
} else {
    y.println("F")
}
```

`if` is also an expression:

```yolish
let label = if x > 0 { "positive" } else { "non-positive" }
```

---

## 5. Loops

### while

```yolish
var i = 0
while i < 5 {
    y.print(i)
    i = i + 1
}
```

### for — range

```yolish
for i in 0..10 {
    y.print(i)    -- 0 1 2 3 4 5 6 7 8 9
}
```

### for — array

```yolish
let arr = [10, 20, 30]
for item in arr {
    y.println(item)
}

-- with index using range
for i in 0..y.len(arr) {
    y.println(arr[i])
}
```

### for — string characters

```yolish
for ch in "hello" {
    y.print(ch)
    y.print("-")
}
-- output: h-e-l-l-o-
```

### break

Exits the innermost loop immediately.

```yolish
for n in 0..100 {
    if n * n > 50 { break }
    y.print(n)    -- 0 1 2 3 4 5 6 7
}
```

```yolish
var i = 0
while i < 10 {
    if i == 5 { break }
    y.print(i)
    i = i + 1
}
-- prints: 0 1 2 3 4
```

### continue

Skips the rest of the current iteration and moves to the next.

```yolish
for n in 1..11 {
    if n % 2 == 0 { continue }
    y.print(n)    -- 1 3 5 7 9
}
```

```yolish
for w in ["a", "skip", "b", "skip", "c"] {
    if w == "skip" { continue }
    y.println(w)    -- a, b, c
}
```

### Nested loops

`break` and `continue` only affect the **innermost** loop:

```yolish
for i in 0..3 {
    for j in 0..5 {
        if j == 3 { break }    -- only exits inner loop
        y.print(j)
    }
    y.print("|")
}
-- 012|012|012|
```

### Notes

- `break` and `continue` work in `while`, `for item in array`, and `for i in range` loops.
- They do **not** escape function boundaries.
- Using `break` or `continue` outside a loop has no effect.

---

## 6. Match Expressions

`match` is a full expression — it returns a value and can appear anywhere a value is expected.

### Syntax

```yolish
match value {
    pattern1 => expr
    pattern2 => { block }
    _        => expr
}
```

### Integer patterns

```yolish
match code {
    200 => y.println("OK")
    404 => y.println("Not Found")
    500 => y.println("Internal Server Error")
    _   => y.println("Unknown")
}
```

### Range patterns

```yolish
match score {
    100     => y.println("Perfect")
    90..100 => y.println("A")
    80..90  => y.println("B")
    70..80  => y.println("C")
    _       => y.println("F")
}
```

Range is inclusive on the left, exclusive on the right (`90..100` matches 90–99).

### String patterns

```yolish
match lang {
    "Yolish" => y.println("capability-aware OS language")
    "C"      => y.println("systems language")
    "Python" => y.println("scripting language")
    _        => y.println("unknown")
}
```

### Bool patterns

```yolish
match flag {
    true  => y.println("enabled")
    false => y.println("disabled")
}
```

### Block body

```yolish
match x {
    1 => {
        y.println("one")
        y.println("confirmed")
    }
    _ => y.println("other")
}
```

### Match as expression

```yolish
-- direct assignment
let label = match status {
    200 => "OK"
    404 => "Not Found"
    _   => "Unknown"
}

-- in function return
fn grade(score) {
    return match score {
        90..100 => "A"
        80..90  => "B"
        70..80  => "C"
        _       => "F"
    }
}

-- nested match
let result = match a {
    1 => match b { 3 => "1-3"  _ => "1-x" }
    _ => "other"
}
```

### Match guards

Add `if <condition>` after a pattern. The arm only matches if the pattern matches **and** the guard is true:

```yolish
fn classify(n) {
    return match n {
        0            => "zero"
        n if n < 0   => "negative"
        n if n < 10  => "small"
        n if n < 100 => "medium"
        _            => "large"
    }
}
y.println(classify(-5))    -- "negative"
y.println(classify(7))     -- "small"
```

### Pattern binding

A bare identifier as a pattern always matches and binds the subject to that name inside the guard and body:

```yolish
fn fizzbuzz(n) {
    return match n {
        n if n % 15 == 0 => "FizzBuzz"
        n if n % 3  == 0 => "Fizz"
        n if n % 5  == 0 => "Buzz"
        n                => y.str(n)
    }
}
```

The binding name is only visible inside that arm — it does not leak out.

### Notes

- Arms are evaluated in order; first match wins.
- Up to 16 arms per match expression.
- If no arm matches and there is no `_`, returns `nil`.
- Guards are only evaluated if the pattern matches.

---

## 7. Functions

### Definition

```yolish
fn add(a, b) {
    return a + b
}
```

Optional return type annotation (documentation only, not enforced):

```yolish
fn multiply(a, b) -> int {
    return a * b
}
```

### Call

```yolish
let result = add(10, 20)
```

### Recursion

```yolish
fn factorial(n) {
    if n <= 1 { return 1 }
    return n * factorial(n - 1)
}
```

### Notes

- Max 8 parameters per function.
- `return` exits immediately at any depth.
- Functions are first-class values — they can be stored in variables and passed as arguments.

---

## 8. Closures and First-Class Functions

Functions are first-class values in Yolish — they can be stored in variables, passed as arguments, and returned from other functions.

### Anonymous functions

```yolish
let double = fn(x) { return x * 2 }
y.println(double(5))    -- 10
```

### Pass as argument

```yolish
fn apply(f, x) { return f(x) }
y.println(apply(double, 7))    -- 14
```

### Closures — capture environment

```yolish
fn make_adder(n) {
    return fn(x) { return x + n }    -- captures n
}

let add5  = make_adder(5)
let add10 = make_adder(10)
y.println(add5(3))     -- 8
y.println(add10(3))    -- 13
```

### Higher-order builtins

| Function | Description |
|----------|-------------|
| `y.map(arr, fn)` | Apply fn to each element, return new array |
| `y.filter(arr, fn)` | Keep elements where fn returns true |
| `y.reduce(arr, fn, init)` | Fold array to single value |
| `y.each(arr, fn)` | Run fn for side effects |

```yolish
let nums = [1, 2, 3, 4, 5]
y.map(nums, fn(x) { return x * 2 })              -- [2, 4, 6, 8, 10]
y.filter(nums, fn(x) { return x % 2 == 0 })      -- [2, 4]
y.reduce(nums, fn(acc, x) { return acc + x }, 0) -- 15
y.each(nums, fn(x) { y.print(x)  y.print(" ") })
```

---

## 9. Arrays

### Create

```yolish
let arr = [10, 20, 30, 40, 50]
```

### Read

```yolish
y.print(arr[0])    -- 10
y.print(arr[2])    -- 30
```

### Write

```yolish
arr[1] = 99
```

### Length

```yolish
y.print(y.len(arr))    -- 5
```

### Iterate

```yolish
for item in arr {
    y.print(item)
}
```

### Push / Pop

```yolish
y.push(arr, 60)    -- append
y.pop(arr)         -- remove and return last element
```

### Slice

```yolish
y.slice(arr, 1, 3)    -- elements at index 1 and 2
```

### Notes

- Max 512 elements per array.
- Element types can be mixed.

---

## 10. Structs

### Define

```yolish
struct Point  { x, y }
struct Person { name, age }
```

### Instantiate

```yolish
let p      = Point  { x: 10, y: 20 }
let person = Person { name: "Diaz", age: 22 }
```

### Field access

```yolish
y.print(p.x)           -- 10
y.print(person.name)   -- Diaz
```

### Structs in functions

```yolish
fn make_point(px, py) {
    return Point { x: px, y: py }
}
let p2 = make_point(5, 15)
```

### Notes

- Struct names must start with an uppercase letter.
- Max 8 fields per struct.
- Field values can be any type including other structs.

---

## 11. Impl — Struct Methods

Use `impl StructName { }` to attach methods to a struct. The first parameter must be `self`.

```yolish
struct Point { x, y }

impl Point {
    fn distance(self) {
        return y.math.sqrt(self.x * self.x + self.y * self.y)
    }
    fn scale(self, factor) {
        return Point { x: self.x * factor, y: self.y * factor }
    }
    fn to_str(self) {
        return y.format(r"Point({0}, {1})", self.x, self.y)
    }
}

let p = Point { x: 3, y: 4 }
y.println(p.distance())          -- 5
y.println(p.scale(2).to_str())   -- Point(6, 8)
```

### Method chaining

```yolish
struct Counter { value }
impl Counter {
    fn inc(self)    { return Counter { value: self.value + 1 } }
    fn add(self, n) { return Counter { value: self.value + n } }
    fn get(self)    { return self.value }
}

let c = Counter { value: 0 }
y.println(c.inc().inc().add(5).get())    -- 7
```

### Notes

- `self` is a **copy** of the struct (value type). Return a new struct to represent mutation.
- Put all methods for a struct in a single `impl` block.
- Methods are resolved by struct name at runtime.

---

## 12. String Builtins

| Function | Returns | Description |
|----------|---------|-------------|
| `y.len(s)` | `int` | String length |
| `y.upper(s)` | `str` | Convert to uppercase |
| `y.lower(s)` | `str` | Convert to lowercase |
| `y.trim(s)` | `str` | Remove leading/trailing whitespace |
| `y.substr(s, start, len)` | `str` | Slice a substring |
| `y.contains(s, sub)` | `bool` | Check if `sub` is in `s` |
| `y.starts_with(s, prefix)` | `bool` | Prefix check |
| `y.ends_with(s, suffix)` | `bool` | Suffix check |
| `y.split(s, sep)` | `array` | Split by separator |
| `y.replace(s, from, to)` | `str` | Replace first match |
| `y.index_of(s, sub)` | `int` | Index of substring (-1 if not found) |
| `y.reverse(s)` | `str` | Reverse the string |
| `y.repeat(s, n)` | `str` | Repeat string n times |
| `y.join(arr, sep)` | `str` | Join array elements with separator |
| `y.format(fmt, ...)` | `str` | Positional string formatting |

### Examples

```yolish
y.upper("hello")                   -- "HELLO"
y.lower("WORLD")                   -- "world"
y.trim("  hi  ")                   -- "hi"
y.substr("Exploidus", 0, 5)        -- "Explo"
y.contains("Yolish", "oli")        -- true
y.starts_with("Yolish", "Yo")      -- true
y.ends_with("Yolish", "ish")       -- true
y.split("a,b,c", ",")              -- ["a", "b", "c"]
y.replace("Hi World", "World", "Yolish")   -- "Hi Yolish"
y.index_of("hello", "ll")          -- 2
y.reverse("Yolish")                -- "hsiloY"
y.repeat("ha", 3)                  -- "hahaha"
y.join(["a", "b", "c"], "-")       -- "a-b-c"
```

Also available as `y.string.*` namespace:

```yolish
y.string.repeat("ha", 3)
y.string.starts_with("Yolish", "Yo")
y.string.ends_with("Yolish", "ish")
y.string.replace("Hi World", "World", "Yolish")
y.string.reverse("Yolish")
y.string.pad_left("42", 6)         -- "    42"
y.string.pad_right("42", 6)        -- "42    "
```

---

## 13. I/O Builtins

### Output

| Function | Description |
|----------|-------------|
| `y.print(val)` | Print value without newline |
| `y.println(val)` | Print value with newline |

### Input

| Function | Returns | Description |
|----------|---------|-------------|
| `y.input()` | `str` | Read a line from stdin |
| `y.input(prompt)` | `str` | Print prompt, then read a line |
| `y.input_int()` | `int` | Read and parse as integer |
| `y.input_int(prompt)` | `int` | Print prompt, read and parse as integer |
| `y.input_float()` | `float` | Read and parse as float |
| `y.input_float(prompt)` | `float` | Print prompt, read and parse as float |

```yolish
let name  = y.input("Your name: ")
let age   = y.input_int("Your age: ")
let score = y.input_float("Score: ")

y.println(y.format(r"Hello {0}, age {1}", name, age))
```

---

## 14. Type Conversion Builtins

| Function | Description |
|----------|-------------|
| `y.str(val)` | Convert int / float / bool / nil / array to string |
| `y.int(val)` | Parse string to int, or truncate float to int |
| `y.float(val)` | Parse string to float, or convert int to float |
| `y.bool(val)` | `"true"/"1"/"yes"` → `true`; `"false"/"0"` → `false`; int `0` → `false` |

```yolish
y.str(42)           -- "42"
y.str(-3.14)        -- "-3.14"
y.str(true)         -- "true"
y.str([1, 2, 3])    -- "[1, 2, 3]"

y.int("42")         -- 42
y.int("-17")        -- -17
y.int(3.9)          -- 3

y.float("3.14")     -- 3.14
y.float(42)         -- 42.0

y.bool("true")      -- true
y.bool("0")         -- false
y.bool(1)           -- true
```

---

## 15. Multiline and Raw Strings

### Multiline (backtick)

Use `` ` `` to write strings that span multiple lines. Newlines are included literally. `{expr}` interpolation works normally.

```yolish
let name = "Diaz"
let banner = `Hello {name}!
Welcome to Yolish.
Have a great day.`
y.println(banner)
```

```yolish
fn sql_query(table, limit) {
    return `SELECT *
FROM {table}
WHERE active = 1
LIMIT {limit}`
}
```

### Raw strings

Prefix with `r` to disable both escape sequences and `{expr}` interpolation. Everything is taken literally.

```yolish
let path = r"C:\Users\Diaz\file.txt"
y.println(path)    -- C:\Users\Diaz\file.txt

let pat = r"\d+\.\d+"
y.println(pat)     -- \d+\.\d+

let tmpl = r"Dear {name}, code {0}"
y.println(tmpl)    -- Dear {name}, code {0}  (no substitution)
```

Raw strings are the recommended way to write `y.format` templates:

```yolish
y.println(y.format(r"Hello {0}! You have {1} messages.", "Diaz", 5))
```

### String syntax summary

| Syntax | Newlines | `\n` `\t` escapes | `{expr}` interpolation |
|--------|----------|-------------------|------------------------|
| `"..."` | `\n` only | Yes | Yes |
| `` `...` `` | literal | No | Yes |
| `r"..."` | `\n` only | No | No |

---

## 16. Array Functional Builtins

### y.range

```yolish
y.range(n)                     -- [0, 1, ..., n-1]
y.range(start, end)            -- [start, ..., end-1]
y.range(start, end, step)      -- with step (can be negative)
```

```yolish
y.range(5)             -- [0, 1, 2, 3, 4]
y.range(2, 7)          -- [2, 3, 4, 5, 6]
y.range(0, 10, 2)      -- [0, 2, 4, 6, 8]
y.range(10, 0, -3)     -- [10, 7, 4, 1]
```

### y.sort

```yolish
y.sort(arr)                           -- ascending (numbers or strings)
y.sort(arr, fn(a, b){ return ... })   -- custom comparator: return true if a before b
```

```yolish
y.sort([5, 2, 8, 1])                           -- [1, 2, 5, 8]
y.sort(["b", "a", "c"])                        -- ["a", "b", "c"]
y.sort([5, 2, 8], fn(a, b){ return a > b })    -- [8, 5, 2]  (descending)
```

Returns a **sorted copy** — original is unchanged.

### y.map

```yolish
y.map(arr, fn(item) { ... })
y.map(arr, fn(item, index) { ... })    -- index available as 2nd param
```

```yolish
y.map([1,2,3,4,5], fn(x){ return x * x })       -- [1, 4, 9, 16, 25]
y.map(["hi","bye"], fn(s){ return y.upper(s) })  -- ["HI", "BYE"]
```

### y.filter

```yolish
y.filter(arr, fn(item) { ... })    -- keep items where fn returns true
```

```yolish
y.filter(y.range(1, 11), fn(n){ return n % 2 == 0 })    -- [2, 4, 6, 8, 10]
```

### y.reduce

```yolish
y.reduce(arr, fn(acc, item) { ... }, initial)
```

```yolish
y.reduce([1,2,3,4,5], fn(acc, x){ return acc + x }, 0)           -- 15
y.reduce([3,7,2,9], fn(m, x){ if x > m { return x } return m }, 0) -- 9
```

### y.each

```yolish
y.each(arr, fn(item) { ... })    -- run fn for side effects, returns nil
```

### y.zip

```yolish
y.zip(arr1, arr2)    -- array of Pair{first, second} structs, length = min(len1, len2)
```

```yolish
let pairs = y.zip(["a","b","c"], [1, 2, 3])
for p in pairs {
    y.println(y.format(r"{0} = {1}", p.first, p.second))
}
-- a = 1
-- b = 2
-- c = 3
```

### y.sum

```yolish
y.sum([1, 2, 3, 4, 5])    -- 15  (works with ints and floats)
```

### y.flatten

```yolish
y.flatten([[1,2],[3,4],[5]])    -- [1, 2, 3, 4, 5]
```

### Pipeline pattern

```yolish
-- sum of squares of odd numbers 1..10
let result = y.reduce(
    y.map(
        y.filter(y.range(1, 11), fn(x){ return x % 2 == 1 }),
        fn(x){ return x * x }
    ),
    fn(acc, x){ return acc + x },
    0
)
y.println(result)    -- 165  (1 + 9 + 25 + 49 + 81)
```

Also available as `y.array.*` namespace:

```yolish
y.array.sort(arr)
y.array.reverse(arr)
y.array.slice(arr, start, end)
y.array.join(arr, sep)
y.array.find(arr, fn(x){ return x > 5 })      -- first matching element
y.array.index_of(arr, val)                     -- index of value (-1 if not found)
y.array.contains(arr, val)                     -- true / false
```

---

## 17. Capability System

Every resource access in Yolish requires an explicit capability token. You cannot accidentally open, read, or write a file without declaring intent.

### Permission flags

| Value | Meaning |
|-------|---------|
| `1` | Read (`CAP_READ`) |
| `2` | Write (`CAP_WRITE`) |
| `3` | Read + Write |
| `4` | Execute (`CAP_EXEC`) |

### Functions

| Function | Description |
|----------|-------------|
| `cap.open(path, perm)` | Open resource, returns capability token |
| `cap.read(cap)` | Read from capability (requires CAP_READ) |
| `cap.write(cap, data)` | Write to capability (requires CAP_WRITE) |
| `cap.close(cap)` | Close and revoke the capability |
| `cap.perm(cap)` | Returns current permission flags |

### Example

```yolish
fn main() {
    let r = cap.open("/input.txt", 1)
    let data = cap.read(r)
    y.print(data)
    cap.close(r)

    let w = cap.open("/log.txt", 2)
    cap.write(w, "Log entry\n")
    cap.close(w)
}
```

### Safe copy pattern

```yolish
fn copy_file(src, dst) {
    let r = cap.open(src, 1)
    let w = cap.open(dst, 2)
    cap.write(w, cap.read(r))
    cap.close(r)
    cap.close(w)
}
```

### Capability annotations

```yolish
@cap(net.read, fs.write)
fn fetch_and_save(url, path) {
    -- only runs if caller has net.read + fs.write capabilities
}
```

### Runtime capability check

```yolish
let caps = y.capabilities()
if y.has_cap(caps, "fs.read") {
    -- safe to read files
}
```

### Why this matters

In C or Python, any code can open any file. In Yolish, capabilities are typed values — they can be passed, stored, and inspected. On Exploidus OS, the kernel validates the capability before granting access. When a capability goes out of scope, it is automatically revoked.

---

## 18. Import / Modules

### Simple import (shared env)

```yolish
import "mathlib.y"

fn main() {
    y.print(square(7))    -- calls fn defined in mathlib.y
}
```

### Named import (isolated namespace)

```yolish
-- utils.y
fn greet(name) { return "Hello, {name}!" }
fn double(n)   { return n * 2 }
```

```yolish
-- main.y
import "utils.y" as util

fn main() {
    y.println(util.greet("Diaz"))    -- Hello, Diaz!
    y.println(util.double(21))       -- 42
}
```

### Notes

- Path is relative to the importing file's directory.
- `import "file.y" as name` runs in an isolated env — symbols do not pollute the caller.
- `import "file.y"` (without `as`) shares the caller's env.
- Circular imports are not detected; avoid them manually.

---

## 19. Annotations

Annotations attach metadata to functions. Declared on the line immediately before `fn`.

### Syntax

```yolish
@annotation_name("argument")
fn function_name(params) { ... }
```

### @intent

Signals resource intent to the Exploidus OS scheduler. Emitted to `stderr` once per outermost call — recursive calls do not repeat it.

```yolish
@intent("io")
fn read_config(path) {
    let f = cap.open(path, 1)
    let data = cap.read(f)
    cap.close(f)
    return data
}

@intent("compute")
fn fibonacci(n) {
    if n <= 1 { return n }
    return fibonacci(n-1) + fibonacci(n-2)
}
```

Common intent values:

| Value | Meaning |
|-------|---------|
| `"io"` | File or device access |
| `"compute"` | CPU-intensive work |
| `"network"` | Network access |
| `"memory"` | Large allocation |

Scheduler output (stderr):
```
[scheduler] intent=compute fn=fibonacci
```

### @audit

Logs every call — tag, function name, and argument count. Output goes to `stderr`.

```yolish
@audit("auth")
fn login(user, pass) {
    if user == "admin" && pass == "1234" { return true }
    return false
}

@audit("write")
fn save_log(msg) {
    let f = cap.open("/var/log/app.log", 2)
    cap.write(f, msg)
    cap.close(f)
}
```

Audit output (stderr):
```
[audit] tag=auth fn=login args=2
[audit] tag=write fn=save_log args=1
```

### Separating output from logs

```bash
ys program.y                           -- both in terminal
ys program.y 2>/dev/null               -- program output only
ys program.y 2>audit.log               -- save audit log
ys program.y >out.txt 2>audit.log      -- save both separately
```

### Notes

- Annotation must be on the line directly before `fn`.
- Only one annotation per function.
- Both `@intent` and `@audit` accept an optional string argument.
- On Exploidus OS, `@intent` integrates with the kernel scheduler directly.

---

## 20. Error Handling

### throw

Raises an error. Execution stops and unwinds to the nearest `catch`.

```yolish
throw "division by zero"
throw "invalid input"
throw y.error("not found", 404)    -- structured error object
```

### try / catch

```yolish
try {
    -- code that might throw
} catch(e) {
    -- e is the thrown value (string or error object)
    y.println("error: " + y.str(e))
}
```

`catch` without a variable is also valid:

```yolish
try {
    throw "oops"
} catch {
    y.println("something went wrong")
}
```

### Example

```yolish
fn divide(a, b) {
    if b == 0 { throw "division by zero" }
    return a / b
}

fn safe_divide(a, b) {
    try {
        return divide(a, b)
    } catch(e) {
        y.print("caught: ")
        y.println(e)
        return -1
    }
}

y.println(safe_divide(10, 2))    -- 5
y.println(safe_divide(10, 0))    -- caught: division by zero / -1
```

### Notes

- `throw` accepts any value (string, int, error object).
- Uncaught throws propagate up through function calls.
- `throw` inside a `for` or `while` loop works correctly — it unwinds to the nearest `try`.

---

## 21. Type System

### y.typeof

```yolish
y.typeof(42)               -- "int"
y.typeof(3.14)             -- "float"
y.typeof("hello")          -- "str"
y.typeof(true)             -- "bool"
y.typeof([1,2,3])          -- "array"
y.typeof(fn(x){return x})  -- "fn"
y.typeof(nil)              -- "nil"
```

### Type check predicates

| Function | Returns |
|----------|---------|
| `y.is_int(v)` | true if int |
| `y.is_float(v)` | true if float |
| `y.is_str(v)` | true if str |
| `y.is_bool(v)` | true if bool |
| `y.is_array(v)` | true if array |
| `y.is_fn(v)` | true if function |
| `y.is_nil(v)` | true if nil |
| `y.is_err(v)` | true if error object |

### Example

```yolish
fn print_typed(v) {
    match y.typeof(v) {
        "int"   => { y.print("int: ")   y.println(v) }
        "str"   => { y.print("str: ")   y.println(v) }
        "array" => { y.print("array: ") y.println(v) }
        _       => { y.print("other: ") y.println(v) }
    }
}
```

---

## 22. String Interpolation

Embed expressions directly inside string literals using `{expr}` syntax.

```yolish
let name = "Diaz"
let age  = 22
y.println("Hello {name}!")                      -- Hello Diaz!
y.println("You are {age} years old.")           -- You are 22 years old.
y.println("Next year: {age + 1}")               -- Next year: 23
y.println("Array has {y.len(arr)} elements.")
y.println("Upper: {y.upper(name)}")             -- Upper: DIAZ
```

Any valid Yolish expression works inside `{}`. Use `\{` for a literal brace.

---

## 23. Error Objects

`y.error(message, code)` creates a structured error value with `.message` and `.code` fields.

```yolish
fn divide(a, b) {
    if b == 0 {
        throw y.error("division by zero", 400)
    }
    return a / b
}

try {
    y.println(divide(10, 0))
} catch(e) {
    y.println(e.message)    -- division by zero
    y.println(e.code)       -- 400
}
```

You can also throw plain strings:

```yolish
throw "something went wrong"    -- catch(e): e is a string
throw y.error("msg", 500)       -- catch(e): e is a struct with .message and .code
```

---

## 24. REPL

Running `ys` without arguments starts an interactive REPL session.

```
$ ys
Yolish v1.5 (Exploidus Runtime)
Type "help" or "exit" to quit.

ys> let x = 10
ys> let y2 = 20
ys> x + y2
30
ys> fn double(n) { return n * 2 }
ys> double(21)
42
ys> y.map([1,2,3], fn(x){ return x*x })
[1, 4, 9]
ys> exit
Bye!
```

The REPL shares a persistent environment across lines — variables and functions defined on one line are available on the next.

---

## 25. Standard Library

For `y.fs`, `y.json`, `y.time`, `y.path`, `y.env`, and `process` — see
sections 33–38 below for full documentation.


### y.math

| Function | Description | Example |
|----------|-------------|---------|
| `y.math.sqrt(n)` | Square root | `y.math.sqrt(144)` → `12` |
| `y.math.pow(base, exp)` | Power | `y.math.pow(2, 10)` → `1024` |
| `y.math.abs(n)` | Absolute value | `y.math.abs(-42)` → `42` |
| `y.math.min(a, b)` | Minimum | `y.math.min(3, 5)` → `3` |
| `y.math.max(a, b)` | Maximum | `y.math.max(3, 5)` → `5` |
| `y.math.clamp(v, lo, hi)` | Clamp to range | `y.math.clamp(15, 0, 10)` → `10` |
| `y.math.floor(n)` | Floor | `y.math.floor(3.7)` → `3` |
| `y.math.ceil(n)` | Ceiling | `y.math.ceil(3.2)` → `4` |
| `y.math.round(n)` | Round to nearest | `y.math.round(3.5)` → `4` |
| `y.math.sign(n)` | Sign (-1, 0, 1) | `y.math.sign(-5)` → `-1` |
| `y.math.log(n)` | Natural logarithm | `y.math.log(2.718)` → `≈1` |
| `y.math.sin(n)` | Sine (radians) | `y.math.sin(0)` → `0` |
| `y.math.cos(n)` | Cosine (radians) | `y.math.cos(0)` → `1` |
| `y.math.tan(n)` | Tangent (radians) | |
| `y.math.pi` | π constant | `3.141592653589793` |

### y.string

| Function | Description | Example |
|----------|-------------|---------|
| `y.string.repeat(s, n)` | Repeat string | `y.string.repeat("ha", 3)` → `"hahaha"` |
| `y.string.starts_with(s, p)` | Prefix check | `y.string.starts_with("Yolish", "Yo")` → `true` |
| `y.string.ends_with(s, p)` | Suffix check | `y.string.ends_with("Yolish", "ish")` → `true` |
| `y.string.replace(s, from, to)` | Replace first match | `y.string.replace("Hi World", "World", "Yolish")` → `"Hi Yolish"` |
| `y.string.reverse(s)` | Reverse string | `y.string.reverse("Yolish")` → `"hsiloY"` |
| `y.string.pad_left(s, w)` | Left-pad with spaces | `y.string.pad_left("42", 6)` → `"    42"` |
| `y.string.pad_right(s, w)` | Right-pad with spaces | `y.string.pad_right("42", 6)` → `"42    "` |

### y.array

| Function | Description | Example |
|----------|-------------|---------|
| `y.array.sort(arr)` | Sort ascending (copy) | `y.array.sort([3,1,2])` → `[1,2,3]` |
| `y.array.reverse(arr)` | Reverse (copy) | `y.array.reverse([1,2,3])` → `[3,2,1]` |
| `y.array.slice(arr, s, e)` | Slice | `y.array.slice(arr, 1, 3)` |
| `y.array.join(arr, sep)` | Join to string | `y.array.join([1,2,3], ", ")` → `"1, 2, 3"` |
| `y.array.find(arr, fn)` | First matching element | `y.array.find(arr, fn(x){return x>5})` |
| `y.array.index_of(arr, val)` | Index of value | `y.array.index_of(arr, 4)` → `2` |
| `y.array.contains(arr, val)` | Check membership | `y.array.contains(arr, 9)` → `true` |

---

## 26. Native Compiler

Compile Yolish source to a standalone native binary — no interpreter needed at runtime.

```bash
ys -c program.y                      -- compile for current OS
ys -c program.y -o myprogram         -- custom output name
ys -c program.y --target linux       -- Linux ELF64
ys -c program.y --target windows     -- Windows PE32+
ys -c program.y --target macos       -- macOS Mach-O
```

### Currently supported in native compiler

| Feature | Native |
|---------|--------|
| Integer arithmetic (`+` `-` `*` `/` `%`) | Yes |
| Comparisons (`<` `<=` `>` `>=` `==` `!=`) | Yes |
| `if` / `else` | Yes |
| `while` loop + `break` / `continue` | Yes |
| `for i in lo..hi` | Yes |
| Functions + recursion | Yes |
| `y.print` / `y.println` (int + string) | Yes |
| Local variables | Yes |
| String literals | Yes |
| Float, arrays, structs | [ ] v1.1 |
| File I/O | [ ] v1.2 |

### Binary output formats

| Target | Format | Notes |
|--------|--------|-------|
| `linux` | ELF64 | Static, runs without libc |
| `windows` | PE32+ | kernel32.dll imports |
| `macos` | Mach-O 64-bit | LC_MAIN entry |
| `exploidus` | (v1.1) | Native Exploidus syscalls |

---

## 27. Comments

```yolish
-- This is a single-line comment
let x = 10    -- inline comment
```

Multi-line block comments:

```yolish
--[
   This is a multi-line comment.
   Everything here is ignored.
   Useful for disabling blocks of code.
]--
```

---

## 28. Known Limitations

### String length
Max **1023 characters**. Longer strings are silently truncated at the lexer.

### Array size
Max **512 elements** per array literal. The interpreter uses a shared pool of up to **8192 array objects** total.

### Float precision
Floats use native `double` (IEEE 754, ~15 significant digits):
```yolish
y.println(0.1 + 0.2)    -- 0.30000000000000004  (standard float behaviour)
```
For exact decimal arithmetic, use integers and scale manually (e.g. store cents, not dollars).

### Function parameters
Max **8 parameters** per function.

### Struct fields
Max **8 fields** per struct.

### Match arms
Max **16 arms** per `match` expression.

### Native compiler
The native compiler (`ys -c`) currently supports integers, strings, arithmetic, `if`/`else`, `while`, `for`, and recursion. Float, arrays, and structs in native mode are coming in **v1.1**.

---

## 29. String Formatting Reference

`y.format` supports two styles:

### Positional arguments

```yolish
y.format("Hello {0}!", "World")              -- "Hello World!"
y.format("{0} + {1} = {2}", 1, 2, 3)        -- "1 + 2 = 3"
y.format("repeat: {0} {0} {0}", "ha")       -- "repeat: ha ha ha"
y.format(r"OS: {0}  v{1}", "Exploidus", 3)  -- "OS: Exploidus  v3"
```

### Inline expression interpolation

```yolish
let name = "Yolish"
let ver  = 1
let s = "Welcome to {name} v{ver}"           -- "Welcome to Yolish v1"

let x = 10
let s2 = "double = {x * 2}"                  -- "double = 20"
```

---

## 30. Full Example Programs

### FizzBuzz

```yolish
for i in 1..101 {
    if i % 15 == 0      { y.println("FizzBuzz") }
    else if i % 3 == 0  { y.println("Fizz") }
    else if i % 5 == 0  { y.println("Buzz") }
    else                 { y.println(i) }
}
```

### Fibonacci

```yolish
fn fib(n) {
    if n <= 1 { return n }
    return fib(n-1) + fib(n-2)
}

for i in 0..10 {
    y.print(fib(i))
    y.print(" ")
}
y.println("")
-- 0 1 1 2 3 5 8 13 21 34
```

### Stack struct

```yolish
struct Stack { items }
impl Stack {
    fn push(self, val) {
        y.push(self.items, val)
        return self
    }
    fn pop(self)  { return y.pop(self.items) }
    fn size(self) { return y.len(self.items) }
    fn peek(self) { return self.items[self.size() - 1] }
}

let s = Stack { items: [] }
s.push(1).push(2).push(3)
y.println(s.size())    -- 3
y.println(s.peek())    -- 3
y.println(s.pop())     -- 3
y.println(s.size())    -- 2
```

### Array pipeline

```yolish
-- sum of squares of odd numbers from 1 to 10
let result = y.reduce(
    y.map(
        y.filter(y.range(1, 11), fn(x){ return x % 2 == 1 }),
        fn(x){ return x * x }
    ),
    fn(acc, x){ return acc + x },
    0
)
y.println(result)    -- 165
```

### Error handling with error objects

```yolish
fn safe_divide(a, b) {
    if b == 0 { throw y.error("division by zero", 400) }
    return a / b
}

try {
    y.println(safe_divide(10, 2))     -- 5
    y.println(safe_divide(10, 0))
} catch(e) {
    y.println("Error " + y.str(e.code) + ": " + e.message)
}
-- Error 400: division by zero
```

### Module usage

```yolish
-- mathlib.y
fn square(x) { return x * x }
fn cube(x)   { return x * x * x }
```

```yolish
-- main.y
import "mathlib.y" as math
y.println(math.square(5))    -- 25
y.println(math.cube(3))      -- 27
```

---

## 31. Quick Reference

```yolish
-- Variables
let x = 10
var y2 = 20

-- if / else if / else
if x > 5 { ... } else if x == 5 { ... } else { ... }

-- loops
while i < 10 { i = i + 1 }
for i in 0..10 { ... }
for item in arr { ... }
for ch in "str" { ... }

-- break / continue
while true { if done { break } if skip { continue } }

-- match
match x {
    1       => "one"
    2..5    => "two to four"
    "hello" => "greeting"
    true    => "yes"
    n if n > 100 => "big"
    _       => "default"
}

-- functions
fn add(a, b) { return a + b }
let r = add(3, 4)

-- closures
let double = fn(x) { return x * 2 }
fn make_adder(n) { return fn(x) { return x + n } }

-- arrays
let arr = [1, 2, 3]
arr[0]                y.len(arr)
y.push(arr, 4)        y.pop(arr)
y.slice(arr, 1, 3)

-- structs
struct Point { x, y }
let p = Point { x: 1, y: 2 }
p.x

-- impl methods
impl Point {
    fn to_str(self) { return "{self.x},{self.y}" }
}
p.to_str()

-- strings
y.upper(s)   y.lower(s)   y.trim(s)   y.reverse(s)
y.substr(s, 0, 5)          y.contains(s, "hi")
y.split(s, ",")             y.join(arr, ", ")
y.replace(s, "old", "new")  y.repeat(s, 3)
y.format(r"{0} is {1}", name, age)
"Hello {name}, age {age}!"   -- interpolation

-- capabilities
let f = cap.open("/file", 1)
cap.read(f)    cap.write(f, data)    cap.close(f)

-- import
import "utils.y" as util
util.greet("Diaz")

-- try / catch / throw
try { throw "oops" } catch(e) { y.println(e) }
try { throw y.error("fail", 500) } catch(e) { y.println(e.message) }

-- types
y.typeof(42)         -- "int"
y.is_str("hi")       -- true
y.str(42)    y.int("5")    y.float("3.14")    y.bool("true")

-- higher-order
y.map(arr, fn(x) { return x * 2 })
y.filter(arr, fn(x) { return x % 2 == 0 })
y.reduce(arr, fn(acc, x) { return acc + x }, 0)
y.sort(arr)    y.range(0, 10)    y.sum(arr)

-- y.math
y.math.sqrt(144)    y.math.pow(2, 10)    y.math.pi
y.math.min(a, b)    y.math.max(a, b)    y.math.clamp(v, lo, hi)
y.math.floor(n)     y.math.ceil(n)      y.math.sign(n)

-- y.string
y.string.pad_left(s, 10)    y.string.pad_right(s, 10)
y.string.starts_with(s, p)  y.string.ends_with(s, p)

-- y.array
y.array.find(arr, fn)       y.array.contains(arr, v)
y.array.index_of(arr, v)    y.array.join(arr, ", ")

-- native compiler
ys -c file.y
ys -c file.y --target linux|windows|macos
```

---

*Release history: see [README.md § Release History](README.md#release-history)*  
*Build instructions: see [BUILD.md](BUILD.md)*
---

## 32. Enums

Enums define a fixed set of named values. Each variant is an integer under the hood
but prints with its qualified name.

```yolish
enum Direction { North  South  East  West }
enum Status    { Loading  Ready  Error  Done }
enum HttpMethod { GET  POST  PUT  DELETE }
```

**Access variants** using `EnumName.Variant`:
```yolish
let dir = Direction.South
y.println(dir)            -- Direction.South
```

**Match on enum values:**
```yolish
match dir {
    Direction.North => y.println("Heading North")
    Direction.South => y.println("Heading South")
    Direction.East  => y.println("Heading East")
    Direction.West  => y.println("Heading West")
}
```

**Compare enum values:**
```yolish
let a = Direction.East
let b = Direction.East
y.println(a == b)                    -- true
y.println(a == Direction.West)       -- false
```

---

## 33. File I/O — y.fs.*

```yolish
y.fs.write(path, data)     -- write string to file (creates or overwrites)
y.fs.append(path, data)    -- append string to file
y.fs.read(path)            -- read entire file as string
y.fs.exists(path)          -- bool — file or directory exists
y.fs.size(path)            -- int — file size in bytes (-1 on error)
y.fs.is_dir(path)          -- bool — path is a directory
y.fs.list(dir)             -- array of filenames in directory
y.fs.mkdir(path)           -- create directory (bool — success)
y.fs.delete(path)          -- delete file or empty directory (bool)
y.fs.rename(old, new)      -- rename/move file (bool)
```

**Examples:**
```yolish
-- Write and read
y.fs.write("notes.txt", "Hello Yolish!
")
y.fs.append("notes.txt", "Second line.
")
let text = y.fs.read("notes.txt")
y.print(text)

-- Check and inspect
y.println(y.fs.exists("notes.txt"))   -- true
y.println(y.fs.size("notes.txt"))     -- 26
y.println(y.fs.is_dir("."))           -- true

-- Directory operations
y.fs.mkdir("output")
let files = y.fs.list(".")
for f in files { y.println(f) }

-- Rename and delete
y.fs.rename("notes.txt", "notes_v2.txt")
y.fs.delete("notes_v2.txt")
```

**Note:** File paths are relative to the script's directory.

---

## 34. JSON — y.json.*

```yolish
y.json.parse(str)       -- parse JSON string → value
y.json.stringify(val)   -- convert value → JSON string
```

**Supported types:** `int`, `float`, `bool`, `nil`/`null`, `str`, arrays, structs/objects.

**Important:** Use backtick strings `` `...` `` for JSON with `{}` braces,
since `{expr}` triggers string interpolation in regular `"..."` strings.

```yolish
-- Parse a JSON object
let obj = y.json.parse(`{"name": "Yolish", "version": 1, "stable": true}`)
y.println(obj.name)             -- Yolish
y.println(obj.version)          -- 1
y.println(obj.stable)           -- true

-- Parse a JSON array
let arr = y.json.parse("[10, 20, 30]")
y.println(arr)

-- Parse primitives
let n = y.json.parse("42")
let f = y.json.parse("3.14")
let b = y.json.parse("true")

-- Stringify
y.println(y.json.stringify(42))         -- 42
y.println(y.json.stringify("hello"))    -- "hello"
y.println(y.json.stringify(true))       -- true

-- Read JSON config file
y.fs.write("config.json", `{"debug": false, "port": 8080}`)
let cfg = y.json.parse(y.fs.read("config.json"))
y.println(cfg.port)                     -- 8080
```

---

## 35. Time — y.time.*

```yolish
y.time.now()              -- int — milliseconds since Unix epoch
y.time.unix()             -- int — seconds since Unix epoch
y.time.sleep(ms)          -- sleep for N milliseconds
y.time.format(ms, fmt)    -- format timestamp as string
```

**Format strings** follow `strftime` conventions:
`%Y` year, `%m` month, `%d` day, `%H` hour, `%M` minute, `%S` second.

```yolish
let now = y.time.now()
y.println(y.time.format(now, "%Y-%m-%d %H:%M:%S"))   -- 2026-06-09 14:30:00
y.println(y.time.format(now, "%Y-%m-%d"))             -- 2026-06-09
y.println(y.time.unix())                              -- 1780967333

-- Measure elapsed time
let start = y.time.now()
y.time.sleep(500)
let elapsed = y.time.now() - start
y.println(elapsed)                                    -- ~500
```

---

## 36. Path — y.path.*

```yolish
y.path.join(a, b, ...)    -- join path segments with /
y.path.basename(path)     -- filename with extension
y.path.dirname(path)      -- parent directory
y.path.ext(path)          -- extension including dot (e.g. ".y")
y.path.stem(path)         -- filename without extension
y.path.abs(path)          -- absolute path
```

```yolish
let p = "/home/bhuiya/projects/hello.y"

y.println(y.path.basename(p))         -- hello.y
y.println(y.path.dirname(p))          -- /home/bhuiya/projects
y.println(y.path.ext(p))              -- .y
y.println(y.path.stem(p))             -- hello
y.println(y.path.join("/home", "bhuiya", "code"))  -- /home/bhuiya/code

-- Check file type
let ext = y.path.ext("script.y")
if ext == ".y" {
    y.println("Yolish file!")
}
```

---

## 37. Env — y.env.*

```yolish
y.env.get(key)         -- string or nil — get environment variable
y.env.set(key, val)    -- set environment variable
y.env.unset(key)       -- remove environment variable
```

```yolish
let home = y.env.get("HOME")
y.println(home)                      -- /home/bhuiya

y.env.set("MY_APP_MODE", "debug")
let mode = y.env.get("MY_APP_MODE")
y.println(mode)                      -- debug

y.env.unset("MY_APP_MODE")
y.println(y.env.get("MY_APP_MODE"))  -- nil
```

---

## 38. Process & System

```yolish
-- Run a shell command, capture stdout
process.spawn(cmd)          -- string (stdout output)
process.spawn_code(cmd)     -- int (exit code)
process.env(key)            -- string or nil (environment variable)
process.pid()               -- int (current process ID)

-- System
sys.exit(code)              -- exit with given code
sys.platform()              -- "linux" | "windows" | "macos"
```

```yolish
-- Run command and capture output
let kernel = process.spawn("uname -s")
y.print(kernel)                         -- Linux

let code = process.spawn_code("true")
y.println(code)                         -- 0

-- Read environment
let home = process.env("HOME")
y.println(home)                         -- /home/bhuiya

-- System info
y.println(sys.platform())               -- linux
y.println(process.pid())                -- 1234

-- Exit with code
if some_error {
    sys.exit(1)
}
```

---

## 39. Garbage Collector — gc.*

Yolish v1.5 ships a **mark-and-sweep garbage collector**. All array and struct
field allocations are automatically tracked. The GC runs periodically between
statements and frees unreachable objects.

### How it works

1. **Allocate** — every `alloc_arr` / `alloc_fld` call goes through the GC allocator
2. **Mark** — scan all live environments, mark every reachable array/struct block
3. **Sweep** — free all unmarked allocations from previous cycles
4. **Cycle protection** — objects allocated in the current cycle are never freed prematurely

The GC triggers automatically every ~128 allocations. You can also control it manually.

### Builtins

```yolish
gc.collect()   -- force a full mark-and-sweep cycle
gc.stats()     -- returns a struct with GC diagnostics
```

`gc.stats()` returns a struct with these fields:

| Field | Type | Description |
|-------|------|-------------|
| `alloc` | int | total allocations since start |
| `freed` | int | total objects freed so far |
| `live` | int | currently live (tracked) objects |
| `threshold` | int | allocs before next auto-collect |
| `cycle` | int | number of GC cycles completed |

### Examples

```yolish
-- Check GC state
let s = gc.stats()
y.print("live: ")  y.println(s.live)
y.print("freed: ") y.println(s.freed)
y.print("cycle: ") y.println(s.cycle)

-- Force a collection
gc.collect()

-- Memory-intensive loop — GC automatically reclaims old arrays
var i = 0
while i < 1000 {
    let data = [i, i+1, i+2, i+3, i+4]
    -- previous iteration's 'data' is overwritten → freed by GC
    i = i + 1
}

-- Verify collection happened
let after = gc.stats()
y.print("freed: ") y.println(after.freed)     -- significant number
y.print("reclaim %: ")
y.println((after.freed * 100) / after.alloc)  -- > 50%
```

### What the GC reclaims

| Situation | Reclaimed? |
|-----------|------------|
| Array overwritten by new assignment |  Yes |
| Struct created in loop, overwritten | ✅Yes |
| Temporary arrays inside functions |  Yes (after function returns) |
| Array stored in a live variable |  No (correctly kept alive) |
| Array returned from a function |  No (correctly kept alive) |

### Notes

- The GC is **conservative** — it may keep some objects alive longer than necessary,
  but it will never free a live object
- For long-running programs, call `gc.collect()` explicitly at natural checkpoints
- `gc.stats().freed` resets to 0 on each collection cycle

---

## 40. Testing — ys test

Write `test` blocks anywhere in a `.y` file:

```yolish
test "description" {
    -- assertions go here
    assert(expr)
    assert_eq(actual, expected)
}
```

Run with:
```bash
ys test file.y
```

Output:
```
Running tests in math.y

  ✓  addition (3 assertions)
  ✓  factorial (3 assertions)
  ✗  broken test
    assertion failed: expected [5], got [6]

2 passed, 1 failed
```

Exit code is `0` if all pass, `1` if any fail — works with CI.

### Assertion builtins

| Function | Fails when |
|----------|-----------|
| `assert(expr)` | `expr` is false |
| `assert(expr, "msg")` | `expr` is false, shows custom message |
| `assert_eq(actual, expected)` | `actual != expected` |
| `assert_neq(a, b)` | `a == b` |
| `assert_true(v)` | `v` is falsy |
| `assert_false(v)` | `v` is truthy |
| `assert_nil(v)` | `v` is not nil |

### Example test file

```yolish
-- math.y
fn add(a, b) { return a + b }
fn factorial(n) {
    if n <= 1 { return 1 }
    return n * factorial(n - 1)
}

test "add works" {
    assert_eq(add(2, 3), 5)
    assert_eq(add(0, 0), 0)
    assert(add(10, 20) == 30)
}

test "factorial" {
    assert_eq(factorial(1), 1)
    assert_eq(factorial(5), 120)
    assert_eq(factorial(10), 3628800)
}

test "strings" {
    let s = "hello"
    assert(y.len(s) == 5)
    assert_eq(y.upper(s), "HELLO")
    assert_false(y.len(s) == 0)
}
```

### Notes

- Code outside `test` blocks runs first (setup code — define functions, load data)
- A failing assertion stops that test block; other tests continue
- `test` blocks are silently skipped when running with plain `ys file.y`

---

## 41. Static Checker — ys check

```bash
ys check file.y
```

Parses and analyzes the file **without running it**. Reports:
- Undefined variable references
- Calls to possibly-undefined functions

```yolish
-- buggy.y
let username = "Bhuiya"
y.println(usernmae)   -- typo
```

```
$ ys check buggy.y
buggy.y:2:11: warning: undefined 'usernmae'
1 warning(s) found.
```

Exit code: `0` for clean, `1` if issues found. Use in CI:

```bash
ys check src/main.y && ys test src/main.y
```

### What ys check analyzes

| Check | Status |
|-------|--------|
| Undefined variables | Done |
| Undefined functions (non-dotted names) | Done |
| Function parameters defined in scope | Done |
| Enum variant names defined | Done |
| Struct names defined | Done |
| Import paths |  planned |
| Type mismatches |  planned |

---

## 42. Formatter — ys fmt

```bash
ys fmt file.y           # prints formatted code to stdout
ys fmt file.y > out.y   # save to new file
ys fmt file.y > file.y  # overwrite in place
```

Normalizes:
- **Indentation** — 4 spaces per level
- **Trailing whitespace** — removed from every line
- **Blank lines** — preserved

```yolish
-- before: mixed indentation
fn greet(name) {
let msg = "Hello " + name
      y.println(msg)
}
```

```yolish
-- after: ys fmt
fn greet(name) {
    let msg = "Hello " + name
    y.println(msg)
}
```

Note: `ys fmt` preserves all string content unchanged (raw strings, backtick strings, comments).