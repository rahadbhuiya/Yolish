-- json_demo.y  —  v1.7: JSON parse & stringify
-- Note: use backtick strings `` for JSON (they are raw, no interpolation)

-- Stringify different types
y.println(y.json.stringify(42))
y.println(y.json.stringify(3.14))
y.println(y.json.stringify(true))

-- Parse numbers
let n = y.json.parse("123")
y.println(n)

let f = y.json.parse("3.14")
y.println(f)

-- Parse boolean
let b = y.json.parse("true")
y.println(b)

-- Parse JSON object using backtick string (raw, no interpolation)
let obj = y.json.parse(`{"name": "Yolish", "version": 1, "stable": true}`)
y.println(obj.name)
y.println(obj.version)

-- Read JSON from file
y.fs.write("config.json", `{"debug": false, "port": 8080}`)
let config = y.json.parse(y.fs.read("config.json"))
y.println(config.port)
y.fs.delete("config.json")

-- Stringify struct
let data = y.json.parse(`{"lang": "yolish", "year": 2025}`)
y.println(y.json.stringify(data))

-- Parse array
let arr = y.json.parse("[10, 20, 30, 40]")
y.println(arr)
