#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WS ((1u << 9) | (1u << 10) | (1u << 11) | (1u << 12) | (1u << 13))
#define ISWS(c) ((c) == ' ' || ((unsigned)(c) < 32 && WS >> (c) & 1))

#define RESET "\x1b[0m"
#define CLEAR "\x1b[2J\x1b[H"

#define K1(a) (uint64_t)(unsigned char)(a)
#define K2(a, b) (K1(a) | K1(b) << 8)
#define K3(a, b, c) (K2(a, b) | K1(c) << 16)
#define K4(a, b, c, d) (K3(a, b, c) | K1(d) << 24)
#define K5(a, b, c, d, e) (K4(a, b, c, d) | K1(e) << 32)
#define K6(a, b, c, d, e, f) (K5(a, b, c, d, e) | K1(f) << 40)
#define K7(a, b, c, d, e, f, g) (K6(a, b, c, d, e, f) | K1(g) << 48)
#define K8(a, b, c, d, e, f, g, h) (K7(a, b, c, d, e, f, g) | K1(h) << 56)

#define K_TEXT K5('#', 't', 'e', 'x', 't')
#define K_ROOT K5('#', 'r', 'o', 'o', 't')
#define K_BUTTON K6('b', 'u', 't', 't', 'o', 'n')
#define K_BR K2('b', 'r')
#define K_DIV K3('d', 'i', 'v')
#define K_P K1('p')
#define K_HTML K4('h', 't', 'm', 'l')
#define K_BODY K4('b', 'o', 'd', 'y')
#define K_CLASS K5('c', 'l', 'a', 's', 's')
#define K_ID K2('i', 'd')
#define K_STYLE K5('s', 't', 'y', 'l', 'e')
#define K_SCRIPT K6('s', 'c', 'r', 'i', 'p', 't')
#define K_DISPLAY K7('d', 'i', 's', 'p', 'l', 'a', 'y')
#define K_TALIGN K8('t', 'e', 'x', 't', '-', 'a', 'l', 'i')
#define K_TTRANS K8('t', 'e', 'x', 't', '-', 't', 'r', 'a')

static uint64_t pk(const char *s) {
    uint64_t k;
    memcpy(&k, s, 8);
    uint64_t z = (k - 0x0101010101010101) & ~k & 0x8080808080808080;
    return k & (z ^ (z - 1));
}

static char *cut(char *s, char *e) {
    *e = 0;
    while (e > s && ISWS(e[-1])) *--e = 0;
    while (ISWS(*s)) s++;
    return s;
}

static int unescape(char *s, int len) {            
    char *w = s, *r = s, *e = s + len;
    while (r < e) {
        if (*r != '&' || e - r < 3) { *w++ = *r++; continue; }
        char *sc = memchr(r + 1, ';', e - r - 1);
        if (!sc || sc - r > 9) { *w++ = *r++; continue; }
        char buf[16] = {0};
        memcpy(buf, r + 1, sc - r - 1);
        char out = 0;
        if (*buf == '#') {
            int c = atoi(buf + 1);
            if (c > 0 && c < 128) out = c;
        } else {
            switch (pk(buf)) {
                case K3('a', 'm', 'p'): out = '&'; break;
                case K2('l', 't'): out = '<'; break;
                case K2('g', 't'): out = '>'; break;
                case K4('q', 'u', 'o', 't'): out = '"'; break;
                case K4('a', 'p', 'o', 's'): out = '\''; break;
                case K4('n', 'b', 's', 'p'): out = ' '; break;
            }
        }
        if (!out) { *w++ = *r++; continue; }
        *w++ = out;
        r = sc + 1;
    }
    return w - s;
}

typedef struct Attr { char *k, *v; } Attr;
typedef struct Node Node;
struct Node {
    char *tag; uint64_t tagpk;
    char *text; int tlen;
    Attr attrs[16]; int nattr;
    Node *child[32]; int nchild;
    Node *parent;
    struct { char *k, *v; uint64_t kk; int spec; } st[32]; int nst;
};

static Node POOL[256], *PP = POOL;
static char TAG_TEXT[16] = "#text", TAG_ROOT[16] = "#root";
static Node *node(char *tag) { Node *n = PP++; n->tag = tag; n->tagpk = pk(tag); return n; }

