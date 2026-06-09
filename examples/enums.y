-- enums.y  —  v2.2: Enum

enum Direction { North  South  East  West }

y.println(Direction.North)
y.println(Direction.East)

let dir = Direction.South
y.println(dir)

match dir {
    Direction.North => y.println("Heading North")
    Direction.South => y.println("Heading South")
    Direction.East  => y.println("Heading East")
    Direction.West  => y.println("Heading West")
}

-- Enum comparison
let a = Direction.East
let b = Direction.East
y.println(a == b)
y.println(a == Direction.West)


-- App state enum
enum Status { Loading  Ready  Error  Done }

let state = Status.Ready
match state {
    Status.Loading => y.println("Please wait...")
    Status.Ready   => y.println("System ready!")
    Status.Error   => y.println("Something went wrong")
    Status.Done    => y.println("All done")
}


-- HTTP methods enum
enum HttpMethod { GET  POST  PUT  DELETE }

let method = HttpMethod.POST
match method {
    HttpMethod.GET    => y.println("Fetching")
    HttpMethod.POST   => y.println("Creating")
    HttpMethod.PUT    => y.println("Updating")
    HttpMethod.DELETE => y.println("Deleting")
}


-- Weekday enum
enum Day { Mon  Tue  Wed  Thu  Fri  Sat  Sun }

let today = Day.Fri
let is_weekend = (today == Day.Sat || today == Day.Sun)
y.println(is_weekend)
