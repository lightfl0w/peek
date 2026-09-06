#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <quickjs.h>

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

#define K_CLASS K5('c', 'l', 'a', 's', 's')
#define K_ID K2('i', 'd')
#define K_STYLE K5('s', 't', 'y', 'l', 'e')
#define GROW(a, n, cap, type) do { \
    if ((n) >= (cap)) { \
        (cap) = (cap) ? (cap) << 1 : 8; \
        void *p_ = realloc((a), (size_t)(cap) * sizeof(type)); \
        if (!p_) { fputs("oom\n", stderr); exit(1); } \
        (a) = p_; \
    } \
} while (0)

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

typedef struct Node Node;
typedef struct Tag Tag;

typedef struct Prop Prop;
struct Prop { const char *name; uint8_t f; void (*emit)(const char *v, int btn); };
enum { P_INH = 1 };

static void e_fg(const char *, int), e_bg(const char *, int);
static void e_bold(const char *, int), e_italic(const char *, int);
static void e_deco(const char *, int), e_align(const char *, int);
static void e_trans(const char *, int), e_pad(const char *, int);
static void e_width(const char *, int), e_border(const char *, int);

static const Prop
    P_COLOR = {"color", P_INH, e_fg},
    P_BG = {"background-color", 0, e_bg},
    P_WEIGHT = {"font-weight", P_INH, e_bold},
    P_FS = {"font-style", P_INH, e_italic},
    P_DECO = {"text-decoration", 0, e_deco},
    P_ALIGN = {"text-align", 0, e_align},
    P_TRANS = {"text-transform", P_INH, e_trans},
    P_PAD = {"padding", 0, e_pad},
    P_WIDTH = {"width", 0, e_width},
    P_BORDER = {"border", 0, e_border},
    P_DISPLAY = {"display", 0, 0};

static const Prop *const PROPS[] = {
    &P_COLOR, &P_BG, &P_WEIGHT, &P_FS, &P_DECO, &P_ALIGN,
    &P_TRANS, &P_PAD, &P_WIDTH, &P_BORDER, &P_DISPLAY,
};

static const Prop *prop_find(const char *k) {
    for (size_t i = 0; i < sizeof PROPS / sizeof *PROPS; i++)
        if (!strcmp(PROPS[i]->name, k)) return PROPS[i];
    return 0;
}

typedef struct Attr { char *k, *v; } Attr;
typedef struct { const Prop *p; const char *v; int spec; } St;

struct Node {
    const Tag *def;
    char *tag; uint64_t tagpk;
    char *text; int tlen;
    Attr *attrs; int nattr, acap;
    Node **child; int nchild, ccap;
    Node *parent;
    St *st; int nst, scap;
};

#define CHUNK 128
typedef struct Chunk { struct Chunk *next; Node n[CHUNK]; } Chunk;
static Chunk FIRST, *CH;
static int CN;

static void push_child(Node *p, Node *c) {
    GROW(p->child, p->nchild, p->ccap, Node *);
    p->child[p->nchild++] = c;
}

static void st_push(Node *n, const Prop *p, const char *v, int spec) {
    GROW(n->st, n->nst, n->scap, St);
    St *s = n->st + n->nst++;
    s->p = p; s->v = v; s->spec = spec;
}

static void ua_bold(Node *n) { st_push(n, &P_WEIGHT, "bold", -1); }

enum { T_VOID = 1, T_BLOCK = 2, T_HIDDEN = 4, T_LIST = 8, T_TEXTN = 16 };

struct Tag { const char *name; uint8_t f; void (*draw)(Node *); void (*ua)(Node *); };

static void d_br(Node *), d_hr(Node *), d_button(Node *), ua_bold(Node *);

static char N_TEXT[16] = "#text", N_ROOT[16] = "#root";

static const Tag TAGS[] = {
    {.name = N_ROOT}, {.name = N_TEXT, .f = T_TEXTN},
    {.name = "html", .f = T_BLOCK}, {.name = "body", .f = T_BLOCK},
    {.name = "head", .f = T_HIDDEN}, {.name = "title", .f = T_HIDDEN},
    {.name = "style", .f = T_HIDDEN}, {.name = "script", .f = T_HIDDEN},
    {.name = "p", .f = T_BLOCK}, {.name = "div", .f = T_BLOCK}, {.name = "span"},
    {.name = "h1", .f = T_BLOCK, .ua = ua_bold}, {.name = "h2", .f = T_BLOCK, .ua = ua_bold},
    {.name = "h3", .f = T_BLOCK, .ua = ua_bold}, {.name = "h4", .f = T_BLOCK, .ua = ua_bold},
    {.name = "h5", .f = T_BLOCK, .ua = ua_bold}, {.name = "h6", .f = T_BLOCK, .ua = ua_bold},
    {.name = "ul", .f = T_BLOCK}, {.name = "ol", .f = T_BLOCK},
    {.name = "li", .f = T_BLOCK | T_LIST},
    {.name = "header", .f = T_BLOCK}, {.name = "footer", .f = T_BLOCK},
    {.name = "section", .f = T_BLOCK}, {.name = "article", .f = T_BLOCK},
    {.name = "nav", .f = T_BLOCK}, {.name = "aside", .f = T_BLOCK},
    {.name = "main", .f = T_BLOCK}, {.name = "blockquote", .f = T_BLOCK},
    {.name = "figure", .f = T_BLOCK}, {.name = "figcaption", .f = T_BLOCK},
    {.name = "br", .f = T_VOID, .draw = d_br},
    {.name = "hr", .f = T_VOID | T_BLOCK, .draw = d_hr},
    {.name = "button", .draw = d_button},
};

