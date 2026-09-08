#include "peek.h"

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

static void add_text(char *s, char *e, Node *parent) {
    char sv = *e;
    char *t = cut(s, e);
    if (*t) {
        Node *n = node(N_TEXT);
        n->text = t;
        n->tlen = unescape(t, (int)strlen(t));
        n->parent = parent;
        push_child(parent, n);
    }
    *e = sv;
}

static char *tag_end(char *p, char *end) {
    char q = 0;
    for (char *s = p; s < end; s++) {
        if (q) {
            if (*s == q) q = 0;
        } else if (*s == '"' || *s == '\'') {
            q = *s;
        } else if (*s == '>') {
            return s;
        }
    }
    return 0;
}

static char *raw_end(char *p, char *end, const char *name, int nl) {
    char *q = p;
    while (q < end) {
        char *lt = memchr(q, '<', (size_t)(end - q));
        if (!lt) return end;
        if (lt + 1 < end && lt[1] == '/' && lt + 2 + nl <= end &&
            !strncasecmp(lt + 2, name, (size_t)nl)) {
            char c = lt + 2 + nl < end ? lt[2 + nl] : (char)0;
            if (lt + 2 + nl >= end || c == '>' || ISWS(c) || c == '/') return lt;
        }
        q = lt + 1;
    }
    return end;
}

Node *parse_html(char *src) {
    Node **stk = 0;
    int scap = 0, top = 0;
    GROW(stk, 1, scap, Node *);
    stk[top++] = node(N_ROOT);
    char *end = src + strlen(src);
    char *p = src;
    while (p < end) {
        if (*p != '<') {
            char *e = memchr(p, '<', (size_t)(end - p));
            if (!e) e = end;
            add_text(p, e, stk[top - 1]);
            p = e;
        } else {
            char *e = tag_end(p, end);
            if (!e) {
                char *nx = p + 1 < end
                    ? memchr(p + 1, '<', (size_t)(end - p - 1))
                    : 0;
                char *t = nx ? nx : end;
                add_text(p + 1, t, stk[top - 1]);
                p = t;
                continue;
            }
            char *t = cut(p + 1, e);
            p = e + 1;
            if (!strncmp(t, "!--", 3)) {
                char *c = strstr(p, "-->");
                p = c ? c + 3 : end;
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
                int raw = 0;
                if (!sc && !(n->def->f & T_VOID)) {
                    if (n->taglen == 6 && n->tagpk == K6('s', 'c', 'r', 'i', 'p', 't'))
                        raw = 1;
                    else if (n->taglen == 5 && n->tagpk == K5('s', 't', 'y', 'l', 'e'))
                        raw = 1;
                }
                if (raw) {
                    char *re = raw_end(p, end, n->tag, n->taglen);
                    add_text(p, re, n);
                    char *close = memchr(re, '>', (size_t)(end - re));
                    p = close ? close + 1 : end;
                } else if (!sc && !(n->def->f & T_VOID)) {
                    GROW(stk, top + 1, scap, Node *);
                    stk[top++] = n;
                }
            }
        }
    }
    Node *root = stk[0];
    free(stk);
    return root;
}
