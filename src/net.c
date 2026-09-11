#include "peek.h"
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>

#define UBSZ 2048

int url_is(const char *s) {
    return !strncasecmp(s, "http://", 7) || !strncasecmp(s, "https://", 8);
}

static int uparse(const char *u, char *host, char *hh, int *port, char *path, int *tls) {
    int t = 0, pl = 7;
    if (!strncasecmp(u, "https://", 8)) t = 1, pl = 8;
    else if (strncasecmp(u, "http://", 7)) return 0;
    const char *a = u + pl, *e = a;
    while (*e && *e != '/') e++;
    const char *hs = a, *he = e, *cs = 0;
    int p = t ? 443 : 80;
    if (*hs == '[') {
        const char *rb = memchr(hs, ']', (size_t)(e - hs));
        if (!rb) return 0;
        cs = rb + 1 < e && rb[1] == ':' ? rb + 1 : 0;
    } else {
        cs = memchr(a, ':', (size_t)(e - a));
    }
    if (cs) {
        he = cs;
        p = 0;
        for (const char *q = cs + 1; q < e; q++) p = p * 10 + (*q & 15);
    }
    size_t hl = (size_t)(he - hs);
    if (!hl || hl >= 256) return 0;
    if (*hs == '[' && he[-1] == ']') {
        memcpy(host, hs + 1, hl - 2);
        host[hl - 2] = 0;
    } else {
        memcpy(host, hs, hl);
        host[hl] = 0;
    }
    memcpy(hh, hs, hl);
    hh[hl] = 0;
    if (p != (t ? 443 : 80)) snprintf(hh + hl, 8, ":%d", p);
    *port = p;
    *tls = t;
    if (!*e) strcpy(path, "/");
    else snprintf(path, UBSZ, "%s", e);
    return 1;
}

char *url_join(const char *b, const char *h) {
    size_t bl = strlen(b), hl = strlen(h);
    if (!hl) return sdup(b, bl);
    if (url_is(h)) return sdup(h, hl);
    char *r;
    if (h[0] == '/' && h[1] == '/') {
        const char *c = strchr(b, ':');
        size_t sl = c ? (size_t)(c - b) + 1 : 5;
        r = malloc(sl + hl + 1);
        if (!r) oom();
        memcpy(r, b, sl);
        memcpy(r + sl, h, hl + 1);
        return r;
    }
    const char *ps = 0;
    if (url_is(b)) {
        const char *a = strstr(b, "//");
        if (a) ps = strchr(a + 2, '/');
    }
    if (h[0] == '/') {
        size_t pl = ps ? (size_t)(ps - b) : bl;
        r = malloc(pl + hl + 1);
        if (!r) oom();
        memcpy(r, b, pl);
        memcpy(r + pl, h, hl + 1);
        return r;
    }
    const char *ct = ps ? strrchr(ps, '/') : 0;
    if (!ct) {
        r = malloc(bl + hl + 2);
        if (!r) oom();
        memcpy(r, b, bl);
        r[bl] = '/';
        memcpy(r + bl + 1, h, hl + 1);
        return r;
    }
    size_t kl = (size_t)(ct - b) + 1;
    r = malloc(kl + hl + 1);
    if (!r) oom();
    memcpy(r, b, kl);
    memcpy(r + kl, h, hl + 1);
    return r;
}

static int conn_tcp(const char *host, int port) {
    struct addrinfo hint = {0}, *r, *p;
    hint.ai_family = AF_UNSPEC;
    hint.ai_socktype = SOCK_STREAM;
    char ps[8];
    snprintf(ps, sizeof ps, "%d", port);
    if (getaddrinfo(host, ps, &hint, &r)) return -1;
    int fd = -1;
    for (p = r; p; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;
        fcntl(fd, F_SETFL, O_NONBLOCK);
        int c = connect(fd, p->ai_addr, p->ai_addrlen);
        if (c && errno != EINPROGRESS) { close(fd); fd = -1; continue; }
        if (c) {
            fd_set ws;
            FD_ZERO(&ws);
            FD_SET(fd, &ws);
            struct timeval tv = {10, 0};
            if (select(fd + 1, 0, &ws, 0, &tv) <= 0) { close(fd); fd = -1; continue; }
            int e = 0;
            socklen_t el = sizeof e;
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &e, &el) < 0 || e) { close(fd); fd = -1; continue; }
        }
        break;
    }
    freeaddrinfo(r);
    if (fd < 0) return -1;
    fcntl(fd, F_SETFL, 0);
    struct timeval tv = {10, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    return fd;
}

typedef struct {
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config cfg;
    mbedtls_x509_crt ca;
    int fd, tls;
} Tls;

