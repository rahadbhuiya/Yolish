# Yolish Language Reference

Version: v0.3  
Interpreter: `ys`  
Extension: `.y`

---

## Table of Contents

1. [Variables](#1-variables)
2. [Types](#2-types)
3. [Operators](#3-operators)
4. [Control Flow](#4-control-flow)
5. [Loops](#5-loops)
6. [Match Expressions](#6-match-expressions)
7. [Functions](#7-functions)
8. [Arrays](#8-arrays)
9. [Structs](#9-structs)
10. [String Builtins](#10-string-builtins)
11. [I/O Builtins](#11-io-builtins)
12. [Utility Builtins](#12-utility-builtins)
13. [Capability System](#13-capability-system)
14. [Import](#14-import)
15. [Error Messages](#15-error-messages)
16. [Comments](#16-comments)
17. [Annotations](#17-annotations)

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

Reassignment (only `var`):

```yolish
var i = 0
i = i + 1
```

---

## 2. Types

| Type | Literal | Notes |
|------|---------|-------|
| `int` | `42`, `-7` | 64-bit signed integer |
| `float` | `3.14`, `0.5` | Fixed-point x1000 (3 decimal places) |
| `str` | `"hello"` | Max 255 chars. Escapes: `\n` `\t` `\\` |
| `bool` | `true`, `false` | |
| `array` | `[1, 2, 3]` | Mixed types allowed |
| `struct` | `Point { x: 1, y: 2 }` | User-defined |
| `nil` | — | Zero value, unset variable |

---

## 3. Operators

### Arithmetic

```yolish
let a = 10 + 3    -- 13
let b = 10 - 3    -- 7
let c = 10 * 3    -- 30
let d = 10 / 3    -- 3  (integer division)
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
0..10    -- used with for loops and match (inclusive start, exclusive end)
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
```

### for — string characters

```yolish
for ch in "hello" {
    y.print(ch)
    y.print("-")
}
-- output: h-e-l-l-o-
```

---

## 6. Match Expressions

Match compares a value against a list of patterns and executes the first arm that matches. The `_` wildcard matches anything and acts as a default case.

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

Range is inclusive on the left, exclusive on the right (`90..100` matches 90 to 99).

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
        y.print("one\n")
        y.print("confirmed\n")
    }
    _ => {
        y.print("other\n")
    }
}
```

### Match as expression (return value)

```yolish
fn grade(score) {
    match score {
        90..100 => "A"
        80..90  => "B"
        70..80  => "C"
        _       => "F"
    }
}

let g = grade(85)    -- "B"
```

### Notes

- Arms are evaluated in order; first match wins.
- Max 8 arms per match.
- If no arm matches and there is no `_`, returns `nil`.
- Arm separator is `=>` (fat arrow), not `->`.

---

## 7. Functions

### Definition

```yolish
fn add(a, b) {
    return a + b
}
```

Optional return type annotation (not enforced, documentation only):

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
- `return` exits the function immediately at any depth.
- Functions are first-class values and can be stored in variables.

---

## 8. Arrays

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

### Push

```yolish
y.push(arr, 60)
```

### Notes

- Max 64 elements per array literal.
- Element type can be mixed.

---

## 9. Structs

### Define

```yolish
struct Point {
    x, y
}

struct Person {
    name, age
}
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
y.print(p2.x)    -- 5
```

### Notes

- Struct names must start with an uppercase letter (`Point`, not `point`).
- Max 8 fields per struct.
- Field values can be any type including other structs.

---

## 10. String Builtins

All string functions are also available without the `y.` prefix.

| Function | Returns | Description |
|----------|---------|-------------|
| `y.len(s)` | `int` | String length |
| `y.upper(s)` | `str` | Convert to uppercase |
| `y.lower(s)` | `str` | Convert to lowercase |
| `y.trim(s)` | `str` | Remove leading/trailing whitespace |
| `y.substr(s, start, len)` | `str` | Slice a substring |
| `y.contains(s, sub)` | `bool` | Check if `sub` is in `s` |
| `y.split(s, sep)` | `array` | Split string by separator |
| `y.format(fmt, ...)` | `str` | String interpolation |

### Examples

```yolish
y.upper("hello")              -- "HELLO"
y.lower("WORLD")              -- "world"
y.trim("  hi  ")              -- "hi"
y.substr("Exploidus", 0, 5)   -- "Explo"
y.contains("Yolish", "oli")   -- true
y.split("a,b,c", ",")         -- ["a", "b", "c"]
```

### y.format

Uses `{0}`, `{1}`, ... positional placeholders:

```yolish
let msg = y.format("Hello {0}! You are {1} years old.", "Diaz", 22)
-- "Hello Diaz! You are 22 years old."

let info = y.format("OS: {0}  v{1}", "Exploidus", 3)
-- "OS: Exploidus  v3"
```

---

## 11. I/O Builtins

| Function | Description |
|----------|-------------|
| `y.print(val)` | Print value without newline |
| `y.println(val)` | Print value with newline |
| `y.input()` | Read a line from stdin, returns `str` |

```yolish
y.print("Enter name: ")
let name = y.input()
y.println(y.format("Hello, {0}!", name))
```

---

## 12. Utility Builtins

| Function | Returns | Description |
|----------|---------|-------------|
| `y.len(x)` | `int` | Length of string or array |
| `y.abs(n)` | `int` | Absolute value |
| `y.str(n)` | `str` | Convert int/float/bool to string |
| `y.int(s)` | `int` | Parse string to integer |
| `y.push(arr, val)` | — | Append value to array |
| `y.exit(code)` | — | Exit with status code |

---

## 13. Capability System

Every resource access in Yolish requires an explicit capability token. You cannot accidentally open, read, or write a file without declaring intent.

### Permission flags

| Value | Meaning |
|-------|---------|
| `1` | Read (CAP_READ) |
| `2` | Write (CAP_WRITE) |
| `3` | Read + Write |
| `4` | Execute (CAP_EXEC) |

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

### Why this matters

In C or Python, any code can open any file. In Yolish, capabilities are typed values — they can be passed, stored, and inspected. On Exploidus OS, the kernel validates the capability before granting access. When a capability goes out of scope, it is automatically revoked.

---

## 14. Import

Load and execute another `.y` file. All functions and variables defined in the imported file become available in the current scope.

```yolish
import "mathlib.y"

fn main() {
    y.print(square(7))    -- calls fn defined in mathlib.y
}
```

### Notes

- Path is relative to the working directory where `ys` is run.
- Imported file shares the same environment — all symbols are immediately visible.
- Circular imports are not detected; avoid them manually.

---

## 15. Error Messages

Parse errors include the line number:

```
[YS] parse error (line 5)
```

Runtime errors:

```
[YS] error (line 12): unknown struct field
[YS] error (line 7): cannot open import file
```

---

## 16. Comments

```yolish
-- This is a single-line comment
let x = 10    -- inline comment
```

Multi-line: use multiple `--` lines. Block comments are not supported.

---

## Quick reference

```yolish
-- Variables
let x = 10
var y = 20

-- if / else if / else
if x > 5 { ... } else if x == 5 { ... } else { ... }

-- loops
while i < 10 { i = i + 1 }
for i in 0..10 { ... }
for item in arr { ... }
for ch in "str" { ... }

-- match
match x {
    1       => "one"
    2..5    => "two to four"
    "hello" => "greeting"
    true    => "yes"
    _       => "default"
}

-- functions
fn add(a, b) { return a + b }
let r = add(3, 4)

-- arrays
let arr = [1, 2, 3]
arr[0]            y.len(arr)        y.push(arr, 4)

-- structs
struct Point { x, y }
let p = Point { x: 1, y: 2 }
p.x

-- strings
y.upper(s)   y.lower(s)   y.trim(s)
y.substr(s, 0, 5)         y.contains(s, "hi")
y.split(s, ",")            y.format("{0} is {1}", name, age)

-- capabilities
let f = cap.open("/file", 1)
cap.read(f)    cap.write(f, data)    cap.close(f)

-- import
import "utils.y"
```

---

## 17. Annotations

Annotations attach metadata to functions. They are declared on the line immediately before `fn`.

### Syntax

```yolish
@annotation_name("argument")
fn function_name(params) {
    ...
}
```

### @intent

Signals the resource intent of a function to the Exploidus OS scheduler. The scheduler uses this hint to prioritize or manage resources before the function runs.

The hint is emitted to `stderr` once per outermost call — recursive calls do not repeat it.

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
    return fibonacci(n - 1) + fibonacci(n - 2)
}

@intent("network")
fn fetch(url) {
    -- future: network capability
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

Logs every call to the function — tag, function name, and argument count. Output goes to `stderr` so it does not mix with program output.

```yolish
@audit("sensitive")
fn get_secret() {
    return "key-abc-123"
}

@audit("auth")
fn login(user, pass) {
    if user == "admin" {
        if pass == "1234" { return true }
    }
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
[audit] tag=sensitive fn=get_secret args=0
```

### Separating output from logs

```bash
./ys examples/ann_test.y              -- both together in terminal
./ys examples/ann_test.y 2>/dev/null   -- program output only
./ys examples/ann_test.y 2>audit.log   -- save logs, show output
./ys examples/ann_test.y >out.txt 2>audit.log  -- save both separately
```

### Notes

- Annotation must be on the line directly before `fn`.
- Only one annotation per function.
- Annotation fires once per outermost call — recursive calls are suppressed.
- Both `@intent` and `@audit` accept an optional string argument.
- On Exploidus OS, `@intent` integrates with the kernel scheduler directly.
