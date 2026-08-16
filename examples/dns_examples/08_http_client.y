-- Native HTTP client (v2.31): y.http.get_print/post_print. url (and
-- body/content_type for post_print) MUST be string literals -- parsed
-- entirely at compile time into scheme/host/port/path, same
-- literal-argument constraint as host/data elsewhere in this backend.
--
-- Build: ./ys -c 08_http_client.y --target linux -o http_client
-- Run:   ./http_client



y.http.get_print("https://example.com/")
y.println("---")
y.http.post_print("http://127.0.0.1:8090/submit",
                   "name=yolish&x=1", "application/x-www-form-urlencoded")
y.println("done")