static const Tag TAG_ANY = {0};

static const Tag *tag_find(const char *s) {
    for (size_t i = 0; i < sizeof TAGS / sizeof *TAGS; i++)
        if (!strcmp(TAGS[i].name, s)) return TAGS + i;
    return &TAG_ANY;
}

static Node *node(char *tag) {
    if (!CH) CH = &FIRST;
    if (CN >= CHUNK) {
        Chunk *c = calloc(1, sizeof *c);
        if (!c) { fputs("oom\n", stderr); exit(1); }
        CH->next = c; CH = c; CN = 0;
    }
    Node *n = CH->n + CN++;
    n->tag = tag;
    n->tagpk = pk(tag);
    n->def = tag_find(tag);
    if (n->def->ua) n->def->ua(n);
    return n;
}

static struct { int pad, wmin, fg, bg, flags; } BTN;
static struct { int center, up, lo; } TXT;
static Node *FOC;

static void e_fg(const char *v, int btn) {
    int c = ansi_color(v);
    if (c < 0) return;
    if (btn) BTN.fg = c;
    else printf("\x1b[38;5;%dm", c);
}

static void e_bg(const char *v, int btn) {
    int c = ansi_color(v);
    if (c < 0) return;
    if (btn) BTN.bg = c;
    else printf("\x1b[48;5;%dm", c);
}

static void e_bold(const char *v, int btn) {
    if (strcmp(v, "bold")) return;
    if (btn) BTN.flags |= 1;
    else fputs("\x1b[1m", stdout);
}

static void e_italic(const char *v, int btn) {
    if (strcmp(v, "italic")) return;
    if (btn) BTN.flags |= 2;
    else fputs("\x1b[3m", stdout);
}

static void e_deco(const char *v, int btn) {
    if (!strcmp(v, "underline")) {
        if (btn) BTN.flags |= 4;
        else fputs("\x1b[4m", stdout);
    } else if (!strcmp(v, "line-through")) {
        if (btn) BTN.flags |= 32;
        else fputs("\x1b[9m", stdout);
    }
}

static void e_align(const char *v, int btn) {
    if (strcmp(v, "center")) return;
    if (btn) BTN.flags |= 8;
    else TXT.center = 1;
}

static void e_trans(const char *v, int btn) {
    if (!strcmp(v, "uppercase")) {
        if (btn) BTN.flags |= 64;
        else TXT.up = 1;
    } else if (!strcmp(v, "lowercase")) {
        if (btn) BTN.flags |= 128;
        else TXT.lo = 1;
    }
}

static void e_pad(const char *v, int btn) { if (btn) BTN.pad = atoi(v); }
static void e_width(const char *v, int btn) { if (btn) BTN.wmin = atoi(v); }
static void e_border(const char *v, int btn) {
    if (btn && !strcmp(v, "none")) BTN.flags |= 16;
}

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
        unescape(v, strlen(v));
        GROW(n->attrs, n->nattr, n->acap, Attr);
        n->attrs[n->nattr].k = k;
        n->attrs[n->nattr].v = v;
        n->nattr++;
    }
}

static Node *parse_html(char *src) {
    Node **stk = 0;
    int scap = 0, top = 0;
    GROW(stk, 1, scap, Node *);
    stk[top++] = node(N_ROOT);
    char *p = src;
    while (*p) {
        if (*p != '<') {
            char *e = strchr(p, '<');
            if (!e) e = p + strlen(p);
            char sv = *e;
            *e = 0;
            char *t = cut(p, e);
            if (*t) {
                Node *n = node(N_TEXT);
                n->text = t;
                n->tlen = unescape(t, (int)strlen(t));
                n->parent = stk[top - 1];
                push_child(stk[top - 1], n);
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
                while (top > 1 && stk[top - 1]->tagpk != pk(t + 1)) top--;
                if (top > 1) top--;
            } else if (*t) {
                int sc = t[strlen(t) - 1] == '/';
                if (sc) t[strlen(t) - 1] = 0;
                char *sp = strchr(t, ' ');
                Node *n = node(sp ? cut(t, sp) : t);
                if (sp) parse_attrs(sp + 1, n);
                n->parent = stk[top - 1];
                push_child(stk[top - 1], n);
                if (!sc && !(n->def->f & T_VOID)) {
                    GROW(stk, top + 1, scap, Node *);
                    stk[top++] = n;
                }
            }
        }
    }
    return stk[0];
}

