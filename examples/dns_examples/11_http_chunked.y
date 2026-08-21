-- HTTP chunked transfer-encoding decoding (v2.33) -- y.http.get_print
-- now strips hex chunk-size lines and boundary CRLFs, printing just
-- the actual payload, for servers that respond with
-- "Transfer-Encoding: chunked" (Cloudflare-fronted sites commonly do).
--
-- Build: ./ys -c 11_http_chunked.y --target linux -o http_chunked
-- Run:   ./http_chunked

y.http.get_print("https://example.com/")
y.println("done")
