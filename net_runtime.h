/*
   net_runtime.h — public API for net_runtime.c
   -----------------------------------------------
   Declarations for the networking / TLS / HTTP / hashmap engine that
   used to live inline in eval.c. Included by eval.c (whose
   call_builtin() dispatch calls these) and by net_runtime.c itself.
*/
#ifndef YOLISH_NET_RUNTIME_H
#define YOLISH_NET_RUNTIME_H

#include "yolish.h"
#include <stdint.h>

/* shared last-error string, set by any ys_net_ or ys_tls_ failure and
   read by the y.net.last_error() builtin */
extern char g_net_err[256];
void ys_net_set_err(const char *msg);

/* y.net.* — TCP client/server sockets */
int64_t ys_net_connect(const char *host, int port);
int64_t ys_net_send(int64_t sock, const char *data, int len);
int64_t ys_net_recv(int64_t sock, char *buf, int maxlen);
void    ys_net_close(int64_t sock);
int64_t ys_net_listen(int port);
int64_t ys_net_accept(int64_t server_sock);
int     ys_net_set_timeout(int64_t sock, int ms);

/* y.net.udp_* — UDP datagram sockets */
int64_t ys_udp_socket(void);
int64_t ys_udp_bind(int port);
int64_t ys_udp_send(int64_t sock, const char *host, int port, const char *data, int len);
Val     ys_udp_recv(int64_t sock, int maxlen);

/* y.net.tls_* — TLS client sockets via OpenSSL (opt-in, YS_WITH_TLS).
   Declared unconditionally so eval.c's dispatch compiles either way;
   without YS_WITH_TLS these simply aren't defined in net_runtime.c and
   the dispatch code has its own #ifdef guards that skip calling them. */
#ifdef YS_WITH_TLS
int64_t ys_tls_connect(const char *host, int port);
int64_t ys_tls_send(int64_t handle, const char *data, int len);
int64_t ys_tls_recv(int64_t handle, char *buf, int maxlen);
void    ys_tls_close(int64_t handle);
#endif

/* y.http.* — HTTP client built on the above */
Val ys_http_request(const char *method, const char *url, const char *body, int body_len, const char *content_type);

/* Shared row-callback shape for both y.db.sqlite_query (gated,
   below) and y.db.pg_query (unconditional, further down): engine-
   agnostic on purpose, since both hand back one row at a time as
   plain C strings and neither knows anything about Val/maps —
   eval.c supplies the same kind of callback for both to build the
   actual Val-map result, reusing the map-building helpers that
   already live there. Declared here, outside any #ifdef, since
   PostgreSQL support needs it and has no opt-in flag to gate on. */
typedef void (*ys_db_row_cb)(void *user_data, int ncol, char **colvals, char **colnames);

/* y.db.sqlite_* — SQLite client via libsqlite3 (opt-in, YS_WITH_SQLITE).
   Same pattern as YS_WITH_TLS above: declared unconditionally so
   eval.c's dispatch compiles either way; without YS_WITH_SQLITE these
   simply aren't defined here and the dispatch code's own #ifdef
   guards skip calling them, returning a clear "not compiled in"
   error instead. The returned handle is the raw sqlite3* pointer
   value reinterpreted as an int64_t -- there's no separate handle
   table (unlike y.net.tls_*, which needs one because it tracks
   {fd,ctx,ssl} per connection; a sqlite3* is already everything
   sqlite3_exec/sqlite3_close need). */
#ifdef YS_WITH_SQLITE
int64_t ys_db_sqlite_open(const char *path);
int     ys_db_sqlite_exec(int64_t handle, const char *sql);
void    ys_db_sqlite_close(int64_t handle);

/* y.db.sqlite_query — reads rows back, unlike sqlite_exec above.
   Deliberately kept at the raw-C-strings level here: this file has no
   knowledge of Yolish's Val/map types, so it just marshals each row's
   column names/values as plain char* and hands them to a
   caller-supplied callback. eval.c supplies that callback and does
   the actual Val-map construction, since that's where the map-
   building helpers (ys_map_init/ys_map_set) already live. Every value
   comes back as text regardless of its real SQLite column type
   (sqlite3_exec's legacy callback API doesn't expose real types --
   only prepare/step/column would -- deliberately not used here to

   keep this to one sqlite3_exec call instead of a four-function
   prepare/bind/step/finalize sequence). Returns the row count written
   (capped at max_rows -- hitting the cap is not treated as an error,
   there just aren't more slots to write into) or -1 on a real error. */
int ys_db_sqlite_query(int64_t handle, const char *sql, int max_rows, ys_db_row_cb cb, void *user_data);
#endif

/* y.db.pg_* — PostgreSQL client, wire protocol v3 implemented from
   scratch over the same raw ys_net_connect socket y.net.connect uses
   -- no libpq, no external dependency, always compiled in (unlike
   TLS/SQLite there's no external C library to opt into linking).
   Auth: trust (no password needed) and MD5 are supported (MD5 is
   implemented locally in net_runtime.c -- no OpenSSL dependency
   pulled in just for this). SCRAM-SHA-256 (the default auth method
   on PostgreSQL 14+ installs that haven't been reconfigured) is NOT
   supported yet -- a real limitation, not an oversight; SCRAM is a
   materially larger protocol (a real SASL exchange, HMAC, PBKDF2,
   optional channel binding) than a straight password hash, and
   worth its own dedicated pass rather than rushing into this one.
   Values in query results, like y.db.sqlite_query, come back as
   strings regardless of their real Postgres column type -- the
   simple query protocol's default text result format, not the
   binary format, is what's implemented here. */
int64_t ys_db_pg_connect(const char *host, int port, const char *user, const char *password, const char *dbname);
int     ys_db_pg_exec(int64_t handle, const char *sql);
int     ys_db_pg_query(int64_t handle, const char *sql, int max_rows, ys_db_row_cb cb, void *user_data);
void    ys_db_pg_close(int64_t handle);

/* y.map.* — hashmap engine (open addressing, linear probing) */
void ys_map_init(Val *m, int cap);
void ys_map_set(Val *m, Val k, Val v);
Val *ys_map_get(Val *m, Val k);
int  ys_map_delete(Val *m, Val k);
int  ys_map_count_live(Val *m);
int  ys_map_key_ok(Val *k);
int  ys_map_slot_empty(Val *k);
int  ys_map_slot_tomb(Val *k);

#endif