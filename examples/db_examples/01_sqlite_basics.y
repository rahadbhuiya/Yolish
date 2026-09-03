-- 01_sqlite_basics.y  —  v2.35/v2.36: y.db.sqlite_*
--
-- Requires ys to have been built with -DYS_WITH_SQLITE -lsqlite3 (or
-- the versioned .so directly, e.g. /usr/lib/x86_64-linux-gnu/libsqlite3.so.0
-- if the unversioned dev symlink isn't installed). Without that flag
-- these calls still run -- they just return -1 / an empty array
-- cleanly, "not compiled in", rather than failing to build. Not swept
-- by the generic examples/*.y CI step for exactly that reason -- see
-- ci.yml's dedicated SQLite test step, which builds the flagged
-- binary this example actually needs.



let db = y.db.sqlite_open(":memory:")
y.println(db >= 0)

y.println(y.db.sqlite_exec(db, "CREATE TABLE people(id INTEGER PRIMARY KEY, name TEXT, age INTEGER)"))
y.println(y.db.sqlite_exec(db, "INSERT INTO people(name, age) VALUES ('sonali', 30)"))
y.println(y.db.sqlite_exec(db, "INSERT INTO people(name, age) VALUES ('rani', 25)"))
y.println(y.db.sqlite_exec(db, "INSERT INTO people(name, age) VALUES ('princess', 35)"))

let rows = y.db.sqlite_query(db, "SELECT name, age FROM people ORDER BY age")
y.println(y.len(rows))
for row in rows {
    y.println(y.map.get(row, "name"))
    y.println(y.map.get(row, "age"))
}

-- empty result set: no rows, not an error
let empty = y.db.sqlite_query(db, "SELECT * FROM people WHERE age > 1000")
y.println(y.len(empty))

-- NULL column comes back as Yolish's own nil, not the string "NULL"
y.db.sqlite_exec(db, "INSERT INTO people(name, age) VALUES ('dave', NULL)")
let withnull = y.db.sqlite_query(db, "SELECT age FROM people WHERE name = 'dave'")
y.println(y.map.get(withnull[0], "age"))

y.db.sqlite_close(db)
y.println("done")