static int mb_send(void *ctx, const unsigned char *b, size_t n) {
    ssize_t w = send(*(int *)ctx, b, n, 0);
    return w < 0 ? MBEDTLS_ERR_NET_SEND_FAILED : (int)w;
}

static int mb_recv(void *ctx, unsigned char *b, size_t n) {
    ssize_t r = recv(*(int *)ctx, b, n, 0);
    if (r > 0) return (int)r;
    if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
        return MBEDTLS_ERR_SSL_WANT_READ;
    return MBEDTLS_ERR_NET_RECV_FAILED;
}

static int tls_up(Tls *t, const char *host) {
    if (psa_crypto_init() != PSA_SUCCESS) {
        fprintf(stderr, "peek: tls: psa init failed\n");
        return 0;
    }
    mbedtls_ssl_init(&t->ssl);
    mbedtls_ssl_config_init(&t->cfg);
    mbedtls_x509_crt_init(&t->ca);
    if (mbedtls_x509_crt_parse_file(&t->ca, "/etc/ssl/certs/ca-certificates.crt") < 0 &&
        mbedtls_x509_crt_parse_file(&t->ca, "/etc/ssl/cert.pem") < 0) {
        fprintf(stderr, "peek: tls: no ca bundle\n");
        return 0;
    }
    if (mbedtls_ssl_config_defaults(&t->cfg, MBEDTLS_SSL_IS_CLIENT,
        MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT)) return 0;
    mbedtls_ssl_conf_session_tickets(&t->cfg, MBEDTLS_SSL_SESSION_TICKETS_DISABLED);
    mbedtls_ssl_conf_authmode(&t->cfg, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&t->cfg, &t->ca, 0);
    if (mbedtls_ssl_setup(&t->ssl, &t->cfg)) return 0;
    if (mbedtls_ssl_set_hostname(&t->ssl, host)) return 0;
    mbedtls_ssl_set_bio(&t->ssl, &t->fd, mb_send, mb_recv, 0);
    t->tls = 1;
    int r;
    while ((r = mbedtls_ssl_handshake(&t->ssl))) {
        if (r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE) {
            fprintf(stderr, "peek: tls: -0x%x\n", -r);
            return 0;
        }
    }
    return 1;
}

static int io_send(Tls *t, const char *b, size_t n) {
    if (t->tls)
        return mbedtls_ssl_write(&t->ssl, (const unsigned char *)b, n);
    ssize_t w = send(t->fd, b, n, 0);
    return w < 0 ? -1 : (int)w;
}

static int io_read(Tls *t, char *b, size_t n) {
    if (t->tls) {
        int r = mbedtls_ssl_read(&t->ssl, (unsigned char *)b, n);
        if (r > 0) return r;
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE ||
            r == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) return 0;
        return -1;
    }
    ssize_t r = recv(t->fd, b, n, 0);
    if (r > 0) return (int)r;
    if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return 0;
    return -1;
}

static size_t dechunk(char *s, size_t n) {
    char *r = s, *w = s, *e = s + n;
    for (;;) {
        char *nl = memchr(r, '\n', (size_t)(e - r));
        if (!nl) break;
        size_t sz = 0;
        for (char *q = r; q < nl && *q != '\r' && *q != ';'; q++)
            sz = sz << 4 | (size_t)((*q & 15) + (*q >> 6) * 9 & 15);
        r = nl + 1;
        if (!sz) break;
        if (r + sz > e) sz = (size_t)(e - r);
        memmove(w, r, sz);
        w += sz;
        r += sz;
        nl = memchr(r, '\n', (size_t)(e - r));
        if (!nl) break;
        r = nl + 1;
    }
    return (size_t)(w - s);
}

