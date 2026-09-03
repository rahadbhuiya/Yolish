-- 02_postgres_basics.y  —  v2.37: y.db.pg_*
--
-- Needs a real, reachable PostgreSQL server with a user set up for
-- MD5 auth (see net_runtime.h's comment on ys_db_pg_connect for why
-- MD5, not the newer SCRAM-SHA-256 default -- not yet supported).
-- Not swept by the generic examples/*.y CI step, since it needs that
-- live server -- see ci.yml's dedicated PostgreSQL test step, which
-- installs and configures one before running this.
--
-- Connection details below match what that CI step sets up. Change
-- them to match your own server if running this by hand.




let h = y.db.pg_connect("127.0.0.1", 5432, "postgres", "testpass", "ysdb")
y.println(h >= 0)

y.db.pg_exec(h, "DROP TABLE IF EXISTS people")
y.println(y.db.pg_exec(h, "CREATE TABLE people(id SERIAL PRIMARY KEY, name TEXT, age INT)"))
y.println(y.db.pg_exec(h, "INSERT INTO people(name, age) VALUES ('tanuja', 30)"))
y.println(y.db.pg_exec(h, "INSERT INTO people(name, age) VALUES ('prithula', 25)"))
y.println(y.db.pg_exec(h, "INSERT INTO people(name, age) VALUES ('princess', 35)"))

let rows = y.db.pg_query(h, "SELECT name, age FROM people ORDER BY age")
y.println(y.len(rows))
for row in rows {
    y.println(y.map.get(row, "name"))
    y.println(y.map.get(row, "age"))
}

-- empty result set: no rows, not an error
let empty = y.db.pg_query(h, "SELECT * FROM people WHERE age > 1000")
y.println(y.len(empty))

-- NULL column comes back as Yolish's own nil
y.db.pg_exec(h, "INSERT INTO people(name, age) VALUES ('dave', NULL)")
let withnull = y.db.pg_query(h, "SELECT age FROM people WHERE name = 'dave'")
y.println(y.map.get(withnull[0], "age"))

-- a wrong password must fail cleanly, not hang or crash
let bad = y.db.pg_connect("127.0.0.1", 5432, "postgres", "wrongpassword", "ysdb")
y.println(bad)

y.db.pg_close(h)
y.println("done")
