/*
   net_runtime.c — networking / TLS / HTTP client / hashmap engine
   -------------------------------------------------------------------
   Extracted from eval.c (v2.19) once that file grew large enough that
   keeping this mostly-self-contained runtime layer separate became
   worth the split. This file has no dependency on the AST/Env/
   eval_node machinery — everything here operates on plain Val structs
   and C types, called into from eval.c's call_builtin() dispatch
   (which does need eval_node/Env, and stays in eval.c).

   Covers:
     - y.net.*      TCP client/server sockets (POSIX + Winsock)
     - y.net.tls_*  TLS client sockets via OpenSSL (opt-in, YS_WITH_TLS)
     - y.http.*     HTTP client built on the above
     - y.map.*      hashmap engine (open addressing, linear probing)

   See net_runtime.h for the declarations eval.c's dispatch code calls.
*/
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "yolish.h"
#include "net_runtime.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifndef _WIN32
#  include <unistd.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <fcntl.h>
#  include <errno.h>
#  include <poll.h>
#  include <sys/time.h>
#  define YS_SOCK_INVALID (-1)
#else
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
   /* NOTE: building ys.exe on Windows with this file requires linking
      against ws2_32 (e.g. `gcc ... -lws2_32` or add ws2_32.lib in MSVC). */
#  define YS_SOCK_INVALID INVALID_SOCKET
#endif

#ifdef YS_WITH_TLS
#  include <openssl/ssl.h>
#  include <openssl/err.h>
#  include <openssl/x509v3.h>
#endif

char g_net_err[256] = {0};

void ys_net_set_err(const char *msg){
    int i=0; while(msg[i]&&i<255){ g_net_err[i]=msg[i]; i++; } g_net_err[i]=0;
}

#ifdef _WIN32
static int g_wsa_ready=0;
static void ys_net_ensure_init(void){
    if(g_wsa_ready) return;
    WSADATA wsa;
    if(WSAStartup(MAKEWORD(2,2), &wsa)==0) g_wsa_ready=1;
    else ys_net_set_err("WSAStartup failed");
}
typedef SOCKET ys_sock_t;
#else
static void ys_net_ensure_init(void){ /* no-op on POSIX */ }
typedef int ys_sock_t;
#endif

#define YS_NET_CONNECT_TIMEOUT_MS 10000  /* 10s default connect timeout */

/* connect(2)/getaddrinfo — returns the OS socket handle as a plain
   integer (cast to int64_t), or -1 on failure with g_net_err set.
   Uses a non-blocking connect + poll() with a timeout rather than a
   bare blocking connect(), which could otherwise hang for the OS's
   own TCP timeout (often much longer than 10s) against an unreachable
   or silently-filtered address. */
int64_t ys_net_connect(const char *host, int port){
    ys_net_ensure_init();
    char portbuf[16];
    snprintf(portbuf,sizeof(portbuf),"%d",port);

    struct addrinfo hints, *res=NULL, *rp;
    memset(&hints,0,sizeof(hints));
    hints.ai_family=AF_UNSPEC;
    hints.ai_socktype=SOCK_STREAM;

    int gai=getaddrinfo(host, portbuf, &hints, &res);
    if(gai!=0 || !res){
        ys_net_set_err("could not resolve host");
        return -1;
    }

    ys_sock_t s = YS_SOCK_INVALID;
    for(rp=res; rp; rp=rp->ai_next){
        s = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if(s==YS_SOCK_INVALID) continue;

#ifdef _WIN32
        u_long nb=1; ioctlsocket(s, FIONBIO, &nb);
#else
        int fl=fcntl(s,F_GETFL,0); fcntl(s,F_SETFL,fl|O_NONBLOCK);
#endif
        int cr=connect(s, rp->ai_addr, (int)rp->ai_addrlen);
        int connected=0;
        if(cr==0){
            connected=1;
        } else {
#ifdef _WIN32
            int in_progress=(WSAGetLastError()==WSAEWOULDBLOCK);
#else
            int in_progress=(errno==EINPROGRESS);
#endif
            if(in_progress){
                struct pollfd pfd; pfd.fd=s; pfd.events=POLLOUT; pfd.revents=0;
#ifdef _WIN32
                int pr=WSAPoll(&pfd,1,YS_NET_CONNECT_TIMEOUT_MS);
#else
                int pr=poll(&pfd,1,YS_NET_CONNECT_TIMEOUT_MS);
#endif
                if(pr>0 && (pfd.revents&POLLOUT)){
                    int soerr=0;
#ifdef _WIN32
                    int sl=sizeof(soerr);
                    getsockopt(s,SOL_SOCKET,SO_ERROR,(char*)&soerr,&sl);
#else
                    socklen_t sl=sizeof(soerr);
                    getsockopt(s,SOL_SOCKET,SO_ERROR,&soerr,&sl);
#endif
                    if(soerr==0) connected=1;
                }
            }
        }

        /* restore blocking mode for the subsequent send/recv calls */
#ifdef _WIN32
        u_long bl=0; ioctlsocket(s, FIONBIO, &bl);
#else
        fcntl(s,F_SETFL,fl);
#endif

        if(connected) break;
#ifdef _WIN32
        closesocket(s);
#else
        close(s);
#endif
        s = YS_SOCK_INVALID;
    }
    freeaddrinfo(res);

    if(s==YS_SOCK_INVALID){
        ys_net_set_err("connect failed or timed out");
        return -1;
    }
    return (int64_t)s;
}

/* Loops until all of data is sent, an error occurs, or the connection
   closes. A single send() syscall can write fewer bytes than requested
   for large payloads ("short write") — treating that as "done" (the
   previous behavior) silently truncates data if the caller doesn't
   check and retry the remainder themselves, which almost nothing does
   in practice. Returns the total bytes sent (== len on full success),
   or -1 if nothing could be sent at all. */
int64_t ys_net_send(int64_t sock, const char *data, int len){
    ys_sock_t s=(ys_sock_t)sock;
    int64_t total=0;
    while(total<len){
#ifdef _WIN32
        int n=send(s,data+total,len-(int)total,0);
#else
        ssize_t n=send(s,data+total,(size_t)(len-total),0);
#endif
        if(n<0){
#ifndef _WIN32
            if(errno==EINTR) continue; /* interrupted, just retry */
#endif
            ys_net_set_err("send failed");
            return total>0?total:-1;
        }
        if(n==0) break; /* shouldn't normally happen for a TCP send, but don't spin if it does */
        total+=n;
    }
    return total;
}

/* returns bytes read into buf (caller-provided, size maxlen), or -1 on
   error; 0 means the peer closed the connection (EOF), not an error. */
int64_t ys_net_recv(int64_t sock, char *buf, int maxlen){
    ys_sock_t s=(ys_sock_t)sock;
#ifdef _WIN32
    int n=recv(s,buf,maxlen,0);
#else
    ssize_t n=recv(s,buf,(size_t)maxlen,0);
#endif
    if(n<0){
#ifdef _WIN32
        if(WSAGetLastError()==WSAETIMEDOUT) ys_net_set_err("recv timed out");
        else
#else
        if(errno==EAGAIN||errno==EWOULDBLOCK) ys_net_set_err("recv timed out");
        else
#endif
        ys_net_set_err("recv failed");
        return -1;
    }
    return (int64_t)n;
}

void ys_net_close(int64_t sock){
    ys_sock_t s=(ys_sock_t)sock;
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
}

/*
   TLS (y.net.tls_*) — real TLS via OpenSSL, opt-in at build time
   ------------------------------------------------------------------
   NOT a custom crypto implementation — this wraps OpenSSL's SSL_*
   API, an audited, industry-standard library, rather than hand-rolling
   a TLS handshake/cipher suite/certificate parser. A from-scratch TLS
   stack is a project on its own and not something to trust without
   serious, dedicated security review.

   Certificate verification IS enabled (SSL_VERIFY_PEER + hostname
   verification via SSL_set1_host, using the system's default CA
   trust store) — a connection to a server presenting an invalid,
   expired, or mismatched-hostname certificate fails, it does not
   silently proceed. SNI is sent (SSL_set_tlsext_host_name), required
   by most modern virtual-hosted HTTPS servers.

   Handles are a distinct id space from plain y.net.* socket fds
   (offset by TLS_HANDLE_BASE) — a TLS handle must only be used with
   y.net.tls_* functions, never with plain y.net.send/recv/close, and
   vice versa. Mixing them is a programming error, not something this
   layer tries to detect for you.
*/
#ifdef YS_WITH_TLS
#define YS_TLS_MAX 64
#define YS_TLS_HANDLE_BASE 1000000
typedef struct { SSL *ssl; int64_t fd; int used; } YsTlsConn;
static YsTlsConn ys_tls_table[YS_TLS_MAX];
static SSL_CTX *ys_tls_ctx=NULL;