static char *fetch1(const char *host, const char *hh, int port, const char *path, int tls,
                    const char *method, const char *body, char *nurl, size_t nsz, int *code, size_t *len) {
    *code = 0;
    *nurl = 0;
    *len = 0;
    int fd = conn_tcp(host, port);
    if (fd < 0) { fprintf(stderr, "peek: connect %s:%d\n", host, port); return 0; }
    Tls T = {0};
    T.fd = fd;
    char *buf = 0, *res = 0;
    if (tls && !tls_up(&T, host)) goto out;
    char req[UBSZ + 512];
    int rl;
    if (body)
        rl = snprintf(req, sizeof req,
            "%s %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: peek/1.0\r\n"
            "Accept: text/html,*/*\r\n"
            "Accept-Encoding: identity\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n"
            "\r\n%s", method, path, hh, strlen(body), body);
    else
        rl = snprintf(req, sizeof req,
            "%s %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: peek/1.0\r\n"
            "Accept: text/html,*/*\r\n"
            "Accept-Encoding: identity\r\n"
            "Connection: close\r\n"
            "\r\n", method, path, hh);
    size_t off = 0;
    while (off < (size_t)rl) {
        int w = io_send(&T, req + off, (size_t)rl - off);
        if (w > 0) { off += (size_t)w; continue; }
        if (w < 0 && T.tls && (w == MBEDTLS_ERR_SSL_WANT_READ || w == MBEDTLS_ERR_SSL_WANT_WRITE)) continue;
        fprintf(stderr, "peek: send %s\n", host);
        goto out;
    }
    size_t cap = 1 << 16, n = 0;
    buf = malloc(cap);
    if (!buf) oom();
    time_t dl = time(0) + 30;
    for (;;) {
        if (n + 4096 > cap) {
            cap <<= 1;
            char *nb = realloc(buf, cap);
            if (!nb) oom();
            buf = nb;
        }
        int r = io_read(&T, buf + n, 4096);
        if (r > 0) { n += (size_t)r; continue; }
        if (!r && time(0) < dl) continue;
        break;
    }
    if (!n) { fprintf(stderr, "peek: empty response %s\n", host); goto out; }
    buf[n] = 0;
    char *hd = strstr(buf, "\r\n\r\n");
    if (!hd || memcmp(buf, "HTTP/", 5)) { fprintf(stderr, "peek: bad response\n"); goto out; }
    *code = atoi(buf + 9);
    long clen = -1;
    int chunked = 0, enc = 0;
    char *loc = 0;
    char *l = strstr(buf, "\r\n");
    if (l) l += 2;
    while (l && l < hd) {
        char *e = memchr(l, '\n', (size_t)(hd - l));
        if (!e) break;
        char *le = e[-1] == '\r' ? e - 1 : e;
        char *c = memchr(l, ':', (size_t)(le - l));
        if (c) {
            *c = 0;
            for (char *q = l; q < c; q++) *q = (char)tolower((unsigned char)*q);
            char *v = c + 1;
            while (v < le && *v == ' ') v++;
            if (!strcmp(l, "content-length")) clen = atol(v);
            else if (!strcmp(l, "transfer-encoding") &&
                     (size_t)(le - v) >= 7 && !strncasecmp(v, "chunked", 7)) chunked = 1;
            else if (!strcmp(l, "location") && v < le) { *le = 0; loc = v; }
            else if (!strcmp(l, "content-encoding") &&
                     (size_t)(le - v) >= 8 && strncasecmp(v, "identity", 8)) enc = 1;
        }
        l = e + 1;
    }
    if (loc && *code >= 300 && *code < 400) {
        snprintf(nurl, nsz, "%s", loc);
        goto out;
    }
    if (*code < 200 || *code >= 300) { fprintf(stderr, "peek: HTTP %d\n", *code); goto out; }
    if (enc) { fprintf(stderr, "peek: compressed body\n"); goto out; }
    char *bs = hd + 4;
    size_t bn = n - (size_t)(bs - buf);
    if (chunked) bn = dechunk(bs, bn);
    else if (clen >= 0 && (size_t)clen < bn) bn = (size_t)clen;
    memmove(buf, bs, bn);
    buf[bn] = 0;
    *len = bn;
    res = buf;
out:
    if (T.tls) {
        mbedtls_ssl_free(&T.ssl);
        mbedtls_ssl_config_free(&T.cfg);
        mbedtls_x509_crt_free(&T.ca);
    }
    close(fd);
    if (!res) free(buf);
    return res;
}

char *http_get(char *url, size_t *len) {
    int code;
    return http_req("GET", url, 0, len, &code);
}

char *http_req(const char *method, const char *url, const char *body, size_t *len, int *code_out) {
    char ub[UBSZ];
    snprintf(ub, sizeof ub, "%s", url);
    for (int hop = 0; hop < 6; hop++) {
        char host[300], hh[308], path[UBSZ], nu[UBSZ];
        int port, tls, code;
        if (!uparse(ub, host, hh, &port, path, &tls)) {
            fprintf(stderr, "peek: bad url %s\n", ub);
            return 0;
        }
        char *b = fetch1(host, hh, port, path, tls, method, body, nu, sizeof nu, &code, len);
        if (code_out) *code_out = code;
        if (b) {
            snprintf((char *)url, UBSZ, "%s", ub);
            return b;
        }
        if (code >= 300 && code < 400 && *nu) {
            char *j = url_join(ub, nu);
            snprintf(ub, sizeof ub, "%s", j);
            free(j);
            continue;
        }
        return 0;
    }
    fprintf(stderr, "peek: too many redirects\n");
    return 0;
}
