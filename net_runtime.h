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
typedef void (*ys_db_sqlite_row_cb)(void *user_data, int ncol, char **colvals, char **colnames);
int ys_db_sqlite_query(int64_t handle, const char *sql, int max_rows, ys_db_sqlite_row_cb cb, void *user_data);
#endif

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