static void parse_attrs(char *s, Node *n) {
    while (*s) {
        while (ISWS(*s)) *s++ = 0;
        if (!*s) break;
        char *k = s, *v = "";
        while (*s && !ISWS(*s) && *s != '=') s++;
        char *ke = s;
        if (*s == '=') {
            *s++ = 0;
            char q = 0;
            if (*s == '"' || *s == '\'') q = *s++;
            v = s;
            while (*s && *s != q && (q || !ISWS(*s))) s++;
            if (*s) *s++ = 0;
        } else if (*s) *s++ = 0;
        *ke = 0;
        if (n->nattr < 16) {
            unescape(v, strlen(v));                
            n->attrs[n->nattr].k = k;
            n->attrs[n->nattr].v = v;
            n->nattr++;
        }
    }
}

static int isvoid(uint64_t k) {
    switch (k) {
        case K2('b', 'r'): case K2('h', 'r'): case K3('i', 'm', 'g'):
        case K4('m', 'e', 't', 'a'): case K4('l', 'i', 'n', 'k'):
        case K5('i', 'n', 'p', 'u', 't'): return 1;
    }
    return 0;
}

static void builtin(Node *n) {                    
    switch (n->tagpk) {
        case K2('h', '1'): case K2('h', '2'): case K2('h', '3'):
        case K2('h', '4'): case K2('h', '5'): case K2('h', '6'):
            n->st[0].k = "font-weight";
            n->st[0].kk = K8('f', 'o', 'n', 't', '-', 'w', 'e', 'i');
            n->st[0].v = "bold";
            n->st[0].spec = -1;
            n->nst = 1;
            break;
    }
}

static Node *parse_html(char *src) {
    Node *stk[64] = { node(TAG_ROOT) };
    int top = 0;
    char *p = src;
    while (*p) {
        if (*p != '<') {
            char *e = strchr(p, '<');
            if (!e) e = p + strlen(p);
            char sv = *e;
            *e = 0;
            char *t = cut(p, e);
            if (*t) {
                Node *n = node(TAG_TEXT);
                n->text = t;
                n->tlen = unescape(t, e - t);
                n->parent = stk[top];
                stk[top]->child[stk[top]->nchild++] = n;
            }
            *e = sv;
            p = e;
        } else {
            char *e = strchr(p, '>');
            if (!e) break;
            char *t = cut(p + 1, e);
            p = e + 1;
            if (!strncmp(t, "!--", 3)) {
                char *c = strstr(p, "-->");
                p = c ? c + 3 : p + strlen(p);
            } else if (*t == '!') {
            } else if (*t == '/') {
                while (top && stk[top]->tagpk != pk(t + 1)) top--;
                if (top) top--;
            } else if (*t) {
                int sc = t[strlen(t) - 1] == '/';
                if (sc) t[strlen(t) - 1] = 0;
                char *sp = strchr(t, ' ');
                Node *n = node(sp ? cut(t, sp) : t);
                if (sp) parse_attrs(sp + 1, n);
                builtin(n);
                n->parent = stk[top];
                stk[top]->child[stk[top]->nchild++] = n;
                if (!sc && !isvoid(n->tagpk) && top < 63) stk[++top] = n;
            }
        }
    }
    return stk[0];
}

typedef struct { char *k, *v; uint64_t kk; } Decl;
typedef struct { const char *ps[8]; uint8_t sep[9], np; Decl d[16]; int nd; } Rule;
static Rule R[64]; static int NR;

static void split_decls(char *s, char *e, Rule *r) {
    while (s < e && r->nd < 16) {
        char *semi = memchr(s, ';', e - s);
        char *de = semi ? semi : e;
        char *c = memchr(s, ':', de - s);
        if (c) {
            r->d[r->nd].k = cut(s, c);
            r->d[r->nd].v = cut(c + 1, de);
            r->d[r->nd].kk = pk(r->d[r->nd].k);     
            r->nd++;
        }
        s = de + 1;
    }
}

static void presplit(Rule *r, char *sel) {         
    char *p = sel;
    r->np = 0;
    r->sep[0] = ' ';
    while (*p && r->np < 8) {
        if (*p == '>' || *p == '+' || *p == '~') {
            r->sep[r->np] = *p;
            *p++ = 0;
            continue;
        }
        while (ISWS(*p)) *p++ = 0;
        if (!*p || *p == '>' || *p == '+' || *p == '~') continue;  
        r->ps[r->np++] = p;
        while (*p && !ISWS(*p) && *p != '>' && *p != '+' && *p != '~') p++;
        r->sep[r->np] = ' ';
    }
}

