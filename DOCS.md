# Yolish Language Reference

Version: v0.6  
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
18. [Error Handling](#18-error-handling)
19. [Type System](#19-type-system)
20. [Closures and First-Class Functions](#20-closures-and-first-class-functions)
21. [REPL](#21-repl)
22. [String Interpolation](#22-string-interpolation)
23. [Error Objects](#23-error-objects)
24. [Module System](#24-module-system)
25. [Standard Library](#25-standard-library)

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

-- try / catch / throw
try { throw "oops" } catch(e) { y.println(e) }

-- types
y.typeof(42)         -- "int"
y.is_str("hi")       -- true

-- closures
let f = fn(x) { return x * 2 }
let add5 = make_adder(5)

-- higher-order
y.map(arr, fn(x) { return x * 2 })
y.filter(arr, fn(x) { return x % 2 == 0 })
y.reduce(arr, fn(acc, x) { return acc + x }, 0)
y.each(arr, fn(x) { y.print(x) })
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
./ys examples/ann_test.y               -- both together in terminal
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

---

## 18. Error Handling

Yolish has a `try/catch/throw` system for runtime error handling.

### throw

Raises an error. Execution stops at the throw point and unwinds to the nearest `catch`.

```yolish
throw "division by zero"
throw "invalid input"
```

### try / catch

```yolish
try {
    -- code that might throw
} catch(e) {
    -- e contains the error message as a string
    y.print("error: ") y.println(e)
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
        y.print("caught: ") y.println(e)
        return -1
    }
}

fn main() {
    y.println(safe_divide(10, 2))   -- 5
    y.println(safe_divide(10, 0))   -- caught: division by zero / -1
}
```

### Notes

- `throw` accepts any value (string, int, etc.) and converts it to an error message.
- `catch(e)` binds the error message to `e` as a string.
- `catch` without a variable is also valid: `catch { ... }`.
- Uncaught throws propagate up through function calls.

---

## 19. Type System

### y.typeof

Returns the type of a value as a string.

```yolish
y.typeof(42)              -- "int"
y.typeof(3.14)            -- "float"
y.typeof("hello")         -- "str"
y.typeof(true)            -- "bool"
y.typeof([1,2,3])         -- "array"
y.typeof(fn(x){return x}) -- "fn"
y.typeof(nil)             -- "nil"
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
| `y.is_err(v)` | true if error value |

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

## 20. Closures and First-Class Functions

Functions are first-class values in Yolish — they can be stored in variables, passed as arguments, and returned from other functions.

### Anonymous functions

```yolish
let double = fn(x) { return x * 2 }
y.println(double(5))   -- 10
```

### Pass as argument

```yolish
fn apply(f, x) { return f(x) }
y.println(apply(double, 7))   -- 14
```

### Closures — capture environment

```yolish
fn make_adder(n) {
    return fn(x) { return x + n }  -- captures n
}

let add5  = make_adder(5)
let add10 = make_adder(10)
y.println(add5(3))    -- 8
y.println(add10(3))   -- 13
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

y.map(nums, fn(x) { return x * 2 })         -- [2, 4, 6, 8, 10]
y.filter(nums, fn(x) { return x % 2 == 0 }) -- [2, 4]
y.reduce(nums, fn(acc, x) { return acc + x }, 0) -- 15
y.each(nums, fn(x) { y.print(x) y.print(" ") })
```

---

## 21. REPL

Running `ys` without arguments starts an interactive REPL session.

```
$ ys
Yolish v0.5 REPL  (type 'exit' to quit)
ys> let x = 10
ys> let y2 = 20
ys> x + y2
30
ys> fn double(n) { return n * 2 }
ys> double(21)
42
ys> exit
Bye!
```

The REPL shares a persistent environment across lines — variables and functions defined on one line are available on the next.

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

fn main() {
    try {
        y.println(divide(10, 0))
    } catch(e) {
        y.print("message : ") y.println(e.message)   -- division by zero
        y.print("code    : ") y.println(e.code)       -- 400
    }
}
```

You can also throw plain strings — catch will receive them as a string:
```yolish
throw "something went wrong"   -- catch(e): e is a string
throw y.error("msg", 500)      -- catch(e): e is a struct
```

---

## 24. Module System

`import "file.y" as name` runs the file in an isolated environment and exposes all its definitions as a namespace.

```yolish
-- utils.y
fn greet(name) { return "Hello, {name}!" }
fn double(n)   { return n * 2 }
```

```yolish
-- main.y
import "utils.y" as util

fn main() {
    y.println(util.greet("Diaz"))   -- Hello, Diaz!
    y.println(util.double(21))      -- 42
}
```

### Notes

- The imported file runs in its own env — its globals do not pollute the caller.
- All top-level `fn` and `let`/`var` definitions become fields of the namespace.
- Path is relative to the importing file's directory.
- `import "file.y"` (without `as`) still works and shares the caller's env.

---

## 25. Standard Library

### y.math

| Function | Description | Example |
|----------|-------------|---------|
| `y.math.sqrt(n)` | Integer square root | `y.math.sqrt(144)` → `12` |
| `y.math.pow(base, exp)` | Power | `y.math.pow(2, 10)` → `1024` |
| `y.math.abs(n)` | Absolute value | `y.math.abs(-42)` → `42` |
| `y.math.min(a, b)` | Minimum | `y.math.min(3, 5)` → `3` |
| `y.math.max(a, b)` | Maximum | `y.math.max(3, 5)` → `5` |
| `y.math.clamp(v, lo, hi)` | Clamp to range | `y.math.clamp(15, 0, 10)` → `10` |
| `y.math.floor(n)` | Floor (float → int) | `y.math.floor(3.7)` → `3` |
| `y.math.ceil(n)` | Ceiling (float → int) | `y.math.ceil(3.2)` → `4` |
| `y.math.sign(n)` | Sign (-1, 0, 1) | `y.math.sign(-5)` → `-1` |

### y.string

| Function | Description | Example |
|----------|-------------|---------|
| `y.string.repeat(s, n)` | Repeat string | `y.string.repeat("ha", 3)` → `"hahaha"` |
| `y.string.starts_with(s, p)` | Prefix check | `y.string.starts_with("Yolish", "Yo")` → `true` |
| `y.string.ends_with(s, p)` | Suffix check | `y.string.ends_with("Yolish", "ish")` → `true` |
| `y.string.replace(s, from, to)` | Replace first match | `y.string.replace("Hi World", "World", "Yolish")` → `"Hi Yolish"` |
| `y.string.reverse(s)` | Reverse string | `y.string.reverse("Yolish")` → `"hsiloY"` |
| `y.string.pad_left(s, width)` | Left-pad with spaces | `y.string.pad_left("42", 6)` → `"    42"` |
| `y.string.pad_right(s, width)` | Right-pad with spaces | `y.string.pad_right("42", 6)` → `"42    "` |

### y.array

| Function | Description | Example |
|----------|-------------|---------|
| `y.array.sort(arr)` | Sort ascending (returns new array) | `y.array.sort([3,1,2])` → `[1,2,3]` |
| `y.array.reverse(arr)` | Reverse (returns new array) | `y.array.reverse([1,2,3])` → `[3,2,1]` |
| `y.array.slice(arr, start, end)` | Slice | `y.array.slice(arr, 1, 3)` → elements 1–2 |
| `y.array.join(arr, sep)` | Join to string | `y.array.join([1,2,3], ", ")` → `"1, 2, 3"` |
| `y.array.find(arr, fn)` | First matching element | `y.array.find(arr, fn(x){return x>5})` |
| `y.array.index_of(arr, val)` | Index of value (-1 if not found) | `y.array.index_of(arr, 4)` → `2` |
| `y.array.contains(arr, val)` | Check membership | `y.array.contains(arr, 9)` → `true` |

---

## Updated Quick Reference

```yolish
-- String interpolation
let msg = "Hello {name}, age {age}!"

-- Error objects
throw y.error("not found", 404)
try { ... } catch(e) { y.println(e.message)  y.println(e.code) }

-- Module system
import "utils.y" as util
util.greet("Diaz")

-- y.math
y.math.sqrt(144)          y.math.pow(2,10)
y.math.min(a,b)           y.math.max(a,b)
y.math.clamp(v,lo,hi)     y.math.abs(n)

-- y.string
y.string.repeat(s,n)      y.string.replace(s,from,to)
y.string.starts_with(s,p) y.string.ends_with(s,p)
y.string.reverse(s)       y.string.pad_left(s,w)

-- y.array
y.array.sort(arr)         y.array.reverse(arr)
y.array.slice(arr,s,e)    y.array.join(arr,sep)
y.array.find(arr,fn)      y.array.contains(arr,v)
y.array.index_of(arr,v)
```

---

## Known Limitations (v0.6)

### 1. Match as direct expression (assignment target)

`match` works correctly as a **return value inside a function**, but cannot be assigned directly to a variable at the statement level:

```yolish
-- works:
fn grade(score) {
    match score {
        90..100 => "A"
        80..90  => "B"
        _       => "F"
    }
}
let g = grade(85)   -- "B" ✓

-- also works (inside function body):
fn describe(x) {
    let label = match x {
        404 => "Not Found"
        _   => "Unknown"
    }
    return label
}

-- does not work (top-level direct assignment):
let label = match x {
    404 => "Not Found"
    _   => "Unknown"
}   -- label = nil ✗
```

**Workaround:** wrap the match in a function and call it.

### 2. String and array limits

- Strings: max 255 characters
- String interpolation expressions: max 127 characters
- Arrays: max 64 elements at creation, max 2048 total across the whole program
- Functions: max 8 parameters
- Structs: max 8 fields
- Match arms: max 8 per expression

### 3. Float precision

Floats are stored as fixed-point integers ×1000 (3 decimal places). Very large or very small floats may lose precision.

```yolish
let pi = 3.14159   -- stored as 3.141
```