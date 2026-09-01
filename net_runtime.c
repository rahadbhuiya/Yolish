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