static void parse_css(char *css) {
    char *w = css, *p = css;                   
    while (*p) {
        if (p[0] == '/' && p[1] == '*') {
            char *e = strstr(p + 2, "*/");
            p = e ? e + 2 : p + strlen(p);
            continue;
        }
        *w++ = *p++;
    }
    *w = 0;
    char *pos = css;
    while (*pos && NR < 64) {
        char *b = strchr(pos, '{');
        if (!b) break;
        char *e = strchr(b, '}');
        if (!e) break;
        char *s0 = pos;
        for (char *q = pos; q < b; q++) if (*q == '}') s0 = q + 1;
        Rule tmp = {0};
        split_decls(b + 1, e, &tmp);
        for (char *q = s0; q < b && NR < 64;) {   
            char *c = memchr(q, ',', b - q);
            if (!c) c = b;
            Rule *r = R + NR++;
            *r = tmp;
            char *sel = cut(q, c);
            if (!*sel) NR--;
            else presplit(r, sel);
            q = c + 1;
        }
        pos = e + 1;
    }
}

static int match_compound(const char *c, Node *n) {
    if (n->tag[0] == '#') return -1;
    int ids = 0, cls = 0, tags = 0, cl = 0;
    char mode = 't', cur[64];
    for (int k = 0;; k++) {
        char ch = c[k];
        if (ch == '.' || ch == '#' || ch == '[' || ch == ']' || ch == ':' || !ch) {
            cur[cl] = 0;
            if (cl) {
                uint64_t ck = pk(cur);
                if (mode == 't') {
                    if (ck != K1('*')) {              
                        if (n->tagpk != ck) return -1;
                        tags++;
                    }
                } else if (mode == '.') {
                    const char *v = 0;
                    for (int i = 0; i < n->nattr; i++)
                        if (pk(n->attrs[i].k) == K_CLASS) v = n->attrs[i].v;
                    if (!v) return -1;
                    char buf[256], pat[66];
                    snprintf(buf, sizeof buf, " %s ", v);
                    snprintf(pat, sizeof pat, " %s ", cur);
                    if (!strstr(buf, pat)) return -1;
                    cls++;
                } else if (mode == '#') {
                    const char *v = 0;
                    for (int i = 0; i < n->nattr; i++)
                        if (pk(n->attrs[i].k) == K_ID) v = n->attrs[i].v;
                    if (!v || pk(v) != ck) return -1;
                    ids++;
                } else if (mode == ':') {               
                    Node *pa = n->parent;
                    if (!pa) return -1;
                    int idx = 0, cnt = 0;
                    for (int i = 0; i < pa->nchild; i++) {
                        Node *sib = pa->child[i];
                        if (sib->tag[0] == '#') continue;
                        if (sib == n) idx = cnt;
                        cnt++;
                    }
                    if (ck == K8('f', 'i', 'r', 's', 't', '-', 'c', 'h')) {
                        if (idx) return -1;
                    } else if (ck == K8('l', 'a', 's', 't', '-', 'c', 'h', 'i')) {
                        if (idx != cnt - 1) return -1;
                    } else return -1;
                    cls++;
                } else {          
                    char *eq = strchr(cur, '='), *val = 0;
                    char op = 0;
                    if (eq) {
                        val = eq + 1;
                        if (eq > cur && (eq[-1] == '^' || eq[-1] == '$' || eq[-1] == '*'))
                            op = *--eq;
                        *eq = 0;
                        size_t wl = strlen(val);
                        if (wl >= 2 && (val[0] == '"' || val[0] == '\'') &&
                            val[wl - 1] == val[0]) {    
                            val[wl - 1] = 0;
                            val++;
                        }
                    }
                    const char *v = 0;
                    for (int i = 0; i < n->nattr; i++)
                        if (pk(n->attrs[i].k) == pk(cur)) v = n->attrs[i].v;
                    if (!v) return -1;
                    if (!op) {                          
                        if (val && pk(v) != pk(val)) return -1;
                    } else if (op == '*') {            
                        if (!strstr(v, val)) return -1;
                    } else {                          
                        size_t vl = strlen(v), pl = strlen(val);
                        if (pl > vl) return -1;
                        if (op == '^' && memcmp(v, val, pl)) return -1;
                        if (op == '$' && memcmp(v + vl - pl, val, pl)) return -1;
                    }
                    cls++;
                }
            }
            if (!ch) break;
            mode = ch == ']' ? 't' : ch;
            cl = 0;
        } else if (cl < 63) cur[cl++] = ch;
    }
    return (ids << 8) | (cls << 4) | tags;
}

