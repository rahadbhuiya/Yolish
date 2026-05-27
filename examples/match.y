-- match.y
-- Match expressions: literals, ranges, strings, bools, wildcard

fn grade(score) {
    match score {
        100     => "Perfect"
        90..100 => "A"
        80..90  => "B"
        70..80  => "C"
        60..70  => "D"
        _       => "F"
    }
}

fn http_status(code) {
    match code {
        200 => "OK"
        201 => "Created"
        400 => "Bad Request"
        401 => "Unauthorized"
        403 => "Forbidden"
        404 => "Not Found"
        500 => "Internal Server Error"
        _   => "Unknown"
    }
}

fn describe(lang) {
    match lang {
        "Yolish" => "capability-aware OS language"
        "C"      => "systems language"
        "Python" => "scripting language"
        "Rust"   => "memory-safe systems language"
        _        => "unknown language"
    }
}

fn main() {
    y.println("--- grades ---")
    let scores = [100, 95, 85, 72, 61, 40]
    for s in scores {
        y.println(y.format("{0} => {1}", s, grade(s)))
    }

    y.println("--- HTTP status ---")
    let codes = [200, 201, 404, 500, 999]
    for c in codes {
        y.println(y.format("{0} : {1}", c, http_status(c)))
    }

    y.println("--- languages ---")
    let langs = ["Yolish", "C", "Python", "Go"]
    for l in langs {
        y.println(y.format("{0} -> {1}", l, describe(l)))
    }

    y.println("--- bool match ---")
    var flag = true
    match flag {
        true  => y.println("flag is on")
        false => y.println("flag is off")
    }
}
