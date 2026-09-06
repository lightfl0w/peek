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

typedef struct Attr { char *k, *v; } Attr;
typedef struct Node Node;
struct Node {
    char *tag; uint64_t tagpk;
    char *text; int tlen;
    Attr attrs[16]; int nattr;
    Node *child[32]; int nchild;
    Node *parent;
    struct { char *k, *v; int spec; } st[32]; int nst;
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
                n->tlen = e - t;
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
                n->parent = stk[top];
                stk[top]->child[stk[top]->nchild++] = n;
                if (!sc && !isvoid(n->tagpk) && top < 63) stk[++top] = n;
            }
        }
    }
    return stk[0];
}

typedef struct { char *k, *v; } Decl;
typedef struct { char *sel; Decl d[16]; int nd; } Rule;
static Rule R[64]; static int NR;

static void split_decls(char *s, char *e, Rule *r) {
    while (s < e && r->nd < 16) {
        char *semi = memchr(s, ';', e - s);
        char *de = semi ? semi : e;
        char *c = memchr(s, ':', de - s);
        if (c) {
            r->d[r->nd].k = cut(s, c);
            r->d[r->nd].v = cut(c + 1, de);
            r->nd++;
        }
        s = de + 1;
    }
}

static void parse_css(char *css) {
    char *pos = css;
    while (*pos && NR < 64) {
        char *b = strchr(pos, '{');
        if (!b) break;
        char *e = strchr(b, '}');
        if (!e) break;
        char *s0 = pos;
        for (char *q = pos; q < b; q++) if (*q == '}') s0 = q + 1;
        Rule *r = R + NR++;
        r->sel = cut(s0, b);
        split_decls(b + 1, e, r);
        if (!*r->sel) NR--;
        pos = e + 1;
    }
}

static int match_compound(const char *c, Node *n) {  
    if (n->tag[0] == '#') return -1;
    int ids = 0, cls = 0, tags = 0, cl = 0;
    char mode = 't', cur[64], buf[256], pat[66];
    for (int k = 0;; k++) {
        char ch = c[k];
        if (ch == '.' || ch == '#' || !ch) {
            cur[cl] = 0;
            if (cl) {
                uint64_t ck = pk(cur);
                if (mode == 't') {
                    if (n->tagpk != ck) return -1;
                    tags++;
                } else {
                    uint64_t ak = mode == '.' ? K_CLASS : K_ID;
                    const char *v = 0;
                    for (int i = 0; i < n->nattr; i++)
                        if (pk(n->attrs[i].k) == ak) v = n->attrs[i].v;
                    if (!v) return -1;
                    if (mode == '.') {
                        snprintf(buf, sizeof buf, " %s ", v);
                        snprintf(pat, sizeof pat, " %s ", cur);
                        if (!strstr(buf, pat)) return -1;
                        cls++;
                    } else {
                        if (pk(v) != ck) return -1;
                        ids++;
                    }
                }
            }
            if (!ch) break;
            mode = ch;
            cl = 0;
        } else if (cl < 63) cur[cl++] = ch;
    }
    return (ids << 8) | (cls << 4) | tags;
}

static int match_selector(const char *sel, Node *n) {
    const char *ps[8]; int pl[8], np = 0;
    for (const char *p = sel; *p && np < 8;) {
        while (ISWS(*p)) p++;
        if (!*p) break;
        ps[np] = p;
        while (*p && !ISWS(*p)) p++;
        pl[np] = p - ps[np];
        np++;
    }
    if (!np) return -1;
    char cb[64];
    memcpy(cb, ps[np - 1], pl[np - 1]); cb[pl[np - 1]] = 0;
    int spec = match_compound(cb, n);
    if (spec < 0) return -1;
    Node *a = n->parent;
    for (int k = np - 2; k >= 0; k--) {
        int s = -1;
        memcpy(cb, ps[k], pl[k]); cb[pl[k]] = 0;
        while (a && (s = match_compound(cb, a)) < 0) a = a->parent;
        if (!a) return -1;
        spec += s;
        a = a->parent;
    }
    return spec;
}