static int match_selector(const Rule *r, Node *n) {
    if (!r->np) return -1;
    int spec = match_compound(r->ps[r->np - 1], n);
    if (spec < 0) return -1;
    Node *m = n;
    for (int k = r->np - 2; k >= 0; k--) {
        const char *part = r->ps[k];
        Node *hit = 0;
        int s = 0;
        switch (r->sep[k + 1]) {
            case '>':                                 
                if (!m->parent) return -1;
                s = match_compound(part, m->parent);
                if (s < 0) return -1;
                hit = m->parent;
                break;
            case '+': case '~': {                      
                Node *q = m->parent;
                if (!q) return -1;
                Node *el[32]; int ne = 0;
                for (int i = 0; i < q->nchild && ne < 32; i++)
                    if (q->child[i]->tag[0] != '#') el[ne++] = q->child[i];
                int mi = -1;
                for (int e = 0; e < ne; e++)
                    if (el[e] == m) { mi = e; break; }
                if (mi < 0) return -1;
                for (int e = mi - 1; e >= 0 && !hit; e--) {
                    s = match_compound(part, el[e]);
                    if (s >= 0) hit = el[e];
                    else if (r->sep[k + 1] == '+') return -1; 
                }
                if (!hit) return -1;
                break;
            }
            default: {                                
                Node *q = m->parent;
                while (q && (s = match_compound(part, q)) < 0) q = q->parent;
                if (!q) return -1;
                hit = q;
                break;
            }
        }
        spec += s;
        m = hit;
    }
    return spec;
}

static void apply_decls(Rule *r, Node *n, int spec) {
    for (int i = 0; i < r->nd; i++) {
        uint64_t k = r->d[i].kk;                  
        int j = 0;
        while (j < n->nst && n->st[j].kk != k) j++;
        if (j == n->nst && n->nst < 32) {
            n->st[j].k = r->d[i].k;
            n->st[j].v = r->d[i].v;
            n->st[j].kk = k;
            n->nst++;
        }
        if (j < n->nst && (!n->st[j].v || spec >= n->st[j].spec)) {
            n->st[j].v = r->d[i].v;
            n->st[j].spec = spec;
        }
    }
}

static int inherits(uint64_t k) {              
    switch (k) {
        case K5('c', 'o', 'l', 'o', 'r'):
        case K8('f', 'o', 'n', 't', '-', 'w', 'e', 'i'):
        case K8('f', 'o', 'n', 't', '-', 's', 't', 'y'):
        case K8('t', 'e', 'x', 't', '-', 't', 'r', 'a'): return 1;
    }
    return 0;
}

static void apply_styles(Node *n) {
    if (n->tag[0] != '#') {
        for (int r = 0; r < NR; r++) {
            int spec = match_selector(R + r, n);
            if (spec >= 0) apply_decls(R + r, n, spec);
        }
        for (int i = 0; i < n->nattr; i++)
            if (pk(n->attrs[i].k) == K_STYLE) {
                Rule t = {0};
                split_decls(n->attrs[i].v,
                            n->attrs[i].v + strlen(n->attrs[i].v), &t);
                apply_decls(&t, n, 1 << 16);
            }
    }
    for (int c = 0; c < n->nchild; c++) {
        Node *ch = n->child[c];
        if (ch->tag[0] == '#') continue;
        for (int i = 0; i < n->nst && ch->nst < 32; i++) {
            if (!inherits(n->st[i].kk)) continue;
            int j = 0;
            while (j < ch->nst && ch->st[j].kk != n->st[i].kk) j++;
            if (j == ch->nst) {                     
                ch->st[j] = n->st[i];
                ch->st[j].spec = -1;
                ch->nst++;
            }
        }
        apply_styles(ch);
    }
}

static const char *stget(Node *n, uint64_t k) {
    for (int i = 0; i < n->nst; i++)
        if (n->st[i].kk == k) return n->st[i].v;
    return 0;
}