static int ys_tls_ensure_ctx(void){
    if(ys_tls_ctx) return 1;
    ys_tls_ctx = SSL_CTX_new(TLS_client_method());
    if(!ys_tls_ctx) return 0;
    SSL_CTX_set_verify(ys_tls_ctx, SSL_VERIFY_PEER, NULL);
    SSL_CTX_set_min_proto_version(ys_tls_ctx, TLS1_2_VERSION);
    if(!SSL_CTX_set_default_verify_paths(ys_tls_ctx)){
        SSL_CTX_free(ys_tls_ctx); ys_tls_ctx=NULL; return 0;
    }
    return 1;
}

static YsTlsConn *ys_tls_lookup(int64_t handle){
    int64_t slot=handle-YS_TLS_HANDLE_BASE;
    if(slot<0||slot>=YS_TLS_MAX||!ys_tls_table[slot].used) return NULL;
    return &ys_tls_table[slot];
}

int64_t ys_tls_connect(const char *host, int port){
    if(!ys_tls_ensure_ctx()){ ys_net_set_err("TLS context init failed"); return -1; }

    int slot=-1;
    for(int i=0;i<YS_TLS_MAX;i++) if(!ys_tls_table[i].used){ slot=i; break; }
    if(slot<0){ ys_net_set_err("too many concurrent TLS connections"); return -1; }

    int64_t fd=ys_net_connect(host,port); /* plain TCP connect first, reusing the existing timeout-aware helper */
    if(fd<0) return -1; /* ys_net_connect already set the error */

    SSL *ssl=SSL_new(ys_tls_ctx);
    if(!ssl){ ys_net_close(fd); ys_net_set_err("SSL_new failed"); return -1; }

    SSL_set_fd(ssl,(int)fd);
    SSL_set_tlsext_host_name(ssl,host);  /* SNI */
    SSL_set1_host(ssl,host);              /* hostname check during verification */

    if(SSL_connect(ssl)!=1){
        ys_net_set_err("TLS handshake failed");
        SSL_free(ssl); ys_net_close(fd);
        return -1;
    }
    if(SSL_get_verify_result(ssl)!=X509_V_OK){
        ys_net_set_err("TLS certificate verification failed");
        SSL_free(ssl); ys_net_close(fd);
        return -1;
    }

    ys_tls_table[slot].ssl=ssl;
    ys_tls_table[slot].fd=fd;
    ys_tls_table[slot].used=1;
    return YS_TLS_HANDLE_BASE+slot;
}

int64_t ys_tls_send(int64_t handle, const char *data, int len){
    YsTlsConn *c=ys_tls_lookup(handle);
    if(!c){ ys_net_set_err("invalid TLS handle"); return -1; }
    int64_t total=0;
    while(total<len){
        int n=SSL_write(c->ssl,data+total,len-(int)total);
        if(n<=0){
            int err=SSL_get_error(c->ssl,n);
            if(err==SSL_ERROR_WANT_READ||err==SSL_ERROR_WANT_WRITE) continue;
            ys_net_set_err("TLS send failed");
            return total>0?total:-1;
        }
        total+=n;
    }
    return total;
}

int64_t ys_tls_recv(int64_t handle, char *buf, int maxlen){
    YsTlsConn *c=ys_tls_lookup(handle);
    if(!c){ ys_net_set_err("invalid TLS handle"); return -1; }
    int n=SSL_read(c->ssl,buf,maxlen);
    if(n<0){
        int err=SSL_get_error(c->ssl,n);
        if(err==SSL_ERROR_ZERO_RETURN) return 0;
        ys_net_set_err("TLS recv failed");
        return -1;
    }
    return n;
}

void ys_tls_close(int64_t handle){
    YsTlsConn *c=ys_tls_lookup(handle);
    if(!c) return;
    SSL_shutdown(c->ssl);
    SSL_free(c->ssl);
    ys_net_close(c->fd);
    c->used=0;
}
#endif /* YS_WITH_TLS */

/*
   HTTP client (y.http.get / y.http.post)
   ---------------------------------------
   A convenience layer on top of the y.net connect/send/recv functions
   above (both the plain and TLS variants) — builds a
   correct HTTP/1.1 request, sends it, reads the full response
   (relying on "Connection: close" so a simple read-until-EOF is
   sufficient framing for either Content-Length or chunked bodies),
   and parses out the status code, headers, and body (decoding
   chunked Transfer-Encoding if present). Returns a y.map with
   "status" (int), "body" (string), and "headers" (a y.map of
   lowercased header names to values), or nil on failure (check
   y.net.last_error()).

   Scope: no redirect following, no cookies, no compression
   (Accept-Encoding is not sent, so a compliant server sends the body
   uncompressed) — this is a basic client, not a full one.
*/
/* forward decls: the hashmap engine (ys_map_*) is defined further
   below, but the HTTP client (right here) needs it for building the
   headers/result maps */
void ys_map_init(Val *m, int cap);
void ys_map_set(Val *m, Val k, Val v);
Val *ys_map_get(Val *m, Val k);

static int ys_ieq(const char *a, const char *b){
    while(*a && *b){
        char ca=*a, cb=*b;
        if(ca>='A'&&ca<='Z') ca+=32;
        if(cb>='A'&&cb<='Z') cb+=32;
        if(ca!=cb) return 0;
        a++; b++;
    }
    return *a==0 && *b==0;
}

typedef struct { char scheme[8]; char host[256]; int port; char path[1024]; } YsUrl;

static int ys_url_parse(const char *url, YsUrl *out){
    out->scheme[0]=0; out->host[0]=0; out->port=0; out->path[0]=0;

    const char *p=url;
    const char *scheme_end=strstr(p,"://");
    if(scheme_end){
        int slen=(int)(scheme_end-p);
        if(slen<=0||slen>=(int)sizeof(out->scheme)) return 0;
        memcpy(out->scheme,p,slen); out->scheme[slen]=0;
        p=scheme_end+3;
    } else {
        strcpy(out->scheme,"http");
    }
    if(!ys_ieq(out->scheme,"http") && !ys_ieq(out->scheme,"https")) return 0;
    if(*p==0) return 0;

    const char *host_start=p;
    const char *path_start=strchr(p,'/');
    const char *host_end = path_start ? path_start : p+strlen(p);

    const char *colon=NULL;
    for(const char *q=host_start;q<host_end;q++) if(*q==':'){ colon=q; break; }

    int host_len = colon ? (int)(colon-host_start) : (int)(host_end-host_start);
    if(host_len<=0||host_len>=(int)sizeof(out->host)) return 0;
    memcpy(out->host,host_start,host_len); out->host[host_len]=0;

    if(colon && colon+1<host_end){
        out->port=atoi(colon+1);
        if(out->port<=0||out->port>65535) return 0;
    } else {
        out->port = ys_ieq(out->scheme,"https") ? 443 : 80;
    }

    if(path_start){
        int plen=(int)strlen(path_start);
        if(plen>=(int)sizeof(out->path)) plen=(int)sizeof(out->path)-1;
        memcpy(out->path,path_start,plen); out->path[plen]=0;
    } else {
        strcpy(out->path,"/");
    }
    return 1;
}

/* Reads from a socket (plain fd, or TLS handle when is_tls) until the
   connection closes, growing a plain malloc'd buffer as needed —
   NOT gc_alloc_str, since this is a C-side scratch buffer freed
   before this function's caller ever hands anything back to Yolish
   code. Returns NULL on a read error with nothing yet received. */