typedef struct { const Prop *p; const char *v; } Decl;
typedef struct { const char *ps[8]; uint8_t sep[9], np; Decl d[16]; int nd; } Rule;
static Rule *R;
static int NR, RCAP;

static void split_decls(char *s, char *e, Rule *r) {
    while (s < e && r->nd < 16) {
        char *semi = memchr(s, ';', e - s);
        char *de = semi ? semi : e;
        char *c = memchr(s, ':', de - s);
        if (c) {
            const Prop *p = prop_find(cut(s, c));
            if (p) {
                r->d[r->nd].p = p;
                r->d[r->nd].v = cut(c + 1, de);
                r->nd++;
            }
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
    while (*pos) {
        char *b = strchr(pos, '{');
        if (!b) break;
        char *e = strchr(b, '}');
        if (!e) break;
        char *s0 = pos;
        for (char *q = pos; q < b; q++) if (*q == '}') s0 = q + 1;
        Rule tmp = {0};
        split_decls(b + 1, e, &tmp);
        for (char *q = s0; q < b;) {
            char *c = memchr(q, ',', b - q);
            if (!c) c = b;
            if (NR >= RCAP) {
                RCAP = RCAP ? RCAP << 1 : 64;
                Rule *rp = realloc(R, (size_t)RCAP * sizeof *R);
                if (!rp) { fputs("oom\n", stderr); exit(1); }
                R = rp;
            }
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
                int mi = -1;
                for (int i = 0; i < q->nchild; i++)
                    if (q->child[i] == m) { mi = i; break; }
                if (mi < 0) return -1;
                for (int i = mi - 1; i >= 0; i--) {
                    Node *sib = q->child[i];
                    if (sib->tag[0] == '#') continue;
                    s = match_compound(part, sib);
                    if (s >= 0) { hit = sib; break; }
                    if (r->sep[k + 1] == '+') return -1;
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
        const Prop *p = r->d[i].p;
        int j = 0;
        while (j < n->nst && n->st[j].p != p) j++;
        if (j == n->nst) st_push(n, p, r->d[i].v, spec);
        else if (!n->st[j].v || spec >= n->st[j].spec) {
            n->st[j].v = r->d[i].v;
            n->st[j].spec = spec;
        }
    }
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
        for (int i = 0; i < n->nst; i++) {
            const Prop *p = n->st[i].p;
            if (!(p->f & P_INH)) continue;
            int j = 0;
            while (j < ch->nst && ch->st[j].p != p) j++;
            if (j == ch->nst) st_push(ch, p, n->st[i].v, -1);
        }
        apply_styles(ch);
    }
}

static void collect_text(Node *n, char *out, size_t cap) {
    if (n->def->f & T_TEXTN) {
        int room = (int)cap - (int)strlen(out) - 1;
        strncat(out, n->text, n->tlen < room ? n->tlen : room);
        return;
    }
    for (int i = 0; i < n->nchild; i++) collect_text(n->child[i], out, cap);
}

static void emit_styles(Node *n, int btn) {
    for (int i = 0; i < n->nst; i++)
        if (n->st[i].p->emit) n->st[i].p->emit(n->st[i].v, btn);
}

static void hline(int cx, const char *seq, int w) {
    printf("%*s%s+", cx, "", seq);
    for (int i = 0; i < w; i++) putchar('-');
    fputs("+" RESET "\n", stdout);
}

static void d_br(Node *n) { (void)n; putchar('\n'); }

static void d_hr(Node *n) {
    (void)n;
    puts("-------------------------------------------------------------");
}

static void d_button(Node *n) {
    char text[256] = "";
    collect_text(n, text, sizeof text);
    memset(&BTN, 0, sizeof BTN);
    BTN.fg = BTN.bg = -1;
    BTN.pad = 1;
    emit_styles(n, 1);
    if (BTN.flags & 64)
        for (char *q = text; *q; q++) *q = toupper((unsigned char)*q);
    else if (BTN.flags & 128)
        for (char *q = text; *q; q++) *q = tolower((unsigned char)*q);
    char seq[64] = "";
    if (BTN.fg >= 0) sprintf(seq + strlen(seq), "\x1b[38;5;%dm", BTN.fg);
    if (BTN.bg >= 0) sprintf(seq + strlen(seq), "\x1b[48;5;%dm", BTN.bg);
    if (BTN.flags & 1) strcat(seq, "\x1b[1m");
    if (BTN.flags & 2) strcat(seq, "\x1b[3m");
    if (BTN.flags & 4) strcat(seq, "\x1b[4m");
    if (BTN.flags & 32) strcat(seq, "\x1b[9m");
    if (n == FOC) strcat(seq, "\x1b[7m");
    int tl = (int)strlen(text), w = tl + BTN.pad * 2;
    if (BTN.wmin > w) w = BTN.wmin;
    int cx = BTN.flags & 8 && w < 80 ? (80 - w) >> 1 : 0;
    if (BTN.flags & 16) {
        printf("%*s%s%s%s\n", cx, "", seq, text, RESET);
        return;
    }
    int left = (w - tl) >> 1, right = w - tl - left;
    hline(cx, seq, w);
    printf("%*s%s|%*s%s%*s|%s\n", cx, "", seq, left, "", text, right, "", RESET);
    hline(cx, seq, w);
}

static void render(Node *n) {
    const Tag *d = n->def;
    if (d->f & T_TEXTN) {
        Node *p = n->parent;
        if (p && !p->def->draw) {
            memset(&TXT, 0, sizeof TXT);
            for (int i = 0; i < p->nst; i++)
                if (p->st[i].p == &P_ALIGN || p->st[i].p == &P_TRANS)
                    p->st[i].p->emit(p->st[i].v, 0);
            int cx = TXT.center ? (80 - n->tlen) >> 1 : 0;
            if (cx > 0) printf("%*s", cx, "");
            emit_styles(p, 0);
            for (int i = 0; i < n->tlen; i++) {
                char ch = n->text[i];
                if (TXT.up) ch = toupper((unsigned char)ch);
                else if (TXT.lo) ch = tolower((unsigned char)ch);
                putchar(ch);
            }
            fputs(RESET, stdout);
        }
        return;
    }
    if (d->f & T_HIDDEN) return;
    for (int i = 0; i < n->nst; i++)
        if (n->st[i].p == &P_DISPLAY && !strcmp(n->st[i].v, "none")) return;
    if (d->draw) { d->draw(n); return; }
    if (d->f & T_LIST) fputs("  \x1b[36m•\x1b[0m ", stdout);
    for (int i = 0; i < n->nchild; i++) render(n->child[i]);
    if (d->f & T_BLOCK) putchar('\n');
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

static Node *DOM;
static JSRuntime *RT;
static JSContext *CTX;
static JSClassID CLS;
static JSValue ELPROTO;
static char **LOGS;
static char **ALERTS;
static int NLOG, LCAP, NAL, ACAP;
static Node **BTNS;
static int NBTN, FOCI;

static char *sdup(const char *s, size_t n) {
    char *p = malloc(n + 1 < 16 ? 16 : n + 1);
    if (!p) { fputs("oom\n", stderr); exit(1); }
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

static char *attr_get(Node *n, const char *k) {
    for (int i = 0; i < n->nattr; i++)
        if (!strcmp(n->attrs[i].k, k)) return n->attrs[i].v;
    return 0;
}

static void attr_set(Node *n, const char *k, const char *v) {
    for (int i = 0; i < n->nattr; i++)
        if (!strcmp(n->attrs[i].k, k)) {
            n->attrs[i].v = sdup(v, strlen(v));
            return;
        }
    GROW(n->attrs, n->nattr, n->acap, Attr);
    n->attrs[n->nattr].k = sdup(k, strlen(k));
    n->attrs[n->nattr].v = sdup(v, strlen(v));
    n->nattr++;
}

static const char *style_get(Node *n, const char *k, int *vl) {
    const char *s = attr_get(n, "style");
    while (s && *s) {
        const char *e = strchr(s, ';');
        if (!e) e = s + strlen(s);
        const char *c = memchr(s, ':', e - s);
        if (c) {
            const char *ks = s, *ke = c;
            while (ks < ke && ISWS(*ks)) ks++;
            while (ke > ks && ISWS(ke[-1])) ke--;
            int kl = (int)(ke - ks);
            if (kl == (int)strlen(k) && !memcmp(ks, k, kl)) {
                const char *vs = c + 1, *ve = e;
                while (vs < ve && ISWS(*vs)) vs++;
                while (ve > vs && ISWS(ve[-1])) ve--;
                *vl = (int)(ve - vs);
                return vs;
            }
        }
        if (!*e) break;
        s = e + 1;
    }
    return 0;
}

static void style_set(Node *n, const char *k, const char *v) {
    const char *old = attr_get(n, "style");
    size_t cap = (old ? strlen(old) * 2 + 16 : 0) + strlen(k) + strlen(v) + 8;
    char *buf = malloc(cap);
    if (!buf) { fputs("oom\n", stderr); exit(1); }
    int w = 0;
    const char *s = old;
    while (s && *s) {
        const char *e = strchr(s, ';');
        if (!e) e = s + strlen(s);
        const char *c = memchr(s, ':', e - s);
        if (c) {
            const char *ks = s, *ke = c;
            while (ks < ke && ISWS(*ks)) ks++;
            while (ke > ks && ISWS(ke[-1])) ke--;
            int kl = (int)(ke - ks);
            if (kl && (kl != (int)strlen(k) || memcmp(ks, k, kl))) {
                const char *vs = c + 1, *ve = e;
                while (vs < ve && ISWS(*vs)) vs++;
                while (ve > vs && ISWS(ve[-1])) ve--;
                if (w) buf[w++] = ' ';
                memcpy(buf + w, ks, kl);
                w += kl;
                w += sprintf(buf + w, ": ");
                memcpy(buf + w, vs, ve - vs);
                w += (int)(ve - vs);
                buf[w++] = ';';
            }
        }
        if (!*e) break;
        s = e + 1;
    }
    sprintf(buf + w, "%s%s: %s;", w ? " " : "", k, v);
    attr_set(n, "style", buf);
    free(buf);
}

static void tsize(Node *n, size_t *t) {
    if (n->def->f & T_TEXTN) { *t += n->tlen; return; }
    for (int i = 0; i < n->nchild; i++) tsize(n->child[i], t);
}

static void tfill(Node *n, char *b, size_t *w) {
    if (n->def->f & T_TEXTN) {
        memcpy(b + *w, n->text, n->tlen);
        *w += n->tlen;
        return;
    }
    for (int i = 0; i < n->nchild; i++) tfill(n->child[i], b, w);
}

static Node **QL;
static int NQL, QCAP;

static void qpush(Node *n) {
    for (int i = 0; i < NQL; i++) if (QL[i] == n) return;
    GROW(QL, NQL, QCAP, Node *);
    QL[NQL++] = n;
}

static void qsel(Node *n, Rule *r) {
    for (int i = 0; i < n->nchild; i++) {
        Node *c = n->child[i];
        if (c->tag[0] != '#' && match_selector(r, c) >= 0) qpush(c);
        qsel(c, r);
    }
}

static int qquery(const char *sel, size_t sl) {
    NQL = 0;
    char *tmp = sdup(sel, sl);
    char *p = tmp;
    while (p && *p) {
        char *cm = strchr(p, ',');
        if (cm) *cm = 0;
        char *seg = cut(p, cm ? cm : p + strlen(p));
        if (*seg) {
            Rule r = {0};
            presplit(&r, seg);
            qsel(DOM, &r);
        }
        p = cm ? cm + 1 : 0;
    }
    free(tmp);
    return NQL;
}

static JSValue mk_el(JSContext *ctx, Node *n) {
    JSValue o = JS_NewObjectProtoClass(ctx, ELPROTO, CLS);
    JS_SetOpaque(o, n);
    return o;
}

static JSValue j_qs(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac;
    size_t sl;
    const char *s = JS_ToCStringLen(ctx, &sl, av[0]);
    if (!s) return JS_EXCEPTION;
    int n = qquery(s, sl);
    JS_FreeCString(ctx, s);
    return n ? mk_el(ctx, QL[0]) : JS_NULL;
}

static JSValue j_qsa(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac;
    size_t sl;
    const char *s = JS_ToCStringLen(ctx, &sl, av[0]);
    if (!s) return JS_EXCEPTION;
    int n = qquery(s, sl);
    JS_FreeCString(ctx, s);
    JSValue arr = JS_NewArray(ctx);
    for (int i = 0; i < n; i++) JS_SetPropertyUint32(ctx, arr, i, mk_el(ctx, QL[i]));
    return arr;
}

static JSValue j_body(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac; (void)av;
    Node *b = find_tag(DOM, K4('b', 'o', 'd', 'y'));
    return b ? mk_el(ctx, b) : JS_NULL;
}

static JSValue j_tg(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac;
    Node *n = JS_GetOpaque(av[0], CLS);
    if (!n) return JS_EXCEPTION;
    size_t tot = 0, w = 0;
    tsize(n, &tot);
    char *b = sdup("", 0);
    char *big = realloc(b, tot + 1);
    if (!big) { fputs("oom\n", stderr); exit(1); }
    tfill(n, big, &w);
    JSValue r = JS_NewStringLen(ctx, big, w);
    free(big);
    return r;
}

static JSValue j_ts(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac;
    Node *n = JS_GetOpaque(av[0], CLS);
    if (!n) return JS_EXCEPTION;
    size_t vl;
    const char *v = JS_ToCStringLen(ctx, &vl, av[1]);
    if (!v) return JS_EXCEPTION;
    n->nchild = 0;
    Node *t = node(N_TEXT);
    t->parent = n;
    t->text = sdup(v, vl);
    t->tlen = (int)vl;
    push_child(n, t);
    JS_FreeCString(ctx, v);
    return JS_UNDEFINED;
}

static JSValue j_tn(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac;
    Node *n = JS_GetOpaque(av[0], CLS);
    if (!n) return JS_EXCEPTION;
    return JS_NewString(ctx, n->tag);
}

static JSValue j_sg(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac;
    Node *n = JS_GetOpaque(av[0], CLS);
    if (!n) return JS_EXCEPTION;
    const char *k = JS_ToCString(ctx, av[1]);
    if (!k) return JS_EXCEPTION;
    int vl;
    const char *v = style_get(n, k, &vl);
    JS_FreeCString(ctx, k);
    return v ? JS_NewStringLen(ctx, v, vl) : JS_UNDEFINED;
}

static JSValue j_ss(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac;
    Node *n = JS_GetOpaque(av[0], CLS);
    if (!n) return JS_EXCEPTION;
    const char *k = JS_ToCString(ctx, av[1]), *v = JS_ToCString(ctx, av[2]);
    if (!k || !v) return JS_EXCEPTION;
    style_set(n, k, v);
    JS_FreeCString(ctx, k);
    JS_FreeCString(ctx, v);
    return JS_UNDEFINED;
}

static JSValue j_ga(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)ac;
    Node *n = JS_GetOpaque(thisv, CLS);
    if (!n) return JS_EXCEPTION;
    const char *k = JS_ToCString(ctx, av[0]);
    if (!k) return JS_EXCEPTION;
    char *v = attr_get(n, k);
    JS_FreeCString(ctx, k);
    return v ? JS_NewString(ctx, v) : JS_NULL;
}

static JSValue j_sa(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)ac;
    Node *n = JS_GetOpaque(thisv, CLS);
    if (!n) return JS_EXCEPTION;
    const char *k = JS_ToCString(ctx, av[0]), *v = JS_ToCString(ctx, av[1]);
    if (!k || !v) return JS_EXCEPTION;
    attr_set(n, k, v);
    JS_FreeCString(ctx, k);
    JS_FreeCString(ctx, v);
    return JS_UNDEFINED;
}

static JSValue j_log(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac;
    size_t l;
    const char *s = JS_ToCStringLen(ctx, &l, av[0]);
    if (!s) return JS_EXCEPTION;
    GROW(LOGS, NLOG, LCAP, char *);
    LOGS[NLOG++] = sdup(s, l);
    JS_FreeCString(ctx, s);
    return JS_UNDEFINED;
}

static JSValue j_alert(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac;
    size_t l;
    const char *s = JS_ToCStringLen(ctx, &l, av[0]);
    if (!s) return JS_EXCEPTION;
    GROW(ALERTS, NAL, ACAP, char *);
    ALERTS[NAL++] = sdup(s, l);
    JS_FreeCString(ctx, s);
    return JS_UNDEFINED;
}

static JSValue j_proto(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac; (void)av;
    return JS_DupValue(ctx, ELPROTO);
}

static Node **OCN;
static JSValue *OCV;
static int NOC, OCCAP;

static int oc_find(Node *n) {
    for (int i = 0; i < NOC; i++) if (OCN[i] == n) return i;
    return -1;
}

static JSValue j_oc(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac;
    Node *n = JS_GetOpaque(av[0], CLS);
    if (!n) return JS_EXCEPTION;
    int i = oc_find(n);
    if (i < 0) {
        if (NOC >= OCCAP) {
            OCCAP = OCCAP ? OCCAP << 1 : 8;
            OCN = realloc(OCN, (size_t)OCCAP * sizeof *OCN);
            OCV = realloc(OCV, (size_t)OCCAP * sizeof *OCV);
            if (!OCN || !OCV) { fputs("oom\n", stderr); exit(1); }
        }
        i = NOC++;
        OCN[i] = n;
        OCV[i] = JS_UNDEFINED;
    }
    JS_FreeValue(ctx, OCV[i]);
    OCV[i] = JS_DupValue(ctx, av[1]);
    return JS_UNDEFINED;
}

static JSValue j_og(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac;
    Node *n = JS_GetOpaque(av[0], CLS);
    if (!n) return JS_EXCEPTION;
    int i = oc_find(n);
    return i >= 0 ? JS_DupValue(ctx, OCV[i]) : JS_UNDEFINED;
}

static const char BOOT[] =
    "(function(){"
    "var P=_proto();"
    "var cc=function(k){return String(k).replace(/[A-Z]/g,"
    "function(c){return '-'+c.toLowerCase()})};"
    "Object.defineProperty(P,'textContent',"
    "{get:function(){return _tg(this)},set:function(v){_ts(this,String(v))}});"
    "Object.defineProperty(P,'tagName',{get:function(){return _tn(this)}});"
    "Object.defineProperty(P,'onclick',"
    "{set:function(v){_oc(this,v)},get:function(){return _og(this)}});"
    "Object.defineProperty(P,'style',{get:function(){var el=this;return new Proxy(el,{"
    "get:function(t,k){return typeof k=='symbol'?undefined:_sg(el,cc(k))},"
    "set:function(t,k,v){if(typeof k!='symbol')_ss(el,cc(k),String(v));return true}})}});"
    "globalThis.console={log:function(){var a=[];"
    "for(var i=0;i<arguments.length;i++)a.push(String(arguments[i]));"
    "_log(a.join(' '))}};"
    "globalThis.alert=function(m){_alert(String(m))};"
    "globalThis.document={querySelector:function(s){return _qs(s)},"
    "querySelectorAll:function(s){return _qsa(s)}};"
    "Object.defineProperty(globalThis.document,'body',{get:function(){return _body()}})"
    "})()";

static void js_pexc(const char *where) {
    JSValue e = JS_GetException(CTX);
    const char *m = JS_ToCString(CTX, e);
    fprintf(stderr, "js %s: %s\n", where, m ? m : "(exception)");
    if (m) JS_FreeCString(CTX, m);
    JS_FreeValue(CTX, e);
}

static void run_scripts(Node *n) {
    if (n->tagpk == K6('s', 'c', 'r', 'i', 'p', 't') && n->nchild &&
        (n->child[0]->def->f & T_TEXTN) && n->child[0]->tlen) {
        Node *t = n->child[0];
        static int sn;
        char fn[24];
        snprintf(fn, sizeof fn, "<script#%d>", ++sn);
        char *code = sdup(t->text, (size_t)t->tlen);
        JSValue r = JS_Eval(CTX, code, t->tlen, fn, JS_EVAL_TYPE_GLOBAL);
        free(code);
        if (JS_IsException(r)) js_pexc(fn);
        JS_FreeValue(CTX, r);
    }
    for (int i = 0; i < n->nchild; i++) run_scripts(n->child[i]);
}

static void js_init(void) {
    RT = JS_NewRuntime();
    CTX = JS_NewContext(RT);
    JS_NewClassID(&CLS);
    static const JSClassDef ELDEF = {.class_name = "Element"};
    JS_NewClass(RT, CLS, &ELDEF);
    ELPROTO = JS_NewObject(CTX);
    JS_SetClassProto(CTX, CLS, ELPROTO);
    JS_SetPropertyStr(CTX, ELPROTO, "getAttribute",
        JS_NewCFunction(CTX, j_ga, "getAttribute", 1));
    JS_SetPropertyStr(CTX, ELPROTO, "setAttribute",
        JS_NewCFunction(CTX, j_sa, "setAttribute", 2));
    JSValue g = JS_GetGlobalObject(CTX);
    JS_SetPropertyStr(CTX, g, "_qs", JS_NewCFunction(CTX, j_qs, "_qs", 1));
    JS_SetPropertyStr(CTX, g, "_qsa", JS_NewCFunction(CTX, j_qsa, "_qsa", 1));
    JS_SetPropertyStr(CTX, g, "_tg", JS_NewCFunction(CTX, j_tg, "_tg", 1));
    JS_SetPropertyStr(CTX, g, "_ts", JS_NewCFunction(CTX, j_ts, "_ts", 2));
    JS_SetPropertyStr(CTX, g, "_sg", JS_NewCFunction(CTX, j_sg, "_sg", 2));
    JS_SetPropertyStr(CTX, g, "_ss", JS_NewCFunction(CTX, j_ss, "_ss", 3));
    JS_SetPropertyStr(CTX, g, "_tn", JS_NewCFunction(CTX, j_tn, "_tn", 1));
    JS_SetPropertyStr(CTX, g, "_log", JS_NewCFunction(CTX, j_log, "_log", 1));
    JS_SetPropertyStr(CTX, g, "_alert", JS_NewCFunction(CTX, j_alert, "_alert", 1));
    JS_SetPropertyStr(CTX, g, "_oc", JS_NewCFunction(CTX, j_oc, "_oc", 2));
    JS_SetPropertyStr(CTX, g, "_og", JS_NewCFunction(CTX, j_og, "_og", 1));
    JS_SetPropertyStr(CTX, g, "_body", JS_NewCFunction(CTX, j_body, "_body", 0));
    JS_SetPropertyStr(CTX, g, "_proto", JS_NewCFunction(CTX, j_proto, "_proto", 0));
    JS_FreeValue(CTX, g);
    JSValue r = JS_Eval(CTX, BOOT, sizeof BOOT - 1, "<boot>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(r)) js_pexc("<boot>");
    JS_FreeValue(CTX, r);
}

static void js_done(void) {
    JS_FreeContext(CTX);
    JS_FreeRuntime(RT);
}

static void draw_dialog(const char *m) {
    int ml = (int)strlen(m);
    int W = ml + 4;
    if (W > 80) W = 80;
    int shown = ml < W - 4 ? ml : W - 4;
    int cx = W < 80 ? (80 - W) >> 1 : 0;
    int left = (W - 2 - shown) >> 1, right = W - 2 - shown - left;
    hline(cx, "", W - 2);
    printf("%*s|%*s%.*s%*s|\n", cx, "", left, "", shown, m, right, "");
    hline(cx, "", W - 2);
}

static void print_alerts(void) {
    for (int i = 0; i < NAL; i++) draw_dialog(ALERTS[i]);
}

static struct termios SAVED;

static void raw_on(void) {
    struct termios t;
    if (tcgetattr(0, &SAVED)) return;
    t = SAVED;
    t.c_lflag &= (tcflag_t)~(ICANON | ECHO | ISIG);
    tcsetattr(0, TCSANOW, &t);
}

static void raw_off(void) { tcsetattr(0, TCSANOW, &SAVED); }

static int getkey(void) {
    unsigned char c;
    if (read(0, &c, 1) != 1) return 'q';
    if (c != 27) return c;
    unsigned char b[2];
    if (read(0, b, 1) != 1) return 'q';
    if (read(0, b + 1, 1) != 1) return 'q';
    if (b[0] == '[' && b[1] == 'A') return 1;
    if (b[0] == '[' && b[1] == 'B') return 2;
    return 0;
}

static void frame(void) {
    fputs(CLEAR, stdout);
    render(DOM);
    if (NLOG) putchar('\n');
    for (int i = 0; i < NLOG; i++) puts(LOGS[i]);
}

static int ASEEN;

static void show_alerts(void) {
    for (; ASEEN < NAL; ASEEN++) {
        putchar('\n');
        draw_dialog(ALERTS[ASEEN]);
        fputs("\x1b[90mpress any key to dismiss\x1b[0m\n", stdout);
        fflush(stdout);
        unsigned char c;
        if (read(0, &c, 1) != 1) return;
    }
}

static void hint(void) {
    if (NBTN)
        fputs("\x1b[90m[\xe2\x86\x91\xe2\x86\x93/Tab] \xe9\x80\x89\xe6\x8b\xa9  [Enter] \xe7\x82\xb9\xe5\x87\xbb  [q] \xe9\x80\x80\xe5\x87\xba\x1b[0m\n", stdout);
    else
        fputs("\x1b[90m[q] \xe9\x80\x80\xe5\x87\xba\x1b[0m\n", stdout);
    fflush(stdout);
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
    DOM = dom;
    js_init();
    run_scripts(dom);
    static char NOCSS[1] = "";
    char *css = NOCSS;
    Node *st = find_tag(dom, K_STYLE);
    if (st && st->nchild && (st->child[0]->def->f & T_TEXTN)) {
        Node *t = st->child[0];
        css = t->text, t->text[t->tlen] = 0;
    }
    parse_css(css);
    apply_styles(dom);
    if (!isatty(0) || !isatty(1)) {
        render(dom);
        if (NLOG) putchar('\n');
        for (int i = 0; i < NLOG; i++) puts(LOGS[i]);
        if (NAL) putchar('\n');
        print_alerts();
        js_done();
        return 0;
    }
    qquery("button", 6);
    NBTN = NQL;
    if (NBTN) {
        BTNS = malloc((size_t)NQL * sizeof *BTNS);
        if (!BTNS) { fputs("oom\n", stderr); exit(1); }
        memcpy(BTNS, QL, (size_t)NQL * sizeof *BTNS);
        FOC = BTNS[0];
    }
    raw_on();
    fputs("\x1b[?25l", stdout);
    frame();
    show_alerts();
    hint();
    for (;;) {
        int k = getkey();
        if (k == 'q' || k == 3) break;
        if (k == 1 || k == 2 || k == '\t') {
            if (NBTN) {
                FOCI = k == 1 ? (FOCI + NBTN - 1) % NBTN : (FOCI + 1) % NBTN;
                FOC = BTNS[FOCI];
            }
        } else if (k == '\r' || k == '\n' || k == ' ') {
            if (NBTN) {
                int i = oc_find(BTNS[FOCI]);
                if (i >= 0) {
                    JSValue el = mk_el(CTX, BTNS[FOCI]);
                    JSValue r = JS_Call(CTX, OCV[i], el, 0, 0);
                    JS_FreeValue(CTX, el);
                    if (JS_IsException(r)) js_pexc("<click>");
                    JS_FreeValue(CTX, r);
                    apply_styles(dom);
                }
            }
        }
        frame();
        show_alerts();
        hint();
    }
    js_done();
    raw_off();
    fputs("\x1b[?25h", stdout);
    putchar('\n');
    return 0;
}
