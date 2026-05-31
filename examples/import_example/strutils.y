-- strutils.y
fn is_empty(s)  { return y.len(s) == 0 }
fn capitalize(s) {
    if y.len(s) == 0 { return s }
    return y.upper(y.substr(s, 0, 1)) + y.lower(y.substr(s, 1, y.len(s) - 1))
}
fn words(s)      { return y.split(s, " ") }
fn word_count(s) { return y.len(y.split(s, " ")) }