static char *ys_http_read_all(int64_t handle, int is_tls, int *out_len){
    size_t cap=8192, len=0;
    char *buf=malloc(cap);
    if(!buf){ *out_len=0; return NULL; }
    for(;;){
        if(len+4096>cap){
            cap*=2;
            char *nb=realloc(buf,cap);
            if(!nb) break;
            buf=nb;
        }
        int64_t n;
#ifdef YS_WITH_TLS
        if(is_tls) n=ys_tls_recv(handle, buf+len, 4096);
        else n=ys_net_recv(handle, buf+len, 4096);
#else
        (void)is_tls;
        n=ys_net_recv(handle, buf+len, 4096);
#endif
        if(n<=0) break;
        len+=(size_t)n;
    }
    *out_len=(int)len;
    if(len==0){ free(buf); return NULL; }
    return buf;
}

static Val ys_http_request_once(const char *method, const char *url, const char *body, int body_len, const char *content_type){
    YsUrl u;
    if(!ys_url_parse(url,&u)){ ys_net_set_err("invalid URL"); return make_nil(); }
    int use_tls = ys_ieq(u.scheme,"https");
#ifndef YS_WITH_TLS
    if(use_tls){ ys_net_set_err("https:// requires ys built with TLS support (see: make tls)"); return make_nil(); }
#endif

    int64_t handle;
#ifdef YS_WITH_TLS
    if(use_tls) handle=ys_tls_connect(u.host,u.port);
    else handle=ys_net_connect(u.host,u.port);
#else
    handle=ys_net_connect(u.host,u.port);
#endif
    if(handle<0) return make_nil(); /* error already set by connect */

    char reqbuf[2048];
    int n;
    if(body && body_len>0){
        n=snprintf(reqbuf,sizeof(reqbuf),
            "%s %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\nUser-Agent: Yolish\r\nContent-Type: %s\r\nContent-Length: %d\r\n\r\n",
            method,u.path,u.host,content_type?content_type:"application/octet-stream",body_len);
    } else {
        n=snprintf(reqbuf,sizeof(reqbuf),
            "%s %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\nUser-Agent: Yolish\r\n\r\n",
            method,u.path,u.host);
    }
    if(n<0||n>=(int)sizeof(reqbuf)){
        ys_net_set_err("request headers too large");
#ifdef YS_WITH_TLS
        if(use_tls) ys_tls_close(handle); else ys_net_close(handle);
#else
        ys_net_close(handle);
#endif
        return make_nil();
    }

#ifdef YS_WITH_TLS
    if(use_tls) ys_tls_send(handle,reqbuf,n); else ys_net_send(handle,reqbuf,n);
    if(body && body_len>0){ if(use_tls) ys_tls_send(handle,body,body_len); else ys_net_send(handle,body,body_len); }
#else
    ys_net_send(handle,reqbuf,n);
    if(body && body_len>0) ys_net_send(handle,body,body_len);
#endif

    int raw_len=0;
    char *raw=ys_http_read_all(handle, use_tls, &raw_len);
#ifdef YS_WITH_TLS
    if(use_tls) ys_tls_close(handle); else ys_net_close(handle);
#else
    ys_net_close(handle);
#endif

    if(!raw || raw_len==0){ if(raw) free(raw); ys_net_set_err("empty response"); return make_nil(); }

    char *sep=NULL;
    for(int i=0;i+3<raw_len;i++){
        if(raw[i]=='\r'&&raw[i+1]=='\n'&&raw[i+2]=='\r'&&raw[i+3]=='\n'){ sep=raw+i; break; }
    }
    char *headers_end = sep ? sep : raw+raw_len;
    char *body_start   = sep ? sep+4 : raw+raw_len;
    int body_len_actual=(int)((raw+raw_len)-body_start);

    int status=0;
    {
        char *line_end=memchr(raw,'\n',(size_t)(headers_end-raw));
        int line_len = line_end ? (int)(line_end-raw) : (int)(headers_end-raw);
        for(int i=0;i<line_len;i++){ if(raw[i]==' '){ status=atoi(raw+i+1); break; } }
    }

    Val hdrs=make_nil(); ys_map_init(&hdrs,8);
    {
        char *p2=memchr(raw,'\n',(size_t)(headers_end-raw));
        if(p2) p2++;
        while(p2 && p2<headers_end){
            char *line_end=memchr(p2,'\n',(size_t)(headers_end-p2));
            int line_len = line_end ? (int)(line_end-p2) : (int)(headers_end-p2);
            int ll=line_len; if(ll>0 && p2[ll-1]=='\r') ll--;
            if(ll>0){
                char *colon=memchr(p2,':',(size_t)ll);
                if(colon){
                    int klen=(int)(colon-p2);
                    char *vstart=colon+1;
                    int vlen=ll-(klen+1);
                    while(vlen>0 && *vstart==' '){ vstart++; vlen--; }
                    char kbuf[128]; int kk=klen<127?klen:127;
                    for(int j=0;j<kk;j++){ char c=p2[j]; if(c>='A'&&c<='Z') c+=32; kbuf[j]=c; }
                    kbuf[kk]=0;
                    char vbuf[512]; int vv=vlen<511?vlen:511;
                    if(vv>0) memcpy(vbuf,vstart,(size_t)vv);
                    vbuf[vv>0?vv:0]=0;
                    ys_map_set(&hdrs, make_str(kbuf), make_str(vbuf));
                }
            }
            p2 = line_end ? line_end+1 : NULL;
        }
    }

    char *final_body; int final_body_len;
    Val *te=ys_map_get(&hdrs, make_str("transfer-encoding"));
    if(te && te->type==YS_STR && strstr(te->sval,"chunked")){
        size_t cap=(size_t)(body_len_actual>0?body_len_actual:256), outlen=0;
        char *outbuf=malloc(cap);
        char *cp=body_start; char *cend=raw+raw_len;
        while(outbuf && cp<cend){
            char *line_end=NULL;
            for(char *q=cp;q+1<cend;q++){ if(q[0]=='\r'&&q[1]=='\n'){ line_end=q; break; } }
            if(!line_end) break;
            long chunk_size=strtol(cp,NULL,16);
            if(chunk_size<=0) break;
            char *data_start=line_end+2;
            if(data_start+chunk_size>cend) break;
            if(outlen+(size_t)chunk_size>cap){
                cap=(outlen+(size_t)chunk_size)*2;
                char *nb=realloc(outbuf,cap);
                if(!nb) break;
                outbuf=nb;
            }
            memcpy(outbuf+outlen,data_start,(size_t)chunk_size);
            outlen+=(size_t)chunk_size;
            cp=data_start+chunk_size+2;
        }
        final_body=outbuf; final_body_len=(int)outlen;
    } else {
        final_body=malloc((size_t)(body_len_actual>0?body_len_actual:1));
        if(final_body && body_len_actual>0) memcpy(final_body,body_start,(size_t)body_len_actual);
        final_body_len=body_len_actual;
    }
    free(raw);

    Val result=make_nil(); ys_map_init(&result,4);
    ys_map_set(&result, make_str("status"), make_int(status));
    if(final_body){
        char *gcbuf=gc_alloc_str(final_body_len+1);
        if(final_body_len>0) memcpy(gcbuf,final_body,(size_t)final_body_len);
        gcbuf[final_body_len]=0;
        free(final_body);
        Val bodyval=make_nil(); bodyval.type=YS_STR; bodyval.sval=gcbuf; bodyval.slen=final_body_len;
        ys_map_set(&result, make_str("body"), bodyval);
    } else {
        ys_map_set(&result, make_str("body"), make_str(""));
    }
    ys_map_set(&result, make_str("headers"), hdrs);
    return result;
}

/* Resolves a Location header against the URL it came from — Location
   is very commonly just a path ("/login"), occasionally a full
   absolute URL. This handles both; it does NOT handle relative paths
   like "../x" or "x" without a leading slash beyond treating them as
   host-root-relative, which covers the overwhelming majority of real
   redirects without a full RFC 3986 relative-resolution algorithm. */