static int rgb256(int r, int g, int b) {
    r = r < 0 ? 0 : r > 255 ? 255 : r;
    g = g < 0 ? 0 : g > 255 ? 255 : g;
    b = b < 0 ? 0 : b > 255 ? 255 : b;
    if (r == g && g == b) return r < 8 ? 16 : r > 248 ? 231 : 232 + ((r - 8) * 205 >> 11);
    return 16 + 36 * ((r * 1287 + 32896) >> 16)
         + 6 * ((g * 1287 + 32896) >> 16) + ((b * 1287 + 32896) >> 16);
}

static void hsl_rgb(int h, int s, int l, int *R, int *G, int *B) {
    h = (h % 360 + 360) % 360;                    
    s = s < 0 ? 0 : s > 100 ? 100 : s;
    l = l < 0 ? 0 : l > 100 ? 100 : l;
    int d = 2 * l - 100;
    if (d < 0) d = -d;
    int c = (100 - d) * s / 100, m2 = 2 * l - c;
    int t = h % 120;
    t = t < 60 ? 60 - t : t - 60;                  
    int x = c * (60 - t) / 60, r2, g2, b2;
    switch (h / 60) {
        case 0: r2 = 2 * c; g2 = 2 * x; b2 = 0; break;
        case 1: r2 = 2 * x; g2 = 2 * c; b2 = 0; break;
        case 2: r2 = 0; g2 = 2 * c; b2 = 2 * x; break;
        case 3: r2 = 0; g2 = 2 * x; b2 = 2 * c; break;
        case 4: r2 = 2 * x; g2 = 0; b2 = 2 * c; break;
        default: r2 = 2 * c; g2 = 0; b2 = 2 * x; break;
    }
    *R = ((r2 + m2) * 51 + 20) / 40;          
    *G = ((g2 + m2) * 51 + 20) / 40;
    *B = ((b2 + m2) * 51 + 20) / 40;
}

static char *p3(char *s, int *p) {          
    for (int i = 0; i < 3; i++) {
        while (*s && *s != '-' && (*s < '0' || *s > '9')) s++;
        if (!*s) return s;
        p[i] = (int)strtol(s, &s, 10);
    }
    return s;
}

static int ansi_color(const char *v) {
    while (*v == ' ') v++;
    if (isdigit((unsigned char)*v)) { int n = atoi(v); return n < 256 ? n : -1; }
    if (!memcmp(v, "rgb", 3) || !memcmp(v, "hsl", 3)) {
        int p[3], r, g, b;
        float a = 1;
        char *lp = (char *)memchr(v, '(', strlen(v));
        if (!lp) return -1;
        char *e3 = p3(lp + 1, p);                
        if (e3 == lp + 1) return -1;
        char *sl = strchr(e3, '/');             
        if (sl) a = atof(sl + 1);
        else if (v[3] == 'a') {                    
            char *cm = lp;
            for (int i = 0; i < 3 && cm; i++) cm = strchr(cm + 1, ',');
            if (cm) a = atof(cm + 1);
        }
        if (a < 0) a = 0;
        if (a > 1) a = 1;
        if (*v == 'h') hsl_rgb(p[0], p[1], p[2], &r, &g, &b);
        else { r = p[0]; g = p[1]; b = p[2]; }
        return rgb256(r * a + .5f, g * a + .5f, b * a + .5f);  
    }
    if (*v == '#') {
        size_t len = strlen(v);
        if (len != 4 && len != 7) return -1;
        unsigned long hx = strtoul(v + 1, 0, 16);
        unsigned r, g, b;
        if (len == 4) {
            r = hx >> 8 & 0xf; g = hx >> 4 & 0xf; b = hx & 0xf;
            r |= r << 4; g |= g << 4; b |= b << 4;
        } else {
            r = hx >> 16 & 0xff; g = hx >> 8 & 0xff; b = hx & 0xff;
        }
        return rgb256(r, g, b);
    }
    switch (pk(v)) {
        case K5('b', 'l', 'a', 'c', 'k'): return 0;
        case K3('r', 'e', 'd'): return 1;
        case K5('g', 'r', 'e', 'e', 'n'): return 2;
        case K6('y', 'e', 'l', 'l', 'o', 'w'): return 3;
        case K4('b', 'l', 'u', 'e'): return 4;
        case K7('m', 'a', 'g', 'e', 'n', 't', 'a'): return 5;
        case K4('c', 'y', 'a', 'n'): case K4('a', 'q', 'u', 'a'): return 6;
        case K5('w', 'h', 'i', 't', 'e'): return 7;
        case K4('l', 'i', 'm', 'e'): return 10;
        case K4('n', 'a', 'v', 'y'): return 18;
        case K4('t', 'e', 'a', 'l'): return 30;
        case K6('m', 'a', 'r', 'o', 'o', 'n'): return 88;
        case K6('p', 'u', 'r', 'p', 'l', 'e'): return 90;
        case K5('o', 'l', 'i', 'v', 'e'): return 100;
        case K7('f', 'u', 'c', 'h', 's', 'i', 'a'): return 201;
        case K4('g', 'r', 'a', 'y'): return 244;
        case K6('s', 'i', 'l', 'v', 'e', 'r'): return 250;
    }
    return -1;
}

