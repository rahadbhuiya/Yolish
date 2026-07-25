# Yolish Language Reference

**Version:** v2.25  
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
11. [Impl: Struct Methods](#11-impl-struct-methods)
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
33. [File I/O: y.fs.*](#33-file-io-yfs)
34. [JSON: y.json.*](#34-json-yjson)
35. [Time: y.time.*](#35-time-ytime)
36. [Path: y.path.*](#36-path-ypath)
37. [Env: y.env.*](#37-env-yenv)
38. [Process & System](#38-process--system)
39. [Garbage Collector: gc.*](#39-garbage-collector-gc)
40. [Testing: ys test](#40-testing-ys-test)
41. [Static Checker: ys check](#41-static-checker-ys-check)
42. [Formatter: ys fmt](#42-formatter-ys-fmt)
43. [Bytecode VM: ys vm](#43-bytecode-vm-ys-vm)
44. [Networking: y.net.*](#44-networking-ynet)
44a. [HTTP client: y.http.*](#44a-http-client-yhttp)
45. [Bitwise Operators](#45-bitwise-operators)
46. [Hashmap: y.map.*](#46-hashmap-ymap)

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
| `array` | `[1, 2, 3]` | Dynamic, mixed types allowed. Max 1024 elements |
| `struct` | `Point { x: 1, y: 2 }` | User-defined |
| `nil` | (none) | Zero value, unset variable |

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

### for: range

```yolish
for i in 0..10 {
    y.print(i)    -- 0 1 2 3 4 5 6 7 8 9
}
```

### for: array

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

### for: string characters

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

`match` is a full expression. It returns a value and can appear anywhere a value is expected.

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

The binding name is only visible inside that arm. It does not leak out.

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
- Functions are first-class values. They can be stored in variables and passed as arguments.

---

## 8. Closures and First-Class Functions

Functions are first-class values in Yolish. They can be stored in variables, passed as arguments, and returned from other functions.

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

### Closures: capture environment

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

`y.push` and `y.pop` always return a **new array**; the original is never mutated.
Arrays use capacity-doubling internally (v2.3), so repeated pushes are O(1) amortized.

```yolish
let a = [1, 2, 3]
let b = y.push(a, 4)
y.println(y.len(a))   -- 3  (a is unchanged)
y.println(y.len(b))   -- 4
```

**Strings have no size limit (v2.4).** They are heap-allocated and
garbage-collected, so file contents, JSON payloads, and large concatenations
all work without truncation, both as literals in source code and as
values built at runtime.

```yolish
let big = "x" * 1   -- conceptually: any length works
let s = y.fs.read("large_file.txt")   -- no truncation, however large the file is
```


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

- Max 1024 elements per array literal (v2.3).
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

## 11. Impl: Struct Methods

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

Returns a **sorted copy**; original is unchanged.

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

In C or Python, any code can open any file. In Yolish, capabilities are typed values: they can be passed, stored, and inspected. On Exploidus OS, the kernel validates the capability before granting access. When a capability goes out of scope, it is automatically revoked.

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
- `import "file.y" as name` runs in an isolated env, so symbols do not pollute the caller.
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

Signals resource intent to the Exploidus OS scheduler. Emitted to `stderr` once per outermost call; recursive calls do not repeat it.

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

Logs every call: tag, function name, and argument count. Output goes to `stderr`.

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
- `throw` inside a `for` or `while` loop works correctly, unwinding to the nearest `try`.

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

The REPL shares a persistent environment across lines, so variables and functions defined on one line are available on the next.

---

## 25. Standard Library

For `y.fs`, `y.json`, `y.time`, `y.path`, `y.env`, and `process`, see
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

Compile Yolish source to a standalone native binary; no interpreter needed at runtime.

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
Max **1024 elements** per array literal (v2.3). Strings have no size limit (v2.4); they are heap-allocated and garbage-collected.

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

## 33. File I/O: y.fs.*

```yolish
y.fs.write(path, data)     -- write string to file (creates or overwrites)
y.fs.append(path, data)    -- append string to file
y.fs.read(path)            -- read entire file as string
y.fs.exists(path)          -- bool: file or directory exists
y.fs.size(path)            -- int: file size in bytes (-1 on error)
y.fs.is_dir(path)          -- bool: path is a directory
y.fs.list(dir)             -- array of filenames in directory
y.fs.mkdir(path)           -- create directory (bool: success)
y.fs.delete(path)          -- delete file or empty directory (bool)
y.fs.rename(old, new)      -- rename/move file (bool)
```

> `y.fs.read/write/append` are binary-safe (as of v2.9) — strings with
> embedded NUL bytes round-trip correctly, and `y.fs.read` reads the
> whole file regardless of size (older versions truncated writes at the
> first NUL byte and truncated reads at 8191 bytes).

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

## 34. JSON: y.json.*

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

## 35. Time: y.time.*

```yolish
y.time.now()              -- int: milliseconds since Unix epoch
y.time.unix()             -- int: seconds since Unix epoch
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

## 36. Path: y.path.*

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

## 37. Env: y.env.*

```yolish
y.env.get(key)         -- string or nil: get environment variable
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
process.fork()              -- 0 in the child, child's pid in the
                             -- parent, -1 on failure/unsupported
                             -- (POSIX only — see §44 for the
                             -- fork-per-connection server pattern)
process.wait(pid)           -- blocks for a specific child, returns
                             -- its exit code, or -1

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

## 39. Garbage Collector: gc.*

Yolish v1.5 ships a **mark-and-sweep garbage collector**. All array and struct
field allocations are automatically tracked. The GC runs periodically between
statements and frees unreachable objects.

### How it works

1. **Allocate**: every `alloc_arr` / `alloc_fld` call goes through the GC allocator
2. **Mark**: scan all live environments, mark every reachable array/struct block
3. **Sweep**: free all unmarked allocations from previous cycles
4. **Cycle protection**: objects allocated in the current cycle are never freed prematurely

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

-- Memory-intensive loop, GC automatically reclaims old arrays
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
| Struct created in loop, overwritten |  Yes |
| Temporary arrays inside functions |  Yes (after function returns) |
| Array stored in a live variable |  No (correctly kept alive) |
| Array returned from a function |  No (correctly kept alive) |

### Notes

- The GC is **conservative**: it may keep some objects alive longer than necessary,
  but it will never free a live object
- For long-running programs, call `gc.collect()` explicitly at natural checkpoints
- `gc.stats().freed` resets to 0 on each collection cycle

---

## 40. Testing: ys test

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

  PASS  addition (3 assertions)
  PASS  factorial (3 assertions)
  FAIL  broken test
    assertion failed: expected [5], got [6]

2 passed, 1 failed
```

Exit code is `0` if all pass, `1` if any fail, so it works with CI.

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

- Code outside `test` blocks runs first (setup code: define functions, load data)
- A failing assertion stops that test block; other tests continue
- `test` blocks are silently skipped when running with plain `ys file.y`

---

## 41. Static Checker: ys check

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

## 42. Formatter: ys fmt

```bash
ys fmt file.y           # prints formatted code to stdout
ys fmt file.y > out.y   # save to new file
ys fmt file.y > file.y  # overwrite in place
```

Normalizes:
- **Indentation**: 4 spaces per level
- **Trailing whitespace**: removed from every line
- **Blank lines**: preserved

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

---

## 43. Bytecode VM: ys vm

```bash
ys vm file.y
```

The bytecode VM is a stack-based execution path that runs alongside the
AST interpreter. Source is compiled to bytecode once, then run by a tight
dispatch loop, so there is no tree-walking per statement and no
environment-chain allocation per scope. On recursion-heavy workloads this
is dramatically faster:

```
fib(27), recursive Fibonacci, about 832K calls

  ys file.y      10.8s   (AST interpreter)
  ys vm file.y    0.3s   (bytecode VM), about 36x faster
```

### What's supported

The VM now compiles the full language, not just a subset:

| Feature | Supported |
|---------|-----------|
| Literals, arithmetic, comparison, logical operators | Yes |
| `let` / `var`, local and global scope | Yes |
| `if` / `else`, `while` | Yes |
| `for` in (range and array/string iteration) | Yes |
| `break` / `continue` | Yes |
| `match` / `match` guards | Yes |
| User-defined functions, recursion, implicit last-expression return | Yes |
| Closures (`fn(x) { ... }`, capturing outer variables) | Yes |
| `try` / `catch` / `throw` | Yes |
| `enum` | Yes |
| `import "file.y"` (merges into current scope) | Yes |
| `import "file.y" as name` (namespace struct) | Yes |
| `impl` blocks and struct methods | Yes |
| Array literals, indexing, index assignment (`arr[i] = x`) | Yes |
| Structs, struct literals, field access | Yes |
| All existing builtins (`y.*`, `process.*`, `sys.*`, `gc.*`, `cap.*`, test assertions) | Yes |
| Auto-calling `fn main()` | Yes |

Closures and `try`/`catch`/`throw` use two different strategies internally.
Closures are compiled as a bridge into the tree-walking interpreter (so a
closure can be handed to `y.map`, `y.filter`, `y.reduce`, `y.sort`, or
`y.each` and behave the same either way). `try`/`catch`/`throw` is native
VM control flow, meaning a `throw` several function calls deep is caught
correctly by an enclosing `try`, without going through the tree-walking
interpreter at all.

### What still falls back to the AST interpreter

A small number of things aren't compiled by the VM yet:

| Feature | Status |
|---------|--------|
| String interpolation (`"Hello {name}"`) | Falls back |
| `@intent` / `@audit` annotations | Falls back |

When a fallback happens, `ys vm` prints which construct triggered it to
stderr, then transparently runs the whole file through the AST
interpreter. The program's output is identical either way, only the
execution path differs, so a fallback never produces wrong results, just
a slower run.

```yolish
-- runs entirely on the VM
fn fib(n) {
    if n <= 1 { return n }
    return fib(n-1) + fib(n-2)
}
y.println(fib(27))
```

```yolish
-- also runs entirely on the VM: structs, impl methods, closures,
-- try/catch, enums, and match all compile now
struct Point { x  y }
impl Point {
    fn dist(self) { return y.math.sqrt(self.x*self.x + self.y*self.y) }
}
let p = Point { x: 3  y: 4 }
y.println(p.dist())

fn make_adder(n) {
    return fn(x) { return x + n }
}
let add5 = make_adder(5)
y.println(add5(10))
```

### Notes

- `ys vm` is stable enough for everyday use, but the AST interpreter
  remains the reference implementation. If the two ever disagree on a
  program's output, that is a bug in the VM, not a language difference.
- The VM shares the same GC, `Val` representation, and the complete
  builtin table with the AST interpreter, so there is only one
  implementation of the builtins to keep correct.
- String interpolation is the main remaining gap. It needs a small
  amount of runtime support to compile, and is the next thing planned
  for the bytecode compiler.

---

## 44. Networking: y.net.*

TCP client sockets. Two independent implementations exist, with **different
capabilities** — read this section carefully before relying on either.

### Interpreter + VM (`ys file.y`, `ys vm file.y`)

Full-featured: hostnames, arbitrary-length receive buffers, string return
values.

```yolish
y.net.connect(host, port)  -- opens a TCP connection, returns a socket
                            -- handle (int), or -1 on failure/timeout
                            -- (10s default connect timeout)
y.net.send(sock, data)     -- sends a string, returns bytes sent or -1
y.net.recv(sock, maxlen)   -- reads up to maxlen bytes, returns a string
                            -- ("" on EOF/closed connection or error)
y.net.close(sock)          -- closes the socket
y.net.listen(port)         -- binds + listens on 0.0.0.0:port (backlog
                            -- 128), returns a listening socket, or -1
y.net.accept(server_sock)  -- blocks until a client connects, returns a
                            -- new connected socket for that client, or -1
y.net.set_timeout(sock, ms) -- sets a receive timeout: makes accept()
                            -- (on a listening socket) or recv() (on a
                            -- connected socket) give up and return -1
                            -- after ms milliseconds instead of blocking
                            -- forever. ms<=0 clears it (blocks again,
                            -- the default). Returns bool (success).
y.net.last_error()         -- string describing the most recent y.net.*
                            -- failure, for debugging
```

```yolish
let sock = y.net.connect("example.com", 80)
if sock == -1 {
    y.println("connect failed: " + y.net.last_error())
} else {
    y.net.send(sock, "GET / HTTP/1.0\r\nHost: example.com\r\n\r\n")
    let resp = y.net.recv(sock, 4096)
    y.println(resp)
    y.net.close(sock)
}
```

A minimal echo server:

```yolish
let srv = y.net.listen(9000)
while true {
    let client = y.net.accept(srv)
    let msg = y.net.recv(client, 1024)
    y.net.send(client, msg)
    y.net.close(client)
}
```

`host` accepts both hostnames (resolved via `getaddrinfo`, IPv4 and IPv6
both tried) and literal IP addresses.

> The Makefile's `windows`/cross-compile target and native Windows
> builds both link against `ws2_32` automatically as of v2.17. Building
> by hand outside the Makefile (e.g. a custom MSVC project) still needs
> it added explicitly (`gcc ... -lws2_32`, or `ws2_32.lib` in MSVC).

### Notes

- `y.net.connect` uses a non-blocking connect + a 10-second timeout
  internally, rather than a bare blocking `connect()` — against an
  unreachable or silently-filtered address, a bare blocking connect can
  hang for the OS's own (often much longer) TCP timeout instead of
  failing promptly.
- By default `y.net.accept` blocks until a client connects and
  `y.net.recv` blocks until data arrives — the normal shape of a
  server accept loop / a blocking read. Use `y.net.set_timeout(sock,
  ms)` if you need either to give up after a while instead (tested:
  both an idle listener and a connected-but-silent peer correctly
  time out and report `y.net.last_error()` as "accept timed out" /
  "recv timed out", distinguishable from other failures).
- `y.net.send` loops internally until all of `data` is sent (or an
  error occurs) — a single underlying `send()` syscall can write fewer
  bytes than requested for large payloads (a "short write"), so this
  matters for anything much bigger than a few KB. Tested with a 5MB
  payload sent whole in one `y.net.send` call.

### Handling more than one client at once: process.fork()

The echo server example above handles exactly one client, start to
finish, before accepting the next — fine for testing, not for a real
server. Fork a child process per connection instead:

```yolish
let srv = y.net.listen(9000)
while true {
    let client = y.net.accept(srv)
    let pid = process.fork()
    if pid == 0 {
        -- child: handle this one client, then exit
        let msg = y.net.recv(client, 1024)
        y.net.send(client, msg)
        y.net.close(client)
        y.exit(0)
    } else {
        -- parent: drop its copy of the fd (the child has its own) and
        -- go straight back to accepting the next connection
        y.net.close(client)
    }
}
```

```yolish
process.fork()      -- returns 0 in the child, the child's pid in the
                     -- parent, or -1 on failure. POSIX only — always
                     -- returns -1 on Windows (fork() doesn't exist
                     -- there; there's no substitute wired up yet).
process.wait(pid)    -- blocks until the given child exits, returns its
                     -- exit code, or -1 on failure
```

`process.fork()` sets `SIGCHLD` to `SIG_IGN` the first time it's
called, so the OS auto-reaps finished children without your script
needing to call `process.wait()` on each one — the right default for a
fire-and-forget fork-per-connection loop like the example above. If you
do need to track a *specific* child (e.g. a worker you're coordinating
with rather than a fire-and-forget connection handler), `process.wait
(pid)` still works — `SIG_IGN` only skips automatic cleanup for
children nobody ever waits on.

Verified with two real clients connecting at the same moment: the
server forked two children, each received the right message with no
cross-talk between connections.

Not implemented for **native compilation** — `process.fork` is an
interpreter/VM builtin only right now, same tier as the rest of
`process.*`.

### UDP: y.net.udp_*

Connectionless datagram sockets — no handshake, no ordering or
delivery guarantees, each send/receive is one whole packet.

```yolish
y.net.udp_socket()                    -- an unbound socket (OS assigns
                                       -- a local port on first send) —
                                       -- for a "client" that sends and
                                       -- waits for replies
y.net.udp_bind(port)                  -- a socket bound to 0.0.0.0:port
                                       -- — for a "server" that needs a
                                       -- known port to receive on
y.net.udp_send(sock, host, port, data) -- sends one datagram, returns
                                       -- bytes sent or -1
y.net.udp_recv(sock, maxlen)          -- blocks for one datagram (unless
                                       -- y.net.set_timeout was called),
                                       -- returns a map {data, host,
                                       -- port} describing it and who
                                       -- sent it, or nil on failure
y.net.udp_close(sock)
```

`udp_recv` returning the sender's address isn't just a nice-to-have —
unlike TCP, where the peer is already known from `connect()`/
`accept()`, a UDP socket can receive from anyone, so knowing who sent
a datagram is usually essential (e.g. to reply to it).

```yolish
-- server
let srv = y.net.udp_bind(9000)
let msg = y.net.udp_recv(srv, 1024)
y.println(y.map.get(msg, "data"))
y.net.udp_send(srv, y.map.get(msg, "host"), y.map.get(msg, "port"),
                "echo: " + y.map.get(msg, "data"))
y.net.udp_close(srv)

-- client
let sock = y.net.udp_socket()
y.net.udp_send(sock, "127.0.0.1", 9000, "hello")
let resp = y.net.udp_recv(sock, 1024)
y.println(y.map.get(resp, "data"))
y.net.udp_close(sock)
```

Tested with a real two-process client/server exchange (client sends,
server receives + replies to the captured sender address, client
receives the reply) on both the interpreter and the VM, and with
`y.net.set_timeout` on a UDP socket (works identically to the TCP
case — same underlying `SO_RCVTIMEO` mechanism, verified it actually
times out rather than blocking).

`y.net.udp_close` is literally `y.net.close` under the hood — closing
a UDP socket is identical to closing a TCP one at the OS level.

Not implemented for native compilation, same tier as `y.net.tls_*`/
`y.http.*`.

### TLS / HTTPS: y.net.tls_*

Real TLS via OpenSSL — **not** a custom crypto implementation. This
wraps OpenSSL's `SSL_*` API (an audited, industry-standard library)
rather than hand-rolling a handshake/cipher suite/certificate parser,
which would be a project on its own and not something to trust without
serious dedicated security review.

**Opt-in at build time** — the default `ys`/`ys.exe` does **not**
include TLS support, so `y.net.tls_*` returns a clear "not compiled
in" error unless built with `make tls` (see BUILD.md). This is
specifically so the project's existing build pipeline (including
Windows CI, where an OpenSSL setup isn't guaranteed) keeps working
unchanged; TLS becomes available wherever the extra dependency is
added — trivial on Linux/macOS, not currently supported on Windows.

```yolish
y.net.tls_connect(host, port)  -- TLS handshake over a new TCP
                                -- connection, returns a handle, or -1
                                -- on failure (bad cert, handshake
                                -- error, or plain connect failure)
y.net.tls_send(handle, data)   -- sends a string, returns bytes sent
                                -- or -1 (loops internally on short
                                -- writes, same as y.net.send)
y.net.tls_recv(handle, maxlen) -- reads up to maxlen bytes, returns a
                                -- string ("" on EOF/error)
y.net.tls_close(handle)        -- closes the connection
```

```yolish
let sock = y.net.tls_connect("example.com", 443)
if sock == -1 {
    y.println("TLS connect failed: " + y.net.last_error())
} else {
    y.net.tls_send(sock, "GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n")
    y.println(y.net.tls_recv(sock, 4096))
    y.net.tls_close(sock)
}
```

**Certificate verification is enabled and enforced** — this was
specifically tested, not just assumed from reading OpenSSL's docs: a
connection to a server presenting a self-signed/untrusted certificate
(verified against a local test server) is correctly rejected with a
"TLS handshake failed" error, while a connection to a server with a
valid certificate (verified against a real public HTTPS endpoint)
succeeds and returns real response data. SNI is sent
(`SSL_set_tlsext_host_name`), required by most modern virtual-hosted
HTTPS servers, and hostname verification is checked against the
certificate (`SSL_set1_host`), not just chain-of-trust validity.

### Notes

- `y.net.tls_connect` reuses `y.net.connect` for the underlying TCP
  connection, so the same 10-second connect timeout applies.
- TLS handles are a **separate id space** from plain `y.net.*` socket
  handles — a TLS handle must only be used with `y.net.tls_*`
  functions, never with plain `y.net.send`/`recv`/`close`, and vice
  versa. Mixing them is a programming error this layer doesn't try to
  detect for you.
- `y.net.listen`/`y.net.accept` have no TLS equivalent yet — this
  batch covers the client side only.
- Not implemented for native compilation, and not implemented for
  Windows even in the interpreter/VM build (the OpenSSL dependency
  isn't wired up for MinGW builds yet).

### Native compilation, Linux only (`ys -c file.y --target linux`)

Raw syscalls (`socket`/`connect`/`bind`/`listen`/`accept`/`read`/`write`/
`close`), no libc linking — this backend produces a fully static ELF
binary with no dynamic linker at all, which is why the API shape here
is narrower:

```yolish
y.net.connect(host_or_ip, port)  -- MUST be a string literal (see below).
                                  -- Accepts an IPv4 literal
                                  -- ("93.184.216.34"), an IPv6 literal
                                  -- ("::1", "2001:db8::1"), or a
                                  -- hostname ("example.com") — the
                                  -- latter is resolved via a hand-
                                  -- written DNS client at connect time,
                                  -- trying an A (IPv4) lookup first and
                                  -- falling back to AAAA (IPv6) if that
                                  -- comes back empty (see "Hostname
                                  -- resolution" below).
                                  -- Returns a socket fd, or -1.
y.net.send(sock, data)          -- data MUST be a string literal.
                                 -- Returns bytes sent, or -1.
y.net.recv_print(sock, maxlen)  -- reads up to maxlen bytes (capped at an
                                 -- internal 4096-byte buffer) and prints
                                 -- them straight to stdout. NOT the same
                                 -- as y.net.recv — see below.
y.net.listen(port)              -- binds 0.0.0.0:port (backlog 128),
                                 -- returns a listening socket, or -1
y.net.accept(server_sock)       -- blocks for a client, returns its
                                 -- socket, or -1
y.net.close(sock)

y.net.udp_socket()              -- an unbound UDP socket (OS assigns a
                                 -- local port on first send) for a
                                 -- client, or -1
y.net.udp_bind(port)            -- a UDP socket bound to 0.0.0.0:port
                                 -- for a server, or -1
y.net.udp_send(sock, host, port, data)
                                 -- host and data MUST be string
                                 -- literals. host is an IPv4 dotted-
                                 -- decimal literal only for now, not a
                                 -- hostname. Returns bytes sent, or -1.
y.net.udp_recv_print(sock, maxlen)
                                 -- reads one datagram and prints its
                                 -- payload to stdout. The sender's
                                 -- address is discarded — see
                                 -- udp_recv_reply_print below if you
                                 -- need to react to it.
y.net.udp_recv_reply_print(sock, maxlen, reply_data)
                                 -- reads one datagram, prints its
                                 -- payload, then sends reply_data back
                                 -- to whichever address it arrived
                                 -- from. reply_data MUST be a string
                                 -- literal. This is the native
                                 -- workaround for udp_recv normally
                                 -- returning the sender's address as
                                 -- part of a y.map (see "Native UDP"
                                 -- below) — replying to the sender is
                                 -- by far the most common reason a
                                 -- program needs that address at all.
y.net.udp_close(sock)           -- same as y.net.close; provided under
                                 -- both names for symmetry with the
                                 -- udp_* family.
```

```yolish
let sock = y.net.connect("example.com", 80)
if sock < 0 {
    y.println("connect failed")
} else {
    y.net.send(sock, "GET / HTTP/1.0\r\nHost: example.com\r\n\r\n")
    y.net.recv_print(sock, 500)
    y.net.close(sock)
}
```

A minimal native server:

```yolish
let srv = y.net.listen(9000)
let client = y.net.accept(srv)
y.net.recv_print(client, 1024)
y.net.send(client, "hello from native Yolish")
y.net.close(client)
y.net.close(srv)
```

Native and interpreter/VM sockets are wire-compatible — a native-compiled
server can accept a connection from an interpreted client and vice versa,
they're both just standard TCP underneath.

`y.net.listen` sets `SO_REUSEADDR` here too, matching the interpreter/VM
version — a restarted native server can rebind the same port
immediately rather than hitting "address already in use" while the
previous instance's socket sits in `TIME_WAIT`. Verified both via
`strace` (confirmed the exact `setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
[1], 4)` call) and in practice (bound, used, closed, and immediately
rebound the same port in one run with no failure).

**Why the native API is different, not just a subset:**

- **`host_or_ip` and `data` must be string literals**, not variables or
  expressions. The native backend has no general runtime string type —
  only compile-time string literals exist as a concept there (the same
  ceiling `y.print`/`y.println` already have for non-literal strings).
  `port` and `sock`, by contrast, can be any integer expression
  (variables, arithmetic, etc.) — only the *string* arguments are
  literal-only.
- **`recv_print` instead of `recv`.** A value-returning `recv(sock,
  maxlen)` would need to hand back a string of runtime-determined
  length, which — again — the native backend has no representation for.
  `recv_print` reads into an internal fixed buffer and writes it
  directly to stdout instead of returning it as a value.
- **No connect timeout.** `connect()` is a bare blocking syscall; against
  an unreachable address this can hang for a long time (whatever the
  kernel's own TCP connect timeout is), not just fail fast.

**Hostname resolution (`ys -c`, Linux):** since nothing is dynamically
linked here (no libc, so no `getaddrinfo`), hostname literals passed to
`y.net.connect` are resolved by a small hand-written DNS client instead:
it reads the first `nameserver` line out of `/etc/resolv.conf` (falling
back to `8.8.8.8` if that file is missing or has none), sends a UDP
query for the A record over raw `socket`/`sendto`/`recvfrom` syscalls,
and tries connecting to every A record in the response in turn, not
just the first, stopping at whichever one actually connects. CNAME
chains work with no special handling — anything that isn't an A record
is just skipped over, which surfaces the eventual A record later in the
same answer section, the way a recursive resolver returns it for a
name chain. Each connect attempt is capped at 3 seconds (non-blocking
connect + `poll` + `SO_ERROR`, not a plain blocking `connect`), so an
unreachable or blackholed record fails fast into the next one instead
of hanging for the OS's default TCP connect timeout on every dead
record before getting anywhere. `SO_RCVTIMEO` (3s) separately covers
the DNS query itself, in case that goes unanswered. This all happens at
runtime; the DNS query packet itself is built at compile time, same as
the address string, since the hostname is already a compile-time
literal either way. Dotted-decimal IPv4 literals skip all of this and
go straight to a plain octet parser, with no DNS round trip.

If the A lookup comes back with nothing connectable, an AAAA (IPv6)
lookup is tried next, with the identical resolv.conf/CNAME/multi-record/
timeout behavior described above, just matching 16-byte AAAA records
and connecting over `AF_INET6`/`sockaddr_in6` instead. IPv4 is always
tried first — an A success skips the AAAA attempt entirely. IPv6
literals ("::1", "2001:db8::1") skip DNS the same way IPv4 literals do:
parsed straight to 16 bytes at compile time and connected directly.

**Native UDP (`ys -c`, Linux):** `udp_socket`/`udp_bind`/`udp_send`/
`udp_close` map directly onto the interpreter/VM versions with no
capability loss. `udp_recv` is where the native backend's limits show
up: the interpreter returns `{data, host, port}` as a `y.map`, since a
UDP socket (unlike a connected TCP one) can receive from anyone, so the
sender's address matters as much as the payload — but there's no map
type and no runtime string type here to construct that value with. Two
narrower primitives cover it instead. `udp_recv_print` reads a datagram
and prints the payload, the same "print instead of return" trade the
TCP side already makes with `recv_print` — the sender's address is
simply dropped. `udp_recv_reply_print` additionally sends a reply back
to that sender: the address recvfrom fills in is kept in the runtime
function's own stack memory and fed straight into the reply's sendto,
without ever becoming a value the Yolish program itself can see or
store. That covers the case that actually needs the sender's address
most of the time (an echo/reply server) without needing a map or
string type to do it — a program that needs the raw address for
something else (logging it, filtering by it, etc.) doesn't have a
native path yet.


Not implemented for native compilation on **macOS** — macOS syscall
numbers and calling convention differ substantially from Linux's, and
this hasn't been ported yet (also tracked in ROADMAP.md). Calling
`y.net.*` while compiling for macOS or Windows hits the "unresolved
symbol" safety net (a clean compile failure, not a broken binary).

---

## 44a. HTTP client: y.http.*

A convenience layer on top of `y.net.*`/`y.net.tls_*` — builds a
correct HTTP/1.1 request, sends it, reads the full response, and
parses out the status code, headers, and body for you, instead of
making you hand-build a raw request and parse the response yourself.

```yolish
y.http.get(url)                      -- GET request
y.http.post(url, body, content_type) -- POST request; content_type
                                      -- defaults to
                                      -- "application/octet-stream"
                                      -- if omitted
```

Both return a `y.map` with three keys, or `nil` on failure (check
`y.net.last_error()`):

```yolish
{
    status:  200,                       -- int
    body:    "...",                     -- string
    headers: { "content-type": "...", ... }  -- y.map, lowercased keys
}
```

```yolish
let r = y.http.get("https://pypi.org/")
if r == nil {
    y.println("request failed: " + y.net.last_error())
} else {
    y.println(y.map.get(r, "status"))
    let headers = y.map.get(r, "headers")
    y.println(y.map.get(headers, "content-type"))
    y.println(y.map.get(r, "body"))
}

let r2 = y.http.post("https://httpbin.example/post",
                      "name=yolish&version=2.15",
                      "application/x-www-form-urlencoded")
y.println(y.map.get(r2, "status"))
```

`https://` URLs need TLS support compiled in (`make tls`) — with the
default build, `y.http.get`/`post` on an `https://` URL returns `nil`
with a clear error rather than silently trying plaintext or crashing.

### What this does and doesn't handle

- Reads the response with a simple "read until the connection closes"
  strategy, relying on the request always sending `Connection: close`
  — correct framing for both `Content-Length` and chunked bodies as
  long as the server honors the header (virtually all do).
- **Chunked `Transfer-Encoding` is decoded** — tested directly against
  a server that deliberately sent a three-chunk response; the
  reassembled body matched exactly.
- **3xx redirects are followed automatically**, up to 10 hops (a loop
  or excessively long chain fails cleanly with "too many redirects"
  rather than hanging — tested directly against a server that always
  redirects back to itself). `Location` can be a full URL or a
  relative path, both are handled; the final response (after
  following) is what's returned. Method/body handling on redirect
  matches curl/browser default behavior: a `303` always downgrades to
  `GET` with no body; `301`/`302` downgrade a `POST` to `GET` too
  (servers commonly rely on this); `307`/`308` preserve the original
  method and body unchanged. All three cases were tested directly
  against local servers built to check exactly what arrived at the
  redirect target.
- No cookies, no compression (`Accept-Encoding` isn't sent, so a
  compliant server responds uncompressed), no connection reuse
  (matches the `Connection: close` framing strategy above — each
  request/redirect hop opens a fresh connection).
- This is a basic client for straightforward request/response use, not
  a full-featured one.

Not implemented for native compilation, same tier as `y.net.tls_*`.

---

## 45. Bitwise Operators

```yolish
a & b     -- bitwise AND
a | b     -- bitwise OR
a ^ b     -- bitwise XOR
a << n    -- shift left
a >> n    -- shift right (arithmetic, sign-preserving)
~a        -- bitwise NOT (unary)
```

```yolish
y.println(5 & 3)    -- 1
y.println(5 | 2)    -- 7
y.println(5 ^ 1)    -- 4
y.println(1 << 4)   -- 16
y.println(256 >> 4) -- 16
y.println(~0)       -- -1
```

Operands are coerced to integers. Precedence (highest to lowest binds
tighter): `* / %`  >  `+ -`  >  `<< >>`  >  comparisons  >  `&`  >  `^`
`>` `|`  >  `&&`  >  `||`  — matching C's precedence ordering. Use
parentheses when mixing bitwise and comparison/logical operators in the
same expression if you're not sure, since this differs from some other
languages (e.g. Python).

Supported everywhere: lexer, parser, tree-walking interpreter, and the
bytecode VM. Not yet supported in native compilation (`ys -c`).

---

## 46. Hashmap: y.map.*

An open-addressing hash table with automatic growth. Keys must be
`string`, `int`, or `bool`; values can be anything.

```yolish
y.map.new()             -- creates a new, empty map
y.map.set(m, k, v)      -- inserts/updates a key, returns the map
y.map.get(m, k)         -- returns the value, or nil if absent
y.map.has(m, k)         -- bool: does the key exist?
y.map.delete(m, k)      -- removes a key, returns bool (did it exist?)
y.map.keys(m)           -- array of all keys
y.map.values(m)         -- array of all values
y.map.len(m)            -- number of entries
```

```yolish
let m = y.map.new()
m = y.map.set(m, "name", "Yolish")
m = y.map.set(m, "version", 29)

y.println(y.map.get(m, "name"))      -- Yolish
y.println(y.map.has(m, "missing"))   -- false
y.println(y.map.len(m))              -- 2

for k in y.map.keys(m) {
    y.println(k)
}
```

### Important: maps mutate in place — unlike arrays

`y.push`/`y.pop` are explicitly **immutable** — they always return a new
array and never touch the original. Maps are the opposite, deliberately:
**`y.map.set` and `y.map.delete` mutate the map in place**, through its
internal shared storage, which is the usual hashmap contract and needed
for reasonable performance (an immutable map would have to copy the whole
table on every insert).

- **`y.map.set`** — still reassign the result (`m = y.map.set(m, k, v)`).
  A set can trigger the table to grow, which allocates a new internal
  buffer; reassigning is what carries that new buffer back into your
  variable. If you forget to reassign, small maps will often *appear* to
  work anyway (since nothing grew yet) and then mysteriously stop working
  once they cross the growth threshold — always reassign to be safe.
- **`y.map.delete`** — do **not** reassign. It returns a `bool` (whether
  the key existed), not the map. `y.map.delete(m, k)` as a bare statement
  is correct and already mutates `m`; writing `m = y.map.delete(m, k)`
  will overwrite your map variable with `true`/`false`.

### Notes

- Available in the tree-walking interpreter and the bytecode VM. Not yet
  supported in native compilation (`ys -c`).
- `y.len(m)` also works on maps (in addition to `y.map.len(m)`).
- Load factor is capped at 70%; the table doubles in size automatically.