static void ys_url_resolve(const char *base_url, const char *location, char *out, size_t outsz){
    if(strstr(location,"://")){ snprintf(out,(int)outsz,"%s",location); return; }
    YsUrl bu;
    if(!ys_url_parse(base_url,&bu)){ snprintf(out,(int)outsz,"%s",location); return; }
    int default_port = ys_ieq(bu.scheme,"https") ? 443 : 80;
    const char *path = (location[0]=='/') ? location+1 : location;
    if(bu.port==default_port) snprintf(out,(int)outsz,"%s://%s/%s",bu.scheme,bu.host,path);
    else snprintf(out,(int)outsz,"%s://%s:%d/%s",bu.scheme,bu.host,bu.port,path);
}

#define YS_HTTP_MAX_REDIRECTS 10

/* Follows 3xx redirects automatically (up to YS_HTTP_MAX_REDIRECTS),
   otherwise just forwards to ys_http_request_once. 303 always
   downgrades to GET; 301/302 downgrade a POST to GET too (matching
   curl/browser default behavior, for compatibility with servers that
   rely on it); 307/308 preserve the original method and body — this
   matches how virtually every mainstream HTTP client behaves by
   default. */
Val ys_http_request(const char *method, const char *url, const char *body, int body_len, const char *content_type){
    char cur_url[2048];
    snprintf(cur_url,sizeof(cur_url),"%s",url);
    const char *cur_method=method;
    const char *cur_body=body;
    int cur_body_len=body_len;

    for(int attempt=0; attempt<=YS_HTTP_MAX_REDIRECTS; attempt++){
        Val r=ys_http_request_once(cur_method,cur_url,cur_body,cur_body_len,content_type);
        if(r.type!=YS_MAP) return r; /* connection/parse failure, error already set */

        Val *status_v=ys_map_get(&r, make_str("status"));
        int status = status_v ? (int)val_int(*status_v) : 0;
        if(status<300||status>399) return r; /* not a redirect — final answer */

        Val *headers_v=ys_map_get(&r, make_str("headers"));
        Val *loc_v = headers_v ? ys_map_get(headers_v, make_str("location")) : NULL;
        if(!loc_v || loc_v->type!=YS_STR || loc_v->slen==0) return r; /* redirect with no Location to follow */

        if(attempt==YS_HTTP_MAX_REDIRECTS){ ys_net_set_err("too many redirects"); return make_nil(); }

        char next_url[2048];
        ys_url_resolve(cur_url, loc_v->sval, next_url, sizeof(next_url));

        if(status==303 || ((status==301||status==302) && ys_ieq(cur_method,"POST"))){
            cur_method="GET"; cur_body=NULL; cur_body_len=0;
        }
        snprintf(cur_url,sizeof(cur_url),"%s",next_url);
    }
    ys_net_set_err("too many redirects");
    return make_nil();
}

/* listen(port) -> a listening TCP socket bound to 0.0.0.0:port
   (backlog fixed at 128), or -1. */
int64_t ys_net_listen(int port){
    ys_net_ensure_init();
    ys_sock_t s = socket(AF_INET, SOCK_STREAM, 0);
    if(s==YS_SOCK_INVALID){ ys_net_set_err("socket failed"); return -1; }

    int yes=1;
#ifdef _WIN32
    setsockopt(s,SOL_SOCKET,SO_REUSEADDR,(const char*)&yes,sizeof(yes));
#else
    setsockopt(s,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
#endif

    struct sockaddr_in addr;
    memset(&addr,0,sizeof(addr));
    addr.sin_family=AF_INET;
    addr.sin_addr.s_addr=INADDR_ANY;
    addr.sin_port=htons((uint16_t)port);

    if(bind(s,(struct sockaddr*)&addr,sizeof(addr))!=0){
        ys_net_set_err("bind failed (port already in use?)");
#ifdef _WIN32
        closesocket(s);
#else
        close(s);
#endif
        return -1;
    }
    if(listen(s,128)!=0){
        ys_net_set_err("listen failed");
#ifdef _WIN32
        closesocket(s);
#else
        close(s);
#endif
        return -1;
    }
    return (int64_t)s;
}

/* accept(server_sock) -> a new connected client socket, or -1. Blocks
   until a connection arrives, unless a timeout was set via
   y.net.set_timeout on the listening socket — see that function. */
int64_t ys_net_accept(int64_t server_sock){
    ys_sock_t s=(ys_sock_t)server_sock;
    ys_sock_t c = accept(s, NULL, NULL);
    if(c==YS_SOCK_INVALID){
#ifdef _WIN32
        if(WSAGetLastError()==WSAETIMEDOUT) ys_net_set_err("accept timed out");
        else
#else
        if(errno==EAGAIN||errno==EWOULDBLOCK) ys_net_set_err("accept timed out");
        else
#endif
        ys_net_set_err("accept failed");
        return -1;
    }
    return (int64_t)c;
}

/* set_timeout(sock, ms) -> bool. Sets SO_RCVTIMEO, the standard
   portable way to time out either accept() (set on a listening
   socket — a client just never showing up) or recv() (set on a
   connected socket — the peer goes quiet). ms<=0 clears the timeout
   (blocks indefinitely again, the default). On timeout, the
   corresponding call returns -1 and y.net.last_error() reports
   "accept timed out" / "recv timed out" specifically, distinguishable
   from other failures. connect() already has its own built-in 10s
   timeout (see ys_net_connect) and isn't affected by this. */
int ys_net_set_timeout(int64_t sock, int ms){
    ys_sock_t s=(ys_sock_t)sock;
    if(ms<0) ms=0;
#ifdef _WIN32
    DWORD timeout=(DWORD)ms;
    return setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,(const char*)&timeout,sizeof(timeout))==0;
#else
    struct timeval tv;
    tv.tv_sec=ms/1000;
    tv.tv_usec=(ms%1000)*1000;
    return setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv))==0;
#endif
}

/*
   UDP (y.net.udp_*)
   -------------------
   Connectionless datagram sockets — no handshake, no ordering/
   delivery guarantees, each send/recv is one whole packet. Uses the
   same YS_SOCK_INVALID/ys_sock_t infrastructure as the TCP code above,
   and reuses ys_net_close/ys_net_set_timeout as-is (closing or
   setting a receive timeout on a UDP socket is identical to doing so
   on a TCP one at the OS level — no UDP-specific behavior needed
   there).
*/

/* udp_socket() -> an unbound UDP socket (OS assigns a local port on
   first send), for a "client" that only sends/receives replies. */
int64_t ys_udp_socket(void){
    ys_net_ensure_init();
    ys_sock_t s = socket(AF_INET, SOCK_DGRAM, 0);
    if(s==YS_SOCK_INVALID){ ys_net_set_err("udp socket failed"); return -1; }
    return (int64_t)s;
}

/* udp_bind(port) -> a UDP socket bound to 0.0.0.0:port, for a
   "server" that needs to receive on a known port. */