static void apply_decls(Rule *r, Node *n, int spec) {
    for (int i = 0; i < r->nd; i++) {
        uint64_t k = pk(r->d[i].k);
        int j = 0;
        while (j < n->nst && pk(n->st[j].k) != k) j++;
        if (j == n->nst && n->nst < 32) {
            n->st[j].k = r->d[i].k;
            n->nst++;
        }
        if (j < n->nst && (!n->st[j].v || spec >= n->st[j].spec)) {
            n->st[j].v = r->d[i].v;
            n->st[j].spec = spec;
        }
    }
}

static void apply_styles(Node *n) {
    if (n->tag[0] != '#') {
        for (int r = 0; r < NR; r++) {
            int spec = match_selector(R[r].sel, n);
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
    for (int c = 0; c < n->nchild; c++) apply_styles(n->child[c]);
}

static int ansi_color(const char *v) {
    while (*v == ' ') v++;
    if (isdigit((unsigned char)*v)) { int n = atoi(v); return n < 256 ? n : -1; }
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
        if (r == g && g == b) return r < 8 ? 16 : r > 248 ? 231 : 232 + ((r - 8) * 205 >> 11);
        return 16 + 36 * ((r * 1287 + 32896) >> 16)   
             + 6 * ((g * 1287 + 32896) >> 16) + ((b * 1287 + 32896) >> 16);
    }
    switch (pk(v)) {
        case K5('b', 'l', 'a', 'c', 'k'): return 0;
        case K3('r', 'e', 'd'): return 1;
        case K5('g', 'r', 'e', 'e', 'n'): return 2;
        case K6('y', 'e', 'l', 'l', 'o', 'w'): return 3;
        case K4('b', 'l', 'u', 'e'): return 4;
        case K7('m', 'a', 'g', 'e', 'n', 't', 'a'): return 5;
        case K4('c', 'y', 'a', 'n'): return 6;
        case K5('w', 'h', 'i', 't', 'e'): return 7;
    }
    return -1;
}

static void emit_style(Node *n) {
    for (int i = 0; i < n->nst; i++) {
        const char *k = n->st[i].k, *v = n->st[i].v;
        int c = ansi_color(v);
        switch (pk(k)) {
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

static void draw_button(Node *n) {
    char text[256] = "";
    collect_text(n, text, sizeof text);
    int pad = 1, fg = -1, bg = -1, flags = 0;    
    for (int i = 0; i < n->nst; i++) {
        const char *k = n->st[i].k, *v = n->st[i].v;
        switch (pk(k)) {
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
        }
    }
    char seq[64] = "";
    if (fg >= 0) sprintf(seq + strlen(seq), "\x1b[38;5;%dm", fg);
    if (bg >= 0) sprintf(seq + strlen(seq), "\x1b[48;5;%dm", bg);
    if (flags & 1) strcat(seq, "\x1b[1m");
    if (flags & 2) strcat(seq, "\x1b[3m");
    if (flags & 4) strcat(seq, "\x1b[4m");
    int w = (int)strlen(text) + pad * 2;
    printf("%s+%.*s+%s\n", seq, w, "------------------------------------------------", RESET);
    printf("%s|%*s%s%*s|%s\n", seq, pad, "", text, pad, "", RESET);
    printf("%s+%.*s+%s\n", seq, w, "------------------------------------------------", RESET);
}

static void render(Node *n) {
    if (n->tagpk == K_TEXT) {
        Node *p = n->parent;
        if (p && p->tagpk != K_BUTTON) {
            emit_style(p);
            printf("%.*s", n->tlen, n->text);
            fputs(RESET, stdout);
        }
        return;
    }
    if (n->tagpk == K_BUTTON) { draw_button(n); return; }
    if (n->tagpk == K_BR) { putchar('\n'); return; }
    if (n->tagpk == K_STYLE) return;
    for (int i = 0; i < n->nchild; i++) render(n->child[i]);
    if (n->tagpk == K_DIV || n->tagpk == K_P ||
        n->tagpk == K_HTML || n->tagpk == K_BODY) putchar('\n');
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
    char *css = "";
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