static void emit_style(Node *n) {
    for (int i = 0; i < n->nst; i++) {
        const char *v = n->st[i].v;
        int c = ansi_color(v);
        switch (n->st[i].kk) {                   
            case K5('c', 'o', 'l', 'o', 'r'):
                if (c >= 0) printf("\x1b[38;5;%dm", c);
                break;
            case K8('b', 'a', 'c', 'k', 'g', 'r', 'o', 'u'):
                if (c >= 0) printf("\x1b[48;5;%dm", c);
                break;
            case K8('f', 'o', 'n', 't', '-', 'w', 'e', 'i'):
                if (!strcmp(v, "bold")) fputs("\x1b[1m", stdout);
                break;
            case K8('f', 'o', 'n', 't', '-', 's', 't', 'y'):
                if (!strcmp(v, "italic")) fputs("\x1b[3m", stdout);
                break;
            case K8('t', 'e', 'x', 't', '-', 'd', 'e', 'c'):
                if (!strcmp(v, "underline")) fputs("\x1b[4m", stdout);
                else if (!strcmp(v, "line-through")) fputs("\x1b[9m", stdout);
                break;
        }
    }
}

static void collect_text(Node *n, char *out, size_t cap) {
    if (n->tagpk == K_TEXT) {
        int room = (int)cap - (int)strlen(out) - 1;
        strncat(out, n->text, n->tlen < room ? n->tlen : room);
        return;
    }
    for (int i = 0; i < n->nchild; i++) collect_text(n->child[i], out, cap);
}

static void hline(int cx, const char *seq, int w) {
    printf("%*s%s+", cx, "", seq);
    for (int i = 0; i < w; i++) putchar('-');
    fputs("+" RESET "\n", stdout);
}

static void draw_button(Node *n) {
    char text[256] = "";
    collect_text(n, text, sizeof text);
    int pad = 1, fg = -1, bg = -1, flags = 0, wmin = 0;
    const char *trans = 0;
    for (int i = 0; i < n->nst; i++) {
        const char *v = n->st[i].v;
        switch (n->st[i].kk) {
            case K7('p', 'a', 'd', 'd', 'i', 'n', 'g'): pad = atoi(v); break;
            case K5('c', 'o', 'l', 'o', 'r'): fg = ansi_color(v); break;
            case K8('b', 'a', 'c', 'k', 'g', 'r', 'o', 'u'): bg = ansi_color(v); break;
            case K8('f', 'o', 'n', 't', '-', 'w', 'e', 'i'):
                if (!strcmp(v, "bold")) flags |= 1;
                break;
            case K8('f', 'o', 'n', 't', '-', 's', 't', 'y'):
                if (!strcmp(v, "italic")) flags |= 2;
                break;
            case K8('t', 'e', 'x', 't', '-', 'd', 'e', 'c'):
                if (!strcmp(v, "underline")) flags |= 4;
                break;
            case K_TALIGN:
                if (!strcmp(v, "center")) flags |= 8;
                break;
            case K5('w', 'i', 'd', 't', 'h'): wmin = atoi(v); break;
            case K6('b', 'o', 'r', 'd', 'e', 'r'):
                if (!strcmp(v, "none")) flags |= 16;
                break;
            case K_TTRANS: trans = v; break;
        }
    }
    if (trans) {
        int up = !strcmp(trans, "uppercase"), lo = !strcmp(trans, "lowercase");
        if (up || lo)
            for (char *q = text; *q; q++)
                *q = up ? toupper((unsigned char)*q) : tolower((unsigned char)*q);
    }
    char seq[64] = "";
    if (fg >= 0) sprintf(seq + strlen(seq), "\x1b[38;5;%dm", fg);
    if (bg >= 0) sprintf(seq + strlen(seq), "\x1b[48;5;%dm", bg);
    if (flags & 1) strcat(seq, "\x1b[1m");
    if (flags & 2) strcat(seq, "\x1b[3m");
    if (flags & 4) strcat(seq, "\x1b[4m");
    int tl = (int)strlen(text), w = tl + pad * 2;
    if (wmin > w) w = wmin;                       
    int cx = flags & 8 && w < 80 ? (80 - w) >> 1 : 0;
    if (flags & 16) {                          
        printf("%*s%s%s%s\n", cx, "", seq, text, RESET);
        return;
    }
    int left = (w - tl) >> 1, right = w - tl - left; 
    hline(cx, seq, w);
    printf("%*s%s|%*s%s%*s|%s\n", cx, "", seq, left, "", text, right, "", RESET);
    hline(cx, seq, w);
}