int64_t ys_udp_bind(int port){
    ys_net_ensure_init();
    ys_sock_t s = socket(AF_INET, SOCK_DGRAM, 0);
    if(s==YS_SOCK_INVALID){ ys_net_set_err("udp socket failed"); return -1; }

    int yes=1;
#ifdef _WIN32
    setsockopt(s,SOL_SOCKET,SO_REUSEADDR,(const char*)&yes,sizeof(yes));
#else
    setsockopt(s,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
#endif

    struct sockaddr_in addr;
    memset(&addr,0,sizeof(addr));
    addr.sin_family=AF_INET;
    addr.sin_addr.s_addr=INADDR_ANY;
    addr.sin_port=htons((uint16_t)port);

    if(bind(s,(struct sockaddr*)&addr,sizeof(addr))!=0){
        ys_net_set_err("udp bind failed (port already in use?)");
#ifdef _WIN32
        closesocket(s);
#else
        close(s);
#endif
        return -1;
    }
    return (int64_t)s;
}

/* udp_send(sock, host, port, data) -> bytes sent, or -1. host is
   resolved fresh on every call (getaddrinfo, same as TCP connect) —
   simpler than caching a resolved address, and sending is rare enough
   relative to the cost of a DNS lookup that this isn't worth
   optimizing yet. UDP sends are one full datagram in one syscall (no
   short-write concept the way a TCP stream has), so unlike
   ys_net_send there's no retry loop needed here. */
int64_t ys_udp_send(int64_t sock, const char *host, int port, const char *data, int len){
    ys_sock_t s=(ys_sock_t)sock;
    struct addrinfo hints, *res=NULL;
    memset(&hints,0,sizeof(hints));
    hints.ai_family=AF_INET;
    hints.ai_socktype=SOCK_DGRAM;
    char portbuf[16];
    snprintf(portbuf,sizeof(portbuf),"%d",port);
    if(getaddrinfo(host,portbuf,&hints,&res)!=0 || !res){ ys_net_set_err("could not resolve host"); return -1; }
#ifdef _WIN32
    int n=sendto(s,data,len,0,res->ai_addr,(int)res->ai_addrlen);
#else
    ssize_t n=sendto(s,data,(size_t)len,0,res->ai_addr,res->ai_addrlen);
#endif
    freeaddrinfo(res);
    if(n<0){ ys_net_set_err("udp send failed"); return -1; }
    return (int64_t)n;
}

/* udp_recv(sock, maxlen) -> a y.map {data, host, port} describing the
   received datagram and who sent it (unlike TCP, where the peer is
   already known from connect()/accept(), a UDP socket can receive
   from anyone, so the sender's address is essential — e.g. to reply
   to it — not just a nice-to-have), or nil on failure. Blocks until a
   datagram arrives unless y.net.set_timeout was called on sock. */
Val ys_udp_recv(int64_t sock, int maxlen){
    ys_sock_t s=(ys_sock_t)sock;
    if(maxlen<=0) maxlen=1024;
    char *buf=malloc((size_t)maxlen);
    if(!buf){ ys_net_set_err("out of memory"); return make_nil(); }

    struct sockaddr_in from;
    memset(&from,0,sizeof(from));
#ifdef _WIN32
    int fromlen=(int)sizeof(from);
    int n=recvfrom(s,buf,maxlen,0,(struct sockaddr*)&from,&fromlen);
#else
    socklen_t fromlen=sizeof(from);
    ssize_t n=recvfrom(s,buf,(size_t)maxlen,0,(struct sockaddr*)&from,&fromlen);
#endif
    if(n<0){
#ifdef _WIN32
        if(WSAGetLastError()==WSAETIMEDOUT) ys_net_set_err("recv timed out");
        else
#else
        if(errno==EAGAIN||errno==EWOULDBLOCK) ys_net_set_err("recv timed out");
        else
#endif
        ys_net_set_err("udp recv failed");
        free(buf);
        return make_nil();
    }

    char ipstr[64];
#ifdef _WIN32
    /* InetNtopA is the ws2tcpip.h equivalent of POSIX inet_ntop */
    InetNtopA(AF_INET,&from.sin_addr,ipstr,sizeof(ipstr));
#else
    inet_ntop(AF_INET,&from.sin_addr,ipstr,sizeof(ipstr));
#endif

    char *gcbuf=gc_alloc_str((int)n+1);
    if(n>0) memcpy(gcbuf,buf,(size_t)n);
    gcbuf[n]=0;
    free(buf);

    Val result=make_nil(); ys_map_init(&result,4);
    Val dataval=make_nil(); dataval.type=YS_STR; dataval.sval=gcbuf; dataval.slen=(int)n;
    ys_map_set(&result, make_str("data"), dataval);
    ys_map_set(&result, make_str("host"), make_str(ipstr));
    ys_map_set(&result, make_str("port"), make_int(ntohs(from.sin_port)));
    return result;
}

/*
   Hashmap (y.map.*)
   -----------------
   Open-addressing hash table with linear probing, storing parallel
   Val arrays (map_keys/map_vals) directly on the Val struct itself —
   same GC-tracked-buffer pattern as arr_data/field_vals. Keys must be
   YS_STR, YS_INT, or YS_BOOL (the hashable, comparable-by-value cases);
   anything else is rejected by the y.map.set/get/... builtins.

   DESIGN NOTE (documented in DOCS.md): unlike y.push/y.pop, which are
   explicitly immutable (always return a new array), maps are mutated
   IN PLACE through their shared map_keys/map_vals pointers — the usual
   hashmap contract, and necessary for O(1) amortized inserts. This is
   a deliberate, documented exception to the array convention.
*/
static uint64_t ys_map_hash(Val *k){
    if(k->type==YS_STR){
        uint64_t h=1469598103934665603ULL; /* FNV-1a */
        for(int i=0;i<k->slen;i++){ h^=(unsigned char)k->sval[i]; h*=1099511628211ULL; }
        return h;
    }
    uint64_t x=(uint64_t)val_int(*k); /* covers YS_INT and YS_BOOL */
    x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;
    x=(x^(x>>27))*0x94d049bb133111ebULL;
    x=x^(x>>31);
    return x;
}
static int ys_map_keys_equal(Val *a, Val *b){
    if(a->type==YS_STR && b->type==YS_STR){
        if(a->slen!=b->slen) return 0;
        return memcmp(a->sval,b->sval,(size_t)a->slen)==0;
    }
    if(a->type==YS_STR || b->type==YS_STR) return 0;
    return val_int(*a)==val_int(*b);
}
int ys_map_slot_empty(Val *k){ return k->type==YS_NIL && k->ival==0; }
int ys_map_slot_tomb(Val *k){  return k->type==YS_NIL && k->ival==1; }

void ys_map_init(Val *m, int cap){
    if(cap<8) cap=8;
    m->type=YS_MAP;
    m->map_keys=gc_alloc(cap);
    m->map_vals=gc_alloc(cap);
    m->map_len=0;
    m->map_cap=cap;
}

/* Returns the slot index for key k. *found=1 + index of the live entry
   if k is present; *found=0 + index of the first empty/tombstone slot
   along the probe sequence otherwise (i.e. where to insert). map_cap is
   always a power of two so `& (cap-1)` is a valid fast modulo. */
static int ys_map_find_slot(Val *m, Val *k, int *found){
    uint64_t h=ys_map_hash(k);
    int cap=m->map_cap;
    int idx=(int)(h & (uint64_t)(cap-1));
    int first_free=-1;
    for(int probe=0; probe<cap; probe++){
        int i=(idx+probe)&(cap-1);
        Val *slotkey=&m->map_keys[i];
        if(ys_map_slot_empty(slotkey)){ *found=0; return (first_free>=0)?first_free:i; }
        if(ys_map_slot_tomb(slotkey)){ if(first_free<0) first_free=i; continue; }
        if(ys_map_keys_equal(slotkey,k)){ *found=1; return i; }
    }
    *found=0;
    return (first_free>=0)?first_free:0;
}

static void ys_map_grow(Val *m){
    int old_cap=m->map_cap;
    Val *old_keys=m->map_keys;
    Val *old_vals=m->map_vals;
    Val new_m; ys_map_init(&new_m, old_cap*2);
    for(int i=0;i<old_cap;i++){
        if(!ys_map_slot_empty(&old_keys[i]) && !ys_map_slot_tomb(&old_keys[i])){
            int found=0;
            int slot=ys_map_find_slot(&new_m,&old_keys[i],&found);
            new_m.map_keys[slot]=old_keys[i];
            new_m.map_vals[slot]=old_vals[i];
            new_m.map_len++;
        }
    }
    m->map_keys=new_m.map_keys;
    m->map_vals=new_m.map_vals;
    m->map_cap=new_m.map_cap;
}

/* Mutates m in place — see the DESIGN NOTE above. Grows past 70% load. */
void ys_map_set(Val *m, Val k, Val v){
    if(m->type!=YS_MAP || !m->map_keys) ys_map_init(m,8);
    if((m->map_len+1)*10 >= m->map_cap*7) ys_map_grow(m);
    int found=0;
    int slot=ys_map_find_slot(m,&k,&found);
    m->map_keys[slot]=k;
    m->map_vals[slot]=v;
    if(!found) m->map_len++;
}
Val *ys_map_get(Val *m, Val k){
    if(m->type!=YS_MAP || !m->map_keys) return NULL;
    int found=0;
    int slot=ys_map_find_slot(m,&k,&found);
    return found?&m->map_vals[slot]:NULL;
}
int ys_map_delete(Val *m, Val k){
    if(m->type!=YS_MAP || !m->map_keys) return 0;
    int found=0;
    int slot=ys_map_find_slot(m,&k,&found);
    if(!found) return 0;
    m->map_keys[slot]=make_nil(); m->map_keys[slot].ival=1; /* tombstone */
    m->map_vals[slot]=make_nil();
    m->map_len--;
    return 1;
}
int ys_map_key_ok(Val *k){ return k->type==YS_STR||k->type==YS_INT||k->type==YS_BOOL; }

/* Recomputes live entry count by scanning, rather than trusting the
   cached map_len field. map_len is a plain scalar, so mutating it on
   one Val "copy" (e.g. inside a builtin that receives m by value) does
   NOT propagate to every other copy of the same map still holding the
   same underlying map_keys/map_vals buffer — only pointer fields share
   state automatically. Scanning is the only way to get a length that's
   correct no matter which copy — or which execution engine (tree-walk
   interpreter vs bytecode VM, which run builtins through independent
   variable-storage mechanisms) — is asking. */
int ys_map_count_live(Val *m){
    if(m->type!=YS_MAP || !m->map_keys) return 0;
    int n2=0;
    for(int i=0;i<m->map_cap;i++)
        if(!ys_map_slot_empty(&m->map_keys[i]) && !ys_map_slot_tomb(&m->map_keys[i])) n2++;
    return n2;
}
/* y.db.sqlite_* — thin SQLite client (opt-in, YS_WITH_SQLITE). Kept
   deliberately narrow: open a file (or ":memory:"), run one SQL
   statement with sqlite3_exec's callback=NULL (fire-and-forget —
   CREATE/INSERT/UPDATE/DELETE, no row results read back, same
   "no general string/array return type" ceiling y.net.recv's
   print-not-return native sibling already lives under), close, and
   (see ys_db_sqlite_query below) read query results back as an array
   of rows. */
#ifdef YS_WITH_SQLITE
/* sqlite3.h's dev-header may not be installed everywhere ys is built,
   but the tiny slice of its C API used below is stable/frozen (SQLite
   guarantees strict backward ABI compatibility across the 3.x series),
   so these are declared directly rather than requiring the full
   header. If sqlite3.h happens to already be on the include path this
   still matches it exactly -- same types, same signatures. */
typedef struct sqlite3 sqlite3;
#define SQLITE_OK 0
extern int sqlite3_open(const char *filename, sqlite3 **ppDb);
extern int sqlite3_exec(sqlite3 *db, const char *sql,
    int (*callback)(void*,int,char**,char**), void *arg, char **errmsg);
extern int sqlite3_close(sqlite3 *db);
extern const char *sqlite3_errmsg(sqlite3 *db);
extern void sqlite3_free(void *ptr);

int64_t ys_db_sqlite_open(const char *path){
    sqlite3 *db=NULL;
    int rc=sqlite3_open(path?path:"", &db);
    if(rc!=SQLITE_OK){
        ys_net_set_err(db?sqlite3_errmsg(db):"sqlite3_open failed");
        if(db) sqlite3_close(db); /* still allocates a handle on failure; must be closed to avoid leaking it */
        return -1;
    }
    return (int64_t)(intptr_t)db;
}

int ys_db_sqlite_exec(int64_t handle, const char *sql){
    if(handle==0) return -1;
    sqlite3 *db=(sqlite3*)(intptr_t)handle;
    char *errmsg=NULL;
    int rc=sqlite3_exec(db, sql?sql:"", NULL, NULL, &errmsg);
    if(rc!=SQLITE_OK){
        ys_net_set_err(errmsg?errmsg:"sqlite3_exec failed");
        if(errmsg) sqlite3_free(errmsg);
        return rc;
    }
    return 0;
}

void ys_db_sqlite_close(int64_t handle){
    if(handle==0) return;
    sqlite3_close((sqlite3*)(intptr_t)handle);
}

typedef struct {
    ys_db_sqlite_row_cb cb;
    void *user_data;
    int max_rows;
    int count;
    int hit_cap;
} ys_sqlite_query_ctx;

/* The actual function sqlite3_exec calls per row. Just forwards to
   the caller's callback (unpacked to plain C strings, exactly what
   sqlite3_exec itself hands us — no real typing available at this
   API level) and stops once max_rows is reached by returning
   non-zero, which makes sqlite3_exec abort further row callbacks. */
static int ys_sqlite_query_trampoline(void *ud, int ncol, char **vals, char **names){
    ys_sqlite_query_ctx *ctx=(ys_sqlite_query_ctx*)ud;
    if(ctx->count>=ctx->max_rows){ ctx->hit_cap=1; return 1; }
    ctx->cb(ctx->user_data, ncol, vals, names);
    ctx->count++;
    return 0;
}

int ys_db_sqlite_query(int64_t handle, const char *sql, int max_rows, ys_db_sqlite_row_cb cb, void *user_data){
    if(handle==0 || !cb) return -1;
    sqlite3 *db=(sqlite3*)(intptr_t)handle;
    ys_sqlite_query_ctx ctx; ctx.cb=cb; ctx.user_data=user_data; ctx.max_rows=max_rows; ctx.count=0; ctx.hit_cap=0;
    char *errmsg=NULL;
    int rc=sqlite3_exec(db, sql?sql:"", ys_sqlite_query_trampoline, &ctx, &errmsg);
    /* SQLITE_ABORT (rc!=SQLITE_OK) caused by hitting max_rows on
       purpose isn't a real error -- only report one if the abort
       came from somewhere else (a genuine SQL/runtime error). */
    if(rc!=SQLITE_OK && !ctx.hit_cap){
        ys_net_set_err(errmsg?errmsg:"sqlite3_exec failed");
        if(errmsg) sqlite3_free(errmsg);
        return -1;
    }
    if(errmsg) sqlite3_free(errmsg);
    return ctx.count;
}
#endif

/* ==========================================================================
   y.db.pg_* — PostgreSQL client, wire protocol v3, implemented from
   scratch. See net_runtime.h's comment on ys_db_pg_connect for the
   scope (trust/MD5 auth only, no SCRAM-SHA-256 yet; text-format
   results only). Uses ys_net_connect for the raw socket and plain
   send()/recv() for everything after that -- no external library.
   ========================================================================== */

/* ---- MD5 (RFC 1321), self-contained: PostgreSQL's MD5 auth needs
   md5(md5(password+username) as hex + salt) as hex, and there's no
   guarantee any crypto library is linked into this build (OpenSSL is
   only pulled in under YS_WITH_TLS) -- so it's implemented directly
   here rather than adding a hard dependency just for one hash. This
   is the standard reference algorithm, nothing PostgreSQL-specific
   about the implementation itself. */
typedef struct { uint32_t state[4]; uint64_t count; unsigned char buf[64]; } ys_md5_ctx;

static void ys_md5_transform(uint32_t state[4], const unsigned char block[64]){
    static const uint32_t K[64]={
        0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
        0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
        0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
        0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
        0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
        0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
        0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
        0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391};
    static const int S[64]={7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
        5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
        4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
        6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21};
    uint32_t M[16];
    for(int i=0;i<16;i++)
        M[i]=(uint32_t)block[i*4] | ((uint32_t)block[i*4+1]<<8) | ((uint32_t)block[i*4+2]<<16) | ((uint32_t)block[i*4+3]<<24);
    uint32_t A=state[0],B=state[1],C=state[2],D=state[3];
    for(int i=0;i<64;i++){
        uint32_t F; int g;
        if(i<16){ F=(B&C)|((~B)&D); g=i; }
        else if(i<32){ F=(D&B)|((~D)&C); g=(5*i+1)%16; }
        else if(i<48){ F=B^C^D; g=(3*i+5)%16; }
        else { F=C^(B|(~D)); g=(7*i)%16; }
        uint32_t tmp=D; D=C; C=B;
        uint32_t x=A+F+K[i]+M[g];
        uint32_t rot=(x<<S[i])|(x>>(32-S[i]));
        B=B+rot; A=tmp;
    }
    state[0]+=A; state[1]+=B; state[2]+=C; state[3]+=D;
}

static void ys_md5_init(ys_md5_ctx *ctx){
    ctx->state[0]=0x67452301; ctx->state[1]=0xefcdab89;
    ctx->state[2]=0x98badcfe; ctx->state[3]=0x10325476;
    ctx->count=0;
}
static void ys_md5_update(ys_md5_ctx *ctx, const unsigned char *data, size_t len){
    size_t have=(size_t)(ctx->count%64);
    ctx->count+=len;
    if(have){
        size_t take=64-have; if(take>len) take=len;
        memcpy(ctx->buf+have,data,take);
        have+=take; data+=take; len-=take;
        if(have==64){ ys_md5_transform(ctx->state,ctx->buf); have=0; }
    }
    while(len>=64){ ys_md5_transform(ctx->state,data); data+=64; len-=64; }
    if(len) memcpy(ctx->buf,data,len);
}
static void ys_md5_final(ys_md5_ctx *ctx, unsigned char out[16]){
    unsigned char pad[64]={0x80};
    uint64_t bits=ctx->count*8;
    size_t have=(size_t)(ctx->count%64);
    size_t padlen=(have<56)?(56-have):(120-have);
    ys_md5_update(ctx,pad,padlen);
    unsigned char lenbuf[8];
    for(int i=0;i<8;i++) lenbuf[i]=(unsigned char)(bits>>(8*i));
    ys_md5_update(ctx,lenbuf,8);
    for(int i=0;i<4;i++) for(int j=0;j<4;j++) out[i*4+j]=(unsigned char)(ctx->state[i]>>(8*j));
}
static void ys_md5_hex(const void *data, size_t len, char out[33]){
    ys_md5_ctx ctx; ys_md5_init(&ctx);
    ys_md5_update(&ctx,(const unsigned char*)data,len);
    unsigned char digest[16]; ys_md5_final(&ctx,digest);
    static const char hexd[]="0123456789abcdef";
    for(int i=0;i<16;i++){ out[i*2]=hexd[digest[i]>>4]; out[i*2+1]=hexd[digest[i]&0xf]; }
    out[32]=0;
}

/* ---- wire helpers: raw send/recv over the plain socket fd
   ys_net_connect already gives us. All-or-nothing (loops until the
   full length is sent/received or the connection dies) -- Postgres
   messages are usually small (well under one TCP segment) so this
   rarely loops more than once in practice, but relying on a single
   send()/recv() call moving the whole buffer isn't guaranteed by the
   socket API and isn't assumed here. */
static int ys_pg_send_all(int fd, const void *buf, size_t len){
    const unsigned char *p=(const unsigned char*)buf;
    size_t sent=0;
    while(sent<len){
        long n=send(fd,(const char*)p+sent,(int)(len-sent),0);
        if(n<=0) return -1;
        sent+=(size_t)n;
    }
    return 0;
}
static int ys_pg_recv_all(int fd, void *buf, size_t len){
    unsigned char *p=(unsigned char*)buf;
    size_t got=0;
    while(got<len){
        long n=recv(fd,(char*)p+got,(int)(len-got),0);
        if(n<=0) return -1;
        got+=(size_t)n;
    }
    return 0;
}
static void ys_pg_put_u32(unsigned char *p, uint32_t v){
    p[0]=(unsigned char)(v>>24); p[1]=(unsigned char)(v>>16);
    p[2]=(unsigned char)(v>>8);  p[3]=(unsigned char)v;
}
static uint32_t ys_pg_get_u32(const unsigned char *p){
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|(uint32_t)p[3];
}
static uint16_t ys_pg_get_u16(const unsigned char *p){
    return (uint16_t)(((uint16_t)p[0]<<8)|(uint16_t)p[1]);
}

/* Reads one backend message: 1-byte type + 4-byte length (length
   INCLUDES itself but not the type byte, per the protocol) +
   (length-4) bytes of payload. Caller owns *payload (malloc'd here,
   NULL if payload is empty) and must free() it. Returns the message
   type byte, or 0 on a read/connection error. */
static char ys_pg_read_msg(int fd, unsigned char **payload, uint32_t *paylen){
    unsigned char hdr[5];
    if(ys_pg_recv_all(fd,hdr,5)!=0) return 0;
    char type=(char)hdr[0];
    uint32_t len=ys_pg_get_u32(hdr+1);
    uint32_t plen = (len>=4) ? (len-4) : 0;
    *paylen=plen;
    if(plen==0){ *payload=NULL; return type; }
    unsigned char *buf=(unsigned char*)malloc(plen);
    if(!buf) return 0;
    if(ys_pg_recv_all(fd,buf,plen)!=0){ free(buf); return 0; }
    *payload=buf;
    return type;
}

/* Handle table: unlike SQLite's sqlite3* (one pointer is everything
   sqlite3_exec/close need), a Postgres connection here is just a
   plain socket fd -- and fds are small non-negative integers that
   would collide with "0 or positive means success" return-value
   conventions used elsewhere (e.g. -1 already means "connect
   failed"), so the raw fd doubles as the handle directly, same as
   how y.net.connect's own TCP handles already work. No separate
   table needed. */

int64_t ys_db_pg_connect(const char *host, int port, const char *user, const char *password, const char *dbname){
    int64_t s64=ys_net_connect(host,port);
    if(s64<0) return -1; /* ys_net_set_err already called by ys_net_connect */
    int fd=(int)s64;

    /* StartupMessage: int32 length, int32 protocol version (3.0),
       then a series of "key\0value\0" pairs, terminated by a lone \0.
       No leading type byte -- StartupMessage is the one message in
       the whole protocol that doesn't have one. */
    char msg[512]; size_t p=4; /* leave room for the length prefix */
    ys_pg_put_u32((unsigned char*)msg,0); /* placeholder, filled in below */
    uint32_t proto=0x00030000;
    msg[p++]=(char)(proto>>24); msg[p++]=(char)(proto>>16); msg[p++]=(char)(proto>>8); msg[p++]=(char)proto;
    const char *keys[2]={"user","database"};
    const char *vals[2]={user?user:"", dbname?dbname:(user?user:"")};
    for(int i=0;i<2;i++){
        size_t klen=strlen(keys[i]), vlen=strlen(vals[i]);
        if(p+klen+vlen+2>=sizeof(msg)) { close(fd); ys_net_set_err("pg_connect: user/database name too long"); return -1; }
        memcpy(msg+p,keys[i],klen); p+=klen; msg[p++]=0;
        memcpy(msg+p,vals[i],vlen); p+=vlen; msg[p++]=0;
    }
    msg[p++]=0; /* terminator */
    ys_pg_put_u32((unsigned char*)msg,(uint32_t)p);

    if(ys_pg_send_all(fd,msg,p)!=0){
        close(fd); ys_net_set_err("pg_connect: failed sending startup message"); return -1;
    }

    /* Auth phase: server sends Authentication* messages (type 'R')
       until it either accepts (auth_type 0) or the connection ends
       in an ErrorResponse ('E'). */
    for(;;){
        unsigned char *payload=NULL; uint32_t plen=0;
        char type=ys_pg_read_msg(fd,&payload,&plen);
        if(type==0){ close(fd); ys_net_set_err("pg_connect: connection closed during auth"); return -1; }
        if(type=='E'){
            ys_net_set_err(plen>0 ? (const char*)payload+1 : "pg_connect: server rejected connection");
            free(payload); close(fd); return -1;
        }
        if(type!='R'){ free(payload); continue; } /* ignore NoticeResponse etc. before auth completes */
        if(plen<4){ free(payload); close(fd); ys_net_set_err("pg_connect: malformed auth message"); return -1; }
        uint32_t auth_type=ys_pg_get_u32(payload);
        if(auth_type==0){ free(payload); break; } /* AuthenticationOk */
        if(auth_type==3){ /* AuthenticationCleartextPassword */
            size_t pwlen=strlen(password?password:"");
            unsigned char pmsg[256]; size_t mp=5;
            if(pwlen+1>=sizeof(pmsg)-5){ free(payload); close(fd); ys_net_set_err("pg_connect: password too long"); return -1; }
            memcpy(pmsg+mp,password?password:"",pwlen); mp+=pwlen; pmsg[mp++]=0;
            pmsg[0]='p'; ys_pg_put_u32(pmsg+1,(uint32_t)(mp-1));
            free(payload);
            if(ys_pg_send_all(fd,pmsg,mp)!=0){ close(fd); ys_net_set_err("pg_connect: failed sending password"); return -1; }
            continue;
        }
        if(auth_type==5){ /* AuthenticationMD5Password: 4-byte salt follows */
            if(plen<8){ free(payload); close(fd); ys_net_set_err("pg_connect: malformed MD5 auth message"); return -1; }
            unsigned char salt[4]; memcpy(salt,payload+4,4);
            free(payload);
            /* md5(md5(password+username) as hex-string bytes + salt) as hex, prefixed "md5" */
            char inner[512]; size_t ip=0;
            const char *pw=password?password:""; const char *un=user?user:"";
            size_t pwlen=strlen(pw), unlen=strlen(un);
            if(pwlen+unlen>=sizeof(inner)){ close(fd); ys_net_set_err("pg_connect: username/password too long for MD5 auth"); return -1; }
            memcpy(inner+ip,pw,pwlen); ip+=pwlen;
            memcpy(inner+ip,un,unlen); ip+=unlen;
            char innerhex[33]; ys_md5_hex(inner,ip,innerhex);
            char outerbuf[33+4]; size_t op=0;
            memcpy(outerbuf+op,innerhex,32); op+=32;
            memcpy(outerbuf+op,salt,4); op+=4;
            char outerhex[33]; ys_md5_hex(outerbuf,op,outerhex);
            char final[40]; snprintf(final,sizeof(final),"md5%s",outerhex);
            size_t flen=strlen(final);
            unsigned char pmsg[64]; size_t mp=5;
            memcpy(pmsg+mp,final,flen); mp+=flen; pmsg[mp++]=0;
            pmsg[0]='p'; ys_pg_put_u32(pmsg+1,(uint32_t)(mp-1));
            if(ys_pg_send_all(fd,pmsg,mp)!=0){ close(fd); ys_net_set_err("pg_connect: failed sending MD5 password"); return -1; }
            continue;
        }
        /* Anything else (SCRAM-SHA-256 = 10, GSS, SSPI, ...) isn't
           supported yet -- see net_runtime.h's comment on this
           function for why SCRAM specifically is out of scope here. */
        free(payload); close(fd);
        char errbuf[128];
        snprintf(errbuf,sizeof(errbuf),
            "pg_connect: server requires an unsupported auth method (type %u) -- only trust and MD5 are implemented; SCRAM-SHA-256 is not yet",
            auth_type);
        ys_net_set_err(errbuf);
        return -1;
    }

    /* Drain ParameterStatus/BackendKeyData/etc. until ReadyForQuery. */
    for(;;){
        unsigned char *payload=NULL; uint32_t plen=0;
        char type=ys_pg_read_msg(fd,&payload,&plen);
        if(type==0){ close(fd); ys_net_set_err("pg_connect: connection closed before ready"); return -1; }
        if(type=='E'){
            ys_net_set_err(plen>0 ? (const char*)payload+1 : "pg_connect: server error before ready");
            free(payload); close(fd); return -1;
        }
        free(payload);
        if(type=='Z') break; /* ReadyForQuery */
    }
    return (int64_t)fd;
}

/* Shared by pg_exec and pg_query: sends a Simple Query message and
   reads the response until ReadyForQuery. If cb is non-NULL, each
   DataRow is unpacked into plain C strings and handed to it (capped
   at max_rows, same "hitting the cap isn't an error" convention as
   y.db.sqlite_query); if cb is NULL, rows are read and discarded
   (that's what pg_exec uses this for). Returns the row count seen
   (0 for statements with no RowDescription, e.g. INSERT/UPDATE/DDL)
   or -1 on error. */
static int ys_pg_run_query(int64_t handle, const char *sql, int max_rows, ys_db_row_cb cb, void *user_data){
    if(handle<0) return -1;
    int fd=(int)handle;
    size_t sqllen=strlen(sql?sql:"");
    unsigned char *qmsg=(unsigned char*)malloc(5+sqllen+1);
    if(!qmsg) return -1;
    qmsg[0]='Q';
    ys_pg_put_u32(qmsg+1,(uint32_t)(4+sqllen+1));
    memcpy(qmsg+5,sql?sql:"",sqllen);
    qmsg[5+sqllen]=0;
    int sendrc=ys_pg_send_all(fd,qmsg,5+sqllen+1);
    free(qmsg);
    if(sendrc!=0){ ys_net_set_err("pg query: failed sending Query message"); return -1; }

    char *colnames[64]; int ncol=0;
    int rowcount=0; int had_error=0;

    for(;;){
        unsigned char *payload=NULL; uint32_t plen=0;
        char type=ys_pg_read_msg(fd,&payload,&plen);
        if(type==0){ ys_net_set_err("pg query: connection closed mid-response"); had_error=1; break; }

        if(type=='T'){ /* RowDescription */
            for(int i=0;i<ncol;i++) free(colnames[i]);
            ncol=0;
            if(plen>=2){
                int fieldcount=ys_pg_get_u16(payload);
                size_t off=2;
                for(int i=0;i<fieldcount && i<64 && off<plen;i++){
                    size_t start=off;
                    while(off<plen && payload[off]!=0) off++;
                    size_t namelen=off-start;
                    colnames[i]=(char*)malloc(namelen+1);
                    memcpy(colnames[i],payload+start,namelen);
                    colnames[i][namelen]=0;
                    ncol=i+1;
                    off++; /* skip the string's NUL */
                    off+=18; /* table OID(4)+col attnum(2)+type OID(4)+typlen(2)+typmod(4)+format(2) */
                }
            }
            free(payload);
        } else if(type=='D'){ /* DataRow */
            if(plen>=2 && cb && rowcount<max_rows){
                int fieldcount=ys_pg_get_u16(payload);
                char *vals[64]; int vfree[64];
                size_t off=2;
                int vc=0;
                for(int i=0;i<fieldcount && i<64 && off+4<=plen;i++){
                    int32_t flen=(int32_t)ys_pg_get_u32(payload+off); off+=4;
                    if(flen<0){ vals[i]=NULL; vfree[i]=0; }
                    else {
                        vals[i]=(char*)malloc((size_t)flen+1);
                        memcpy(vals[i],payload+off,(size_t)flen);
                        vals[i][flen]=0;
                        vfree[i]=1;
                        off+=(size_t)flen;
                    }
                    vc=i+1;
                }
                cb(user_data, vc<ncol?vc:ncol, vals, colnames);
                for(int i=0;i<vc;i++) if(vfree[i]) free(vals[i]);
                rowcount++;
            } else if(cb) {
                rowcount++; /* past the cap: still counted, just not built into a row */
            } else {
                rowcount++; /* pg_exec's cb==NULL case: just counting */
            }
            free(payload);
        } else if(type=='C'){ /* CommandComplete */
            free(payload);
        } else if(type=='E'){ /* ErrorResponse */
            ys_net_set_err(plen>0 ? (const char*)payload+1 : "pg query: server error");
            free(payload);
            had_error=1;
        } else if(type=='Z'){ /* ReadyForQuery -- response is complete */
            free(payload);
            break;
        } else {
            free(payload); /* NoticeResponse, ParameterStatus, etc. -- ignore */
        }
    }

    for(int i=0;i<ncol;i++) free(colnames[i]);
    return had_error ? -1 : rowcount;
}

int ys_db_pg_exec(int64_t handle, const char *sql){
    return ys_pg_run_query(handle, sql, 0, NULL, NULL);
}
int ys_db_pg_query(int64_t handle, const char *sql, int max_rows, ys_db_row_cb cb, void *user_data){
    return ys_pg_run_query(handle, sql, max_rows, cb, user_data);
}
void ys_db_pg_close(int64_t handle){
    if(handle<0) return;
    int fd=(int)handle;
    unsigned char term[5]; term[0]='X'; ys_pg_put_u32(term+1,4);
    ys_pg_send_all(fd,term,5); /* best-effort; ignore failure, we're closing anyway */
    close(fd);
}