static int isblock(uint64_t k) {                  
    switch (k) {
        case K_DIV: case K_P: case K_HTML: case K_BODY:
        case K2('h', '1'): case K2('h', '2'): case K2('h', '3'):
        case K2('h', '4'): case K2('h', '5'): case K2('h', '6'):
        case K2('u', 'l'): case K2('o', 'l'): case K2('l', 'i'):
        case K6('h', 'e', 'a', 'd', 'e', 'r'):
        case K6('f', 'o', 'o', 't', 'e', 'r'):
        case K7('s', 'e', 'c', 't', 'i', 'o', 'n'):
        case K7('a', 'r', 't', 'i', 'c', 'l', 'e'):
        case K3('n', 'a', 'v'): case K5('a', 's', 'i', 'd', 'e'):
        case K4('m', 'a', 'i', 'n'):
        case K8('b', 'l', 'o', 'c', 'k', 'q', 'u', 'o'):
            return 1;
    }
    return 0;
}

static void render(Node *n) {
    if (n->tagpk == K_TEXT) {
        Node *p = n->parent;
        if (p && p->tagpk != K_BUTTON) {
            const char *tt = stget(p, K_TTRANS);
            const char *al = stget(p, K_TALIGN);
            int cx = al && !strcmp(al, "center") ? (80 - n->tlen) >> 1 : 0;
            if (cx > 0) printf("%*s", cx, "");
            emit_style(p);
            int up = tt && !strcmp(tt, "uppercase");
            int lo = tt && !strcmp(tt, "lowercase");
            for (int i = 0; i < n->tlen; i++) {
                char ch = n->text[i];
                if (up) ch = toupper((unsigned char)ch);
                else if (lo) ch = tolower((unsigned char)ch);
                putchar(ch);
            }
            fputs(RESET, stdout);
        }
        return;
    }
    const char *d = stget(n, K_DISPLAY);
    if (d && !strcmp(d, "none")) return;
    switch (n->tagpk) {
        case K_BUTTON: draw_button(n); return;
        case K_BR: putchar('\n'); return;
        case K_STYLE: case K_SCRIPT:
        case K5('t', 'i', 't', 'l', 'e'): case K4('h', 'e', 'a', 'd'):
            return;                        
        case K2('h', 'r'):
            puts("-------------------------------------------------------------");
            return;
        case K2('l', 'i'):
            fputs("  \x1b[36m•\x1b[0m ", stdout);  
            break;
    }
    for (int i = 0; i < n->nchild; i++) render(n->child[i]);
    if (isblock(n->tagpk)) putchar('\n');
}

static char BUF[1 << 16];

static Node *find_tag(Node *n, uint64_t k) {
    if (n->tagpk == k) return n;
    for (int i = 0; i < n->nchild; i++) {
        Node *r = find_tag(n->child[i], k);
        if (r) return r;
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 2) return fprintf(stderr, "usage: %s <file.html>\n", argv[0]), 1;
    FILE *f = fopen(argv[1], "rb");
    if (!f) return perror(argv[1]), 1;
    size_t len = fread(BUF, 1, sizeof BUF - 8, f);
    if (ferror(f)) return perror(argv[1]), 1;
    BUF[len] = 0;
    fclose(f);
    fputs(CLEAR, stdout);
    Node *dom = parse_html(BUF);
    static char NOCSS[1] = "";
    char *css = NOCSS;
    Node *st = find_tag(dom, K_STYLE);
    if (st && st->nchild && st->child[0]->tagpk == K_TEXT) {
        Node *t = st->child[0];
        css = t->text, t->text[t->tlen] = 0;
    }
    parse_css(css);
    apply_styles(dom);
    render(dom);
    return 0